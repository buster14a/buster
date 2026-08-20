// Canonical-IR-to-native-code orchestration: one call to
// codegen_generate_canonical_module near the bottom of the file turns an
// IrModule into a CodegenModule — code bytes, global data images,
// relocations, unwind actions, debug locations, and statistics — for x86-64
// and AArch64. "Canonical" names the register-allocation-free baseline
// emitter that keeps every value in its own frame slot; eligible functions
// are routed through the machine path first (machine_select_canonical_-
// function, then MIR_STACK/FAST/QUALITY placement and the machine encoder
// in machine.c and its included backends), and any function the machine
// subset cannot express falls back per function to the canonical emitter,
// counted in statistics.fallback_opcode_counts.
//
// Nearly everything here sits under one function:
// codegen_generate_canonical_module_attempt lays out global data (read-only
// / writable / thread-local / zero-fill images plus initializer
// relocations), then per function sizes the frame, tries the machine path,
// and otherwise walks each block's instructions emitting native code
// directly. codegen_generate_canonical_module wraps it in a retry loop that
// grows the code-buffer capacity scale when an attempt runs out.
//
// The helper regions above it, in file order; anchors are definitions:
//   codegen_inline_assembly_*                    GNU inline-assembly template
//                                                resolution and mnemonic
//                                                gating
//   codegen_canonical_x64_metadata_*             x86 encoding through the
//                                                assembly metadata tables,
//                                                with per-run recipe and
//                                                template caches
//   codegen_abi_for_target, codegen_prewarm      ABI selection and the serial
//                                                table prewarm (AGENTS.md)
//   codegen_canonical_x64_f80_cache_*,           ABI classification: x87 f80
//   codegen_canonical_aggregate_abi,             shapes, aggregate part
//   codegen_canonical_x64_call_layout_cached     splitting, SysV and Win64
//                                                call layout
//   codegen_canonical_*_adjust_stack             frame setup and stack probes
//   codegen_canonical_x64_evex_*,                AVX-512 SIMD emission for
//   codegen_canonical_x64_simd_emit_*            IR_OPCODE_SIMD
//   codegen_global_assembly_*                    module-level asm directives
//   a64_emit_*, codegen_canonical_a64_*          AArch64 emission helpers

#include <buster/lib/compiler/codegen/codegen_internal.h>

bool codegen_module_relocation_kind_valid(u8 kind)
{
    return kind < (u8)CODEGEN_MODULE_RELOCATION_COUNT;
}

bool codegen_module_relocation_valid(CodegenModuleRelocation* relocation)
{
    if (!relocation || !codegen_module_relocation_kind_valid(relocation->kind) ||
        relocation->source >= CODEGEN_MODULE_RELOCATION_SOURCE_COUNT)
    {
        return false;
    }

    CodegenModuleRelocationKind kind = (CodegenModuleRelocationKind)relocation->kind;
    bool aarch64 = false;
    bool absolute = false;
    bool is_thread_local = false;
    bool thread_local_low = false;
    bool thread_local_index = false;
    switch (kind)
    {
        case CODEGEN_MODULE_RELOCATION_X86_64_PC32:
            break;
        case CODEGEN_MODULE_RELOCATION_AARCH64_CALL26:
            aarch64 = true;
            break;
        case CODEGEN_MODULE_RELOCATION_ABSOLUTE32:
        case CODEGEN_MODULE_RELOCATION_ABSOLUTE64:
            absolute = true;
            break;
        case CODEGEN_MODULE_RELOCATION_X86_64_TPOFF32:
            is_thread_local = true;
            break;
        case CODEGEN_MODULE_RELOCATION_X86_64_PE_TLS_INDEX_PC32:
            is_thread_local = true;
            thread_local_index = true;
            break;
        case CODEGEN_MODULE_RELOCATION_PE_TLS_OFFSET32:
            is_thread_local = true;
            break;
        case CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP:
            aarch64 = true;
            is_thread_local = true;
            thread_local_index = true;
            break;
        case CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_INDEX_LO12:
            aarch64 = true;
            is_thread_local = true;
            thread_local_low = true;
            thread_local_index = true;
            break;
        case CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_OFFSET12:
            aarch64 = true;
            is_thread_local = true;
            break;
        case CODEGEN_MODULE_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12:
            aarch64 = true;
            is_thread_local = true;
            break;
        case CODEGEN_MODULE_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12:
            aarch64 = true;
            is_thread_local = true;
            thread_local_low = true;
            break;
        case CODEGEN_MODULE_RELOCATION_X86_64_MACH_TLV_PC32:
            is_thread_local = true;
            break;
        case CODEGEN_MODULE_RELOCATION_AARCH64_MACH_TLVP_PAGE21:
            aarch64 = true;
            is_thread_local = true;
            break;
        case CODEGEN_MODULE_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12:
            aarch64 = true;
            is_thread_local = true;
            thread_local_low = true;
            break;
        case CODEGEN_MODULE_RELOCATION_AARCH64_MACH_PAGE21:
        case CODEGEN_MODULE_RELOCATION_AARCH64_MACH_PAGEOFF12:
            aarch64 = true;
            break;
        case CODEGEN_MODULE_RELOCATION_COUNT:
            return false;
    }

    // The enum is authoritative.  These compatibility bits are checked,
    // never consulted to choose a kind, so stale combinations fail loudly at
    // the conversion boundary instead of silently producing the wrong object
    // relocation.  label_address is an independent static-label operation,
    // but it can only carry an absolute address payload.
    return relocation->aarch64 == aarch64 && relocation->absolute == absolute && relocation->is_thread_local == is_thread_local &&
           relocation->thread_local_low == thread_local_low && relocation->thread_local_index == thread_local_index &&
           (!relocation->label_address || kind == CODEGEN_MODULE_RELOCATION_ABSOLUTE64);
}

#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/integer.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>

#define X64_VALUE_SLOT_SIZE 32
#define X64_VALUE_SLOT_COMPONENT_COUNT 4
#define A64_VALUE_SLOT_SIZE 32

BUSTER_GLOBAL_LOCAL String8 const codegen_x64_asm_names64[] = {
    S8_INITIALIZER("rax"), S8_INITIALIZER("rcx"), S8_INITIALIZER("rdx"), S8_INITIALIZER("rbx"), S8_INITIALIZER("rsp"), S8_INITIALIZER("rbp"), S8_INITIALIZER("rsi"), S8_INITIALIZER("rdi"),
    S8_INITIALIZER("r8"), S8_INITIALIZER("r9"), S8_INITIALIZER("r10"), S8_INITIALIZER("r11"), S8_INITIALIZER("r12"), S8_INITIALIZER("r13"), S8_INITIALIZER("r14"), S8_INITIALIZER("r15"),
};
BUSTER_GLOBAL_LOCAL String8 const codegen_x64_asm_names32[] = {
    S8_INITIALIZER("eax"), S8_INITIALIZER("ecx"), S8_INITIALIZER("edx"), S8_INITIALIZER("ebx"), S8_INITIALIZER("esp"), S8_INITIALIZER("ebp"), S8_INITIALIZER("esi"), S8_INITIALIZER("edi"),
    S8_INITIALIZER("r8d"), S8_INITIALIZER("r9d"), S8_INITIALIZER("r10d"), S8_INITIALIZER("r11d"), S8_INITIALIZER("r12d"), S8_INITIALIZER("r13d"), S8_INITIALIZER("r14d"), S8_INITIALIZER("r15d"),
};
BUSTER_GLOBAL_LOCAL String8 const codegen_x64_asm_names16[] = {
    S8_INITIALIZER("ax"), S8_INITIALIZER("cx"), S8_INITIALIZER("dx"), S8_INITIALIZER("bx"), S8_INITIALIZER("sp"), S8_INITIALIZER("bp"), S8_INITIALIZER("si"), S8_INITIALIZER("di"),
    S8_INITIALIZER("r8w"), S8_INITIALIZER("r9w"), S8_INITIALIZER("r10w"), S8_INITIALIZER("r11w"), S8_INITIALIZER("r12w"), S8_INITIALIZER("r13w"), S8_INITIALIZER("r14w"), S8_INITIALIZER("r15w"),
};
BUSTER_GLOBAL_LOCAL String8 const codegen_x64_asm_names8[] = {
    S8_INITIALIZER("al"), S8_INITIALIZER("cl"), S8_INITIALIZER("dl"), S8_INITIALIZER("bl"), S8_INITIALIZER("spl"), S8_INITIALIZER("bpl"), S8_INITIALIZER("sil"), S8_INITIALIZER("dil"),
    S8_INITIALIZER("r8b"), S8_INITIALIZER("r9b"), S8_INITIALIZER("r10b"), S8_INITIALIZER("r11b"), S8_INITIALIZER("r12b"), S8_INITIALIZER("r13b"), S8_INITIALIZER("r14b"), S8_INITIALIZER("r15b"),
};
BUSTER_GLOBAL_LOCAL String8 const codegen_x64_asm_mnemonics[] = {
    S8_INITIALIZER("mov"), S8_INITIALIZER("movb"), S8_INITIALIZER("movw"), S8_INITIALIZER("movl"), S8_INITIALIZER("movq"), S8_INITIALIZER("movzx"), S8_INITIALIZER("movsx"), S8_INITIALIZER("movzb"), S8_INITIALIZER("movzw"), S8_INITIALIZER("movzl"),
    S8_INITIALIZER("movsxb"), S8_INITIALIZER("movsxw"), S8_INITIALIZER("movsxl"), S8_INITIALIZER("add"), S8_INITIALIZER("addb"), S8_INITIALIZER("addw"), S8_INITIALIZER("addl"), S8_INITIALIZER("addq"), S8_INITIALIZER("sub"), S8_INITIALIZER("subb"),
    S8_INITIALIZER("subw"), S8_INITIALIZER("subl"), S8_INITIALIZER("subq"), S8_INITIALIZER("xor"), S8_INITIALIZER("xorb"), S8_INITIALIZER("xorw"), S8_INITIALIZER("xorl"), S8_INITIALIZER("xorq"), S8_INITIALIZER("or"), S8_INITIALIZER("orb"), S8_INITIALIZER("orw"),
    S8_INITIALIZER("orl"), S8_INITIALIZER("orq"), S8_INITIALIZER("and"), S8_INITIALIZER("andb"), S8_INITIALIZER("andw"), S8_INITIALIZER("andl"), S8_INITIALIZER("andq"), S8_INITIALIZER("cmp"), S8_INITIALIZER("cmpb"), S8_INITIALIZER("cmpw"), S8_INITIALIZER("cmpl"),
    S8_INITIALIZER("cmpq"), S8_INITIALIZER("test"), S8_INITIALIZER("testb"), S8_INITIALIZER("testw"), S8_INITIALIZER("testl"), S8_INITIALIZER("testq"), S8_INITIALIZER("xchg"), S8_INITIALIZER("xchgb"), S8_INITIALIZER("xchgw"), S8_INITIALIZER("xchgl"),
    S8_INITIALIZER("xchgq"), S8_INITIALIZER("inc"), S8_INITIALIZER("incb"), S8_INITIALIZER("incw"), S8_INITIALIZER("incl"), S8_INITIALIZER("incq"), S8_INITIALIZER("dec"), S8_INITIALIZER("decb"), S8_INITIALIZER("decw"), S8_INITIALIZER("decl"), S8_INITIALIZER("decq"),
    S8_INITIALIZER("neg"), S8_INITIALIZER("negb"), S8_INITIALIZER("negw"), S8_INITIALIZER("negl"), S8_INITIALIZER("negq"), S8_INITIALIZER("not"), S8_INITIALIZER("notb"), S8_INITIALIZER("notw"), S8_INITIALIZER("notl"), S8_INITIALIZER("notq"), S8_INITIALIZER("bswap"),
    S8_INITIALIZER("bswapl"), S8_INITIALIZER("bswapq"),
};
BUSTER_GLOBAL_LOCAL String8 const codegen_x64_asm_registers[] = {
    S8_INITIALIZER("rax"), S8_INITIALIZER("eax"), S8_INITIALIZER("ax"), S8_INITIALIZER("al"), S8_INITIALIZER("rcx"), S8_INITIALIZER("ecx"), S8_INITIALIZER("cx"), S8_INITIALIZER("cl"), S8_INITIALIZER("rdx"), S8_INITIALIZER("edx"), S8_INITIALIZER("dx"), S8_INITIALIZER("dl"),
    S8_INITIALIZER("rbx"), S8_INITIALIZER("ebx"), S8_INITIALIZER("bx"), S8_INITIALIZER("bl"), S8_INITIALIZER("rsp"), S8_INITIALIZER("esp"), S8_INITIALIZER("sp"), S8_INITIALIZER("spl"), S8_INITIALIZER("rbp"), S8_INITIALIZER("ebp"), S8_INITIALIZER("bp"), S8_INITIALIZER("bpl"),
    S8_INITIALIZER("rsi"), S8_INITIALIZER("esi"), S8_INITIALIZER("si"), S8_INITIALIZER("sil"), S8_INITIALIZER("rdi"), S8_INITIALIZER("edi"), S8_INITIALIZER("di"), S8_INITIALIZER("dil"), S8_INITIALIZER("r8"), S8_INITIALIZER("r8d"), S8_INITIALIZER("r8w"), S8_INITIALIZER("r8b"),
    S8_INITIALIZER("r9"), S8_INITIALIZER("r9d"), S8_INITIALIZER("r9w"), S8_INITIALIZER("r9b"), S8_INITIALIZER("r10"), S8_INITIALIZER("r10d"), S8_INITIALIZER("r10w"), S8_INITIALIZER("r10b"), S8_INITIALIZER("r11"), S8_INITIALIZER("r11d"), S8_INITIALIZER("r11w"),
    S8_INITIALIZER("r11b"), S8_INITIALIZER("r12"), S8_INITIALIZER("r12d"), S8_INITIALIZER("r12w"), S8_INITIALIZER("r12b"), S8_INITIALIZER("r13"), S8_INITIALIZER("r13d"), S8_INITIALIZER("r13w"), S8_INITIALIZER("r13b"), S8_INITIALIZER("r14"), S8_INITIALIZER("r14d"),
    S8_INITIALIZER("r14w"), S8_INITIALIZER("r14b"), S8_INITIALIZER("r15"), S8_INITIALIZER("r15d"), S8_INITIALIZER("r15w"), S8_INITIALIZER("r15b"),
};
BUSTER_GLOBAL_LOCAL X64Register const codegen_x64_asm_system_v_registers[] = {
    X64_REGISTER_RAX, X64_REGISTER_RCX, X64_REGISTER_RDX, X64_REGISTER_RSI, X64_REGISTER_RDI,
    X64_REGISTER_R8, X64_REGISTER_R9, X64_REGISTER_R10, X64_REGISTER_R11,
};
BUSTER_GLOBAL_LOCAL X64Register const codegen_x64_asm_windows_registers[] = {
    X64_REGISTER_RAX, X64_REGISTER_RCX, X64_REGISTER_RDX, X64_REGISTER_R8, X64_REGISTER_R9, X64_REGISTER_R10, X64_REGISTER_R11,
};

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_jump_target(IrFunction* function, IrInstruction* instruction, String8 literal, String8 prefix,
                                                             u32* target_index_out)
{
    return ir_inline_assembly_jump_target(function, instruction, literal, prefix, target_index_out);
}

BUSTER_GLOBAL_LOCAL void codegen_emit_u8(CodegenBuffer* buffer, u8 value);
BUSTER_GLOBAL_LOCAL void codegen_emit_u32(CodegenBuffer* buffer, u32 value);
BUSTER_GLOBAL_LOCAL BUSTER_COLD BUSTER_PRESERVE_MOST void codegen_buffer_report_exhausted(CodegenBuffer* buffer);
BUSTER_GLOBAL_LOCAL u32 codegen_inline_assembly_type_class(IrType* type);

BUSTER_GLOBAL_LOCAL bool codegen_decimal_number(String8 string, u64* value_out)
{
    if (!string.pointer || !string.length || !value_out)
    {
        return false;
    }
    u64 value = 0;
    for (u64 index = 0; index < string.length; index += 1)
    {
        u8 digit = (u8)string.pointer[index];
        if (digit < '0' || digit > '9' || value > (UINT64_MAX - (digit - '0')) / 10)
        {
            return false;
        }
        value = value * 10 + (digit - '0');
    }
    *value_out = value;
    return true;
}

// The canonical emitter only gives ordinary inline assembly scalar GPR
// operands.  Keep the spelling table here, rather than teaching the
// instruction encoder about compiler values: the former is the ABI contract,
// while the latter is just a source-level substitution.
BUSTER_GLOBAL_LOCAL String8 codegen_x64_asm_register_name(X64Register register_index, u32 width)
{
    if ((u32)register_index >= BUSTER_ARRAY_LENGTH(codegen_x64_asm_names64))
    {
        return (String8){0};
    }
    switch (width)
    {
    case 1: return codegen_x64_asm_names8[register_index];
    case 2: return codegen_x64_asm_names16[register_index];
    case 4: return codegen_x64_asm_names32[register_index];
    case 8: return codegen_x64_asm_names64[register_index];
    }
    return (String8){0};
}

BUSTER_GLOBAL_LOCAL AssemblySyntax codegen_inline_assembly_syntax(CodegenModuleOptions options)
{
    // GNU's default dialect is AT&T.  Do not pass DEFAULT to assembly_encode:
    // its standalone API intentionally defaults to Intel for historical
    // callers, whereas a GNU asm template must retain the driver's meaning.
    return options.assembly_syntax == ASSEMBLY_SYNTAX_INTEL ? ASSEMBLY_SYNTAX_INTEL : ASSEMBLY_SYNTAX_ATT;
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_operand_name_index(IrInstructionExtra extra, String8 name, u32* index_out)
{
    if (!extra.operand_names)
    {
        return false;
    }
    for (u32 index = 0; index < extra.operand_name_count; index += 1)
    {
        if (string_equal(extra.operand_names[index], name))
        {
            if (index_out)
            {
                *index_out = index;
            }
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_template_reference(String8 source, u64 percent_index, IrInstructionExtra extra,
                                                                    u32 operand_count, u32* operand_index_out, u64* end_out)
{
    if (percent_index + 1 >= source.length)
    {
        return false;
    }
    u64 index = percent_index + 1;
    if (source.pointer[index] == '[')
    {
        u64 name_start = index + 1;
        u64 name_end = name_start;
        while (name_end < source.length && source.pointer[name_end] != ']')
        {
            name_end += 1;
        }
        if (name_end == name_start || name_end >= source.length)
        {
            return false;
        }
        u32 operand_index = 0;
        if (!codegen_inline_assembly_operand_name_index(extra,
                                                        (String8){.pointer = source.pointer + name_start, .length = name_end - name_start},
                                                        &operand_index) || operand_index >= operand_count)
        {
            return false;
        }
        *operand_index_out = operand_index;
        *end_out = name_end + 1;
        return true;
    }
    if (source.pointer[index] < '0' || source.pointer[index] > '9')
    {
        return false;
    }
    u64 value = 0;
    u64 end = index;
    while (end < source.length && source.pointer[end] >= '0' && source.pointer[end] <= '9')
    {
        u8 digit = (u8)(source.pointer[end] - '0');
        if (value > (UINT64_MAX - digit) / 10)
        {
            return false;
        }
        value = value * 10 + digit;
        end += 1;
    }
    if (value >= operand_count || value > UINT32_MAX)
    {
        return false;
    }
    *operand_index_out = (u32)value;
    *end_out = end;
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_mnemonic_allowed(String8 mnemonic)
{
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(codegen_x64_asm_mnemonics); index += 1)
    {
        if (string_equal(mnemonic, codegen_x64_asm_mnemonics[index]))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_register_name(String8 token)
{
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(codegen_x64_asm_registers); index += 1)
    {
        if (string_equal(token, codegen_x64_asm_registers[index]))
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_literal_registers_absent(String8 source)
{
    // A literal register is not tied to a compiler operand.  Reject it even
    // when the general assembler could encode it: otherwise a generic input
    // allocated in (say) RAX could be silently overwritten by `%%rax`.
    for (u64 index = 0; index < source.length; index += 1)
    {
        if (source.pointer[index] != '%')
        {
            continue;
        }
        u64 token_start = index + 1;
            if (token_start < source.length && source.pointer[token_start] == '%')
            {
                token_start += 1;
            }
        if (token_start < source.length && source.pointer[token_start] == '[')
        {
            while (token_start < source.length && source.pointer[token_start] != ']')
            {
                token_start += 1;
            }
            index = token_start;
            continue;
        }
        if (token_start < source.length && (source.pointer[token_start] >= '0' && source.pointer[token_start] <= '9'))
        {
            while (token_start < source.length && source.pointer[token_start] >= '0' && source.pointer[token_start] <= '9')
            {
                token_start += 1;
            }
            index = token_start;
            continue;
        }
        u64 token_end = token_start;
        while (token_end < source.length && ((source.pointer[token_end] >= 'a' && source.pointer[token_end] <= 'z') ||
                                              (source.pointer[token_end] >= 'A' && source.pointer[token_end] <= 'Z') ||
                                              (source.pointer[token_end] >= '0' && source.pointer[token_end] <= '9')))
        {
            token_end += 1;
        }
        if (token_end > token_start && codegen_inline_assembly_register_name((String8){.pointer = source.pointer + token_start,
                                                                                .length = token_end - token_start}))
        {
            return false;
        }
        index = token_end;
    }
    // Intel templates spell physical registers without a percent.  Scan words
    // in that dialect; ATT mnemonics and punctuation do not match the table.
    for (u64 index = 0; index < source.length;)
    {
        if (source.pointer[index] == '%')
        {
            if (index + 1 < source.length && source.pointer[index + 1] == '%')
            {
                index += 2;
                continue;
            }
            if (index + 1 < source.length && source.pointer[index + 1] == '[')
            {
                while (index < source.length && source.pointer[index] != ']')
                {
                    index += 1;
                }
                if (index < source.length)
                {
                    index += 1;
                }
                continue;
            }
            if (index + 1 < source.length && source.pointer[index + 1] >= '0' && source.pointer[index + 1] <= '9')
            {
                index += 2;
                while (index < source.length && source.pointer[index] >= '0' && source.pointer[index] <= '9')
                {
                    index += 1;
                }
                continue;
            }
        }
        while (index < source.length && !((source.pointer[index] >= 'a' && source.pointer[index] <= 'z') ||
                                          (source.pointer[index] >= 'A' && source.pointer[index] <= 'Z')))
        {
            index += 1;
        }
        u64 start = index;
        while (index < source.length && ((source.pointer[index] >= 'a' && source.pointer[index] <= 'z') ||
                                         (source.pointer[index] >= 'A' && source.pointer[index] <= 'Z') ||
                                         (source.pointer[index] >= '0' && source.pointer[index] <= '9')))
        {
            index += 1;
        }
        if (index > start && codegen_inline_assembly_register_name((String8){.pointer = source.pointer + start, .length = index - start}))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_register_only_source(String8 source)
{
    // The canonical path deliberately has no memory or immediate constraints.
    // Reject their source spellings before handing the text to the general
    // assembler, including Intel memory forms that have no '$' marker.
    for (u64 index = 0; index < source.length; index += 1)
    {
        u8 character = (u8)source.pointer[index];
        if (character == '$' || character == '[' || character == ']' || character == '(' || character == ')' || character == '*' || character == ':')
        {
            return false;
        }
    }
    u64 index = 0;
    while (index < source.length)
    {
        while (index < source.length && (source.pointer[index] == ' ' || source.pointer[index] == '\t' || source.pointer[index] == '\r' ||
                                         source.pointer[index] == '\n' || source.pointer[index] == ';'))
        {
            index += 1;
        }
        if (index >= source.length)
        {
            break;
        }
        u64 mnemonic_start = index;
        while (index < source.length && source.pointer[index] != ' ' && source.pointer[index] != '\t' && source.pointer[index] != '\r' &&
               source.pointer[index] != '\n' && source.pointer[index] != ';' && source.pointer[index] != ',')
        {
            index += 1;
        }
        if (!codegen_inline_assembly_mnemonic_allowed((String8){.pointer = source.pointer + mnemonic_start, .length = index - mnemonic_start}))
        {
            return false;
        }
        u64 line_end = index;
        while (line_end < source.length && source.pointer[line_end] != '\n' && source.pointer[line_end] != ';' && source.pointer[line_end] != '\r')
        {
            line_end += 1;
        }
        while (index < line_end)
        {
            while (index < line_end && (source.pointer[index] == ' ' || source.pointer[index] == '\t' || source.pointer[index] == ','))
            {
                index += 1;
            }
            if (index >= line_end)
            {
                break;
            }
            // Every ordinary operand is a substituted GPR.  A leading digit
            // or sign therefore denotes an immediate, while '$' is the GNU
            // spelling already rejected by the punctuation pass above.
            if ((source.pointer[index] >= '0' && source.pointer[index] <= '9') || source.pointer[index] == '+' || source.pointer[index] == '-')
            {
                return false;
            }
            while (index < line_end && source.pointer[index] != ',')
            {
                index += 1;
            }
        }
        index = line_end;
        while (index < source.length && (source.pointer[index] == '\n' || source.pointer[index] == ';' || source.pointer[index] == '\r'))
        {
            index += 1;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_resolve_template(Arena* arena, IrProgram* program, IrFunction* function, IrInstruction* instruction,
                                                                   IrInstructionExtra extra, X64Register* registers, AssemblySyntax syntax,
                                                                   String8* source_out)
{
    String8 template_source = extra.literal;
    if (!codegen_inline_assembly_literal_registers_absent(template_source))
    {
        return false;
    }
    u64 output_length = 0;
    for (u64 index = 0; index < template_source.length;)
    {
        if (template_source.pointer[index] != '%')
        {
            if (output_length == UINT64_MAX)
            {
                return false;
            }
            output_length += 1;
            index += 1;
            continue;
        }
        if (index + 1 < template_source.length && template_source.pointer[index + 1] == '%')
        {
            output_length += 1;
            index += 2;
            continue;
        }
        u32 operand_index = 0;
        u64 end = 0;
        if (!codegen_inline_assembly_template_reference(template_source, index, extra, instruction->operand_count, &operand_index, &end))
        {
            return false;
        }
        IrValueId value = instruction->operands[operand_index];
        if (value.value >= function->value_count)
        {
            return false;
        }
        IrType* type = ir_type_from_id(&program->types, function->values[value.value].canonical_type);
        u32 type_class = codegen_inline_assembly_type_class(type);
        String8 register_name = codegen_x64_asm_register_name(registers[operand_index], type_class == IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID
                                                                                           ? 0
                                                                                           : (u32)type->layout.size);
        if (!register_name.length || (syntax != ASSEMBLY_SYNTAX_ATT && syntax != ASSEMBLY_SYNTAX_INTEL))
        {
            return false;
        }
        if (register_name.length > UINT64_MAX - output_length - (syntax == ASSEMBLY_SYNTAX_ATT ? 1 : 0))
        {
            return false;
        }
        output_length += register_name.length + (syntax == ASSEMBLY_SYNTAX_ATT ? 1 : 0);
        index = end;
    }
    char8* output = arena_allocate(arena, char8, output_length ? output_length : 1);
    if (!output)
    {
        return false;
    }
    u64 output_index = 0;
    for (u64 index = 0; index < template_source.length;)
    {
        if (template_source.pointer[index] != '%')
        {
            output[output_index++] = template_source.pointer[index++];
            continue;
        }
        if (index + 1 < template_source.length && template_source.pointer[index + 1] == '%')
        {
            output[output_index++] = '%';
            index += 2;
            continue;
        }
        u32 operand_index = 0;
        u64 end = 0;
        if (!codegen_inline_assembly_template_reference(template_source, index, extra, instruction->operand_count, &operand_index, &end))
        {
            return false;
        }
        IrValueId value = instruction->operands[operand_index];
        IrType* type = ir_type_from_id(&program->types, function->values[value.value].canonical_type);
        String8 register_name = codegen_x64_asm_register_name(registers[operand_index], (u32)type->layout.size);
        if (syntax == ASSEMBLY_SYNTAX_ATT)
        {
            output[output_index++] = '%';
        }
        memcpy(output + output_index, register_name.pointer, register_name.length);
        output_index += register_name.length;
        index = end;
    }
    *source_out = (String8){.pointer = output, .length = output_index};
    return codegen_inline_assembly_register_only_source(*source_out);
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_clobber_is_rbx(String8 clobber)
{
    return string_equal(clobber, S8("rbx")) || string_equal(clobber, S8("ebx")) || string_equal(clobber, S8("bx")) || string_equal(clobber, S8("bl"));
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_clobber_register(String8 clobber, X64Register* register_out)
{
    if (string_equal(clobber, S8("rax")) || string_equal(clobber, S8("eax")) || string_equal(clobber, S8("ax")) || string_equal(clobber, S8("al")))
    {
        *register_out = X64_REGISTER_RAX;
        return true;
    }
    if (codegen_inline_assembly_clobber_is_rbx(clobber))
    {
        *register_out = X64_REGISTER_RBX;
        return true;
    }
    if (string_equal(clobber, S8("rcx")) || string_equal(clobber, S8("ecx")) || string_equal(clobber, S8("cx")) || string_equal(clobber, S8("cl")))
    {
        *register_out = X64_REGISTER_RCX;
        return true;
    }
    if (string_equal(clobber, S8("rdx")) || string_equal(clobber, S8("edx")) || string_equal(clobber, S8("dx")) || string_equal(clobber, S8("dl")))
    {
        *register_out = X64_REGISTER_RDX;
        return true;
    }
    if (string_equal(clobber, S8("rsi")) || string_equal(clobber, S8("esi")) || string_equal(clobber, S8("si")) || string_equal(clobber, S8("sil")))
    {
        *register_out = X64_REGISTER_RSI;
        return true;
    }
    if (string_equal(clobber, S8("rdi")) || string_equal(clobber, S8("edi")) || string_equal(clobber, S8("di")) || string_equal(clobber, S8("dil")))
    {
        *register_out = X64_REGISTER_RDI;
        return true;
    }
    if (clobber.length >= 2 && clobber.pointer[0] == 'r')
    {
        u64 number = 0;
        String8 suffix = {
            .pointer = clobber.pointer + 1,
            .length = clobber.length - 1,
        };
        if (codegen_decimal_number(suffix, &number) && number >= 8 && number <= 11)
        {
            *register_out = (X64Register)number;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_constraint_register(u64 constraint, X64Register* register_out)
{
    switch (constraint & 0xff)
    {
    case IR_INLINE_ASSEMBLY_CONSTRAINT_A:
        *register_out = X64_REGISTER_RAX;
        return true;
    case IR_INLINE_ASSEMBLY_CONSTRAINT_B:
        *register_out = X64_REGISTER_RBX;
        return true;
    case IR_INLINE_ASSEMBLY_CONSTRAINT_C:
        *register_out = X64_REGISTER_RCX;
        return true;
    case IR_INLINE_ASSEMBLY_CONSTRAINT_D:
        *register_out = X64_REGISTER_RDX;
        return true;
    case IR_INLINE_ASSEMBLY_CONSTRAINT_R:
    case IR_INLINE_ASSEMBLY_CONSTRAINT_COUNT:
        break;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL u32 codegen_inline_assembly_type_class(IrType* type)
{
    if (!type || !type->layout.resolved || !type->layout.size || type->layout.size > 8)
    {
        return IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID;
    }
    switch (type->kind)
    {
    case IR_TYPE_BOOLEAN:
    case IR_TYPE_INTEGER:
    case IR_TYPE_ENUM:
        return IR_INLINE_ASSEMBLY_OPERAND_CLASS_INTEGER;
    case IR_TYPE_POINTER:
        return IR_INLINE_ASSEMBLY_OPERAND_CLASS_POINTER;
    case IR_TYPE_VOID:
    case IR_TYPE_FLOAT:
    case IR_TYPE_VA_LIST:
    case IR_TYPE_SLICE:
    case IR_TYPE_ARRAY:
    case IR_TYPE_VECTOR:
    case IR_TYPE_FUNCTION:
    case IR_TYPE_RANGE:
    case IR_TYPE_STRUCT:
    case IR_TYPE_UNION:
    case IR_TYPE_COUNT:
        break;
    }
    return IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID;
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_types_compatible(IrType* output, IrType* input)
{
    u32 output_class = codegen_inline_assembly_type_class(output);
    u32 input_class = codegen_inline_assembly_type_class(input);
    return output_class != IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID && output_class == input_class && output->layout.size == input->layout.size;
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_constraint_shape_valid(u64 constraint, u32 operand_index, u32 operand_count, u32* match_index_out)
{
    if ((constraint & ~IR_INLINE_ASSEMBLY_CONSTRAINT_KNOWN_MASK) ||
        (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) >= IR_INLINE_ASSEMBLY_CONSTRAINT_COUNT)
    {
        return false;
    }
    bool output = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) != 0;
    bool read_write = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) != 0;
    bool matching = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0;
    if ((read_write && !output) || (matching && (output || read_write)))
    {
        return false;
    }
    if (!matching)
    {
        return (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX_MASK) == 0;
    }
    u32 match_index = IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(constraint);
    if (match_index >= operand_index || match_index >= operand_count)
    {
        return false;
    }
    if (match_index_out)
    {
        *match_index_out = match_index;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_function_shape(IrFunction* function, bool* saves_rbx)
{
    *saves_rbx = false;
    if (!function)
    {
        return true;
    }
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        if (instruction->opcode == IR_OPCODE_ATOMIC_LOAD || instruction->opcode == IR_OPCODE_ATOMIC_STORE ||
            instruction->opcode == IR_OPCODE_ATOMIC_READ_MODIFY_WRITE || instruction->opcode == IR_OPCODE_ATOMIC_COMPARE_EXCHANGE)
        {
            *saves_rbx = true;
            return true;
        }
        if (instruction->opcode != IR_OPCODE_INLINE_ASSEMBLY)
        {
            continue;
        }
        IrInstructionExtra extra = ir_instruction_extra(function, ir_instruction_self_id(function, instruction));
        if ((instruction->operand_count && !instruction->immediates) || (extra.clobber_count && !extra.clobbers))
        {
            return false;
        }
        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
        {
            if ((instruction->immediates[operand_index] & 0xff) == IR_INLINE_ASSEMBLY_CONSTRAINT_B)
            {
                *saves_rbx = true;
                return true;
            }
        }
        for (u32 clobber_index = 0; clobber_index < extra.clobber_count; clobber_index += 1)
        {
            if (codegen_inline_assembly_clobber_is_rbx(extra.clobbers[clobber_index]))
            {
                *saves_rbx = true;
                return true;
            }
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_asm_memory_width(u32 width)
{
    return width == 1 || width == 2 || width == 4 || width == 8;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_a64_asm_memory_width(u32 width)
{
    return width == 1 || width == 2 || width == 4 || width == 8;
}

BUSTER_GLOBAL_LOCAL u32 codegen_canonical_a64_nop_count(String8 literal)
{
    u32 count = 0;
    u64 offset = 0;
    while (offset < literal.length)
    {
        while (offset < literal.length && (literal.pointer[offset] == ' ' || literal.pointer[offset] == '\t' || literal.pointer[offset] == '\r' ||
                                            literal.pointer[offset] == '\n'))
        {
            offset += 1;
        }
        while (offset + 1 < literal.length && literal.pointer[offset] == '\\' &&
               (literal.pointer[offset + 1] == 'n' || literal.pointer[offset + 1] == 'r' || literal.pointer[offset + 1] == 't'))
        {
            offset += 2;
            while (offset < literal.length && (literal.pointer[offset] == ' ' || literal.pointer[offset] == '\t' || literal.pointer[offset] == '\r' ||
                                                literal.pointer[offset] == '\n'))
            {
                offset += 1;
            }
        }
        if (offset == literal.length || literal.pointer[offset] == 0)
        {
            break;
        }
        if (offset + 3 > literal.length || literal.pointer[offset] != 'n' || literal.pointer[offset + 1] != 'o' || literal.pointer[offset + 2] != 'p')
        {
            return 0;
        }
        offset += 3;
        if (count == UINT32_MAX)
        {
            return 0;
        }
        count += 1;
    }
    return count;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand codegen_canonical_x64_metadata_gpr(X64Register register_index, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
        .width = width,
        .reg = {
            .index = (u16)register_index,
            .width = width,
            .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
        },
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand codegen_canonical_x64_metadata_vector(u32 register_index, u16 width)
{
    u8 physical_class = width <= 128 ? BUSTER_X86_METADATA_PHYSICAL_CLASS_XMM
                                     : width == 256 ? BUSTER_X86_METADATA_PHYSICAL_CLASS_YMM
                                                    : BUSTER_X86_METADATA_PHYSICAL_CLASS_ZMM;
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
        .width = width,
        .reg = {
            .index = (u16)register_index,
            .width = width,
            .physical_class = physical_class,
        },
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand codegen_canonical_x64_metadata_immediate(s64 value, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE,
        .width = width,
        .value = value,
        .has_value = true,
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand codegen_canonical_x64_metadata_unsigned_immediate(u64 value, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE,
        .width = width,
        .unsigned_value = value,
        .has_unsigned_value = true,
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand codegen_canonical_x64_metadata_relative(s64 value, u16 width)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE,
        .width = width,
        .value = value,
        .has_value = true,
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand codegen_canonical_x64_metadata_memory(X64Register base, u16 width, s64 displacement)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
        .width = width,
        .memory = {
            .base = {
                .index = (u16)base,
                .width = 64,
                .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR,
            },
            .displacement = displacement,
            .address_size = 64,
            .scale = 1,
            .has_base = true,
            // Keep the frame/reference displacement explicit.  In
            // particular, [rbp] without a displacement is not the same
            // ModRM shape as [rbp+0], because rm=5 is RIP-relative in
            // address modes that omit a displacement.
            .has_displacement = true,
        },
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand codegen_canonical_x64_metadata_memory_relaxed(X64Register base, u16 width, s64 displacement)
{
    BusterX86MetadataPhysicalOperand result = codegen_canonical_x64_metadata_memory(base, width, displacement);
    result.memory.has_displacement = displacement != 0;
    return result;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand codegen_canonical_x64_metadata_segment_memory(
    BusterX86MetadataPhysicalSegment segment, u16 width, s64 displacement)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
        .width = width,
        .memory = {
            .address_size = 64,
            .scale = 1,
            .segment = (u8)segment,
            .displacement = displacement,
            .has_displacement = true,
            .has_segment = true,
        },
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand codegen_canonical_x64_metadata_rip_relative(u16 width, s64 displacement)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY,
        .width = width,
        .memory = {
            .address_size = 64,
            .scale = 1,
            .displacement = displacement,
            .has_displacement = true,
            .rip_relative = true,
        },
    };
}

#define CODEGEN_X64_METADATA_CACHE_CAPACITY 512u
// The architectural maximum x86-64 instruction length.
#define CODEGEN_X64_TEMPLATE_BYTE_CAPACITY 15u
typedef struct CodegenX64MetadataCacheEntry CodegenX64MetadataCacheEntry;
struct CodegenX64MetadataCacheEntry
{
    u64 signature;
    u64 guard;
    // The durable form key, kept whole.  Re-emitting through the key rather
    // than the form id alone lets the metadata module verify the row's
    // identity directly instead of re-deriving the mnemonic's candidate list,
    // which is a normalize-and-binary-search on every emitted instruction.
    u64 stable_hash;
    u32 form_id;
    // Emitted bytes for a row whose byte string depends on no operand value.
    // The metadata transform reports that as value_field_count == 0, and the
    // key above covers the whole operand shape - mnemonic, operand kinds,
    // register numbers, memory topology, the displacement and immediate size
    // classes, attributes and target features - so for such a row the bytes
    // are a pure function of the key and a later hit can copy them instead of
    // running the transform again.  Zero length means no template, which is
    // the case for every row that writes a displacement, immediate, relative
    // or absolute field from an operand.
    u8 template_length;
    u8 template_bytes[CODEGEN_X64_TEMPLATE_BYTE_CAPACITY];
};
// A second table keyed by the operand *values* as well as their shape.  The
// table above answers "which form is this", which the values do not change, so
// it stays value-free and keeps its very high hit rate.  This one answers
// "what are the bytes", which the values do change: with them in the key the
// byte string is fully determined, so a hit is a copy with nothing to patch
// and no restriction to value-free rows.  A miss simply falls through to the
// form table, so this can never make emission worse than not having it.
//
// Sized from a census of one stage-1 self-compile: the value-free table hits
// 99.46% of 4.86 M emissions but only 16.5% of them are value-free and so
// templatable, while a value-inclusive key matches 60.9%.
// The table is sized from the module rather than fixed, because its useful
// size is the number of distinct instruction spellings the module emits and
// that scales with the module.  A fixed constant either starves a large
// translation unit or makes a ten-function one pay for a large one, and the
// operand values are part of this key, so the distinct-key count is large by
// construction - every stack slot at a different displacement is its own key.
// Stage-1 instructions therefore keep falling with capacity.
//
// The ceiling is chosen on peak resident memory, which is what constrains the
// host.  Only the slots a key actually lands on are ever touched, and the
// probe window below bounds that, so a larger table costs address space
// rather than pages until it outgrows the codegen arena's own high-water
// mark.  Measured on the self-host unit, single-lane, with peak RSS sampled
// from `VmHWM`:
//
//   entries   per lane   stage-1 instructions   peak RSS
//     65536      2 MiB       17,728,627,486     1,338,940 kB
//    262144      8 MiB       15,900,614,196     1,338,848 kB
//    524288     16 MiB       15,568,244,627     1,393,996 kB
//
// 262144 is the largest capacity that is free in resident memory; 524288 buys
// a further 2.1% of instructions for 55 MB and was not taken.  The earlier
// 65536 ceiling was chosen against a 64-slot probe that never replaced an
// entry, where a larger table helped mainly by staying unsaturated; see the
// probe window below for why that trade-off no longer holds.
#define CODEGEN_X64_TEMPLATE_CACHE_MINIMUM 4096u
#define CODEGEN_X64_TEMPLATE_CACHE_MAXIMUM 262144u
typedef struct CodegenX64TemplateCacheEntry CodegenX64TemplateCacheEntry;
struct CodegenX64TemplateCacheEntry
{
    u64 signature;
    u64 guard;
    u8 length;
    u8 bytes[CODEGEN_X64_TEMPLATE_BYTE_CAPACITY];
};
BUSTER_CT_CHECK((CODEGEN_X64_TEMPLATE_CACHE_MINIMUM & (CODEGEN_X64_TEMPLATE_CACHE_MINIMUM - 1u)) == 0);
BUSTER_CT_CHECK((CODEGEN_X64_TEMPLATE_CACHE_MAXIMUM & (CODEGEN_X64_TEMPLATE_CACHE_MAXIMUM - 1u)) == 0);

// Entries per function, from the same measurement: the self-host unit's 3,517
// functions reach the ceiling, and smaller units scale down from there so a
// ten-function translation unit does not pay for a large one.  Unchanged when
// the ceiling moved to 262144, which 3,517 functions still reach.
#define CODEGEN_X64_TEMPLATE_ENTRIES_PER_FUNCTION 64u

BUSTER_GLOBAL_LOCAL u32 codegen_canonical_x64_template_capacity(u64 function_count)
{
    u64 wanted = function_count * CODEGEN_X64_TEMPLATE_ENTRIES_PER_FUNCTION;
    u32 capacity = CODEGEN_X64_TEMPLATE_CACHE_MINIMUM;
    while (capacity < CODEGEN_X64_TEMPLATE_CACHE_MAXIMUM && capacity < wanted)
    {
        capacity *= 2u;
    }
    return capacity;
}

typedef struct CodegenX64MetadataCache CodegenX64MetadataCache;
struct CodegenX64MetadataCache
{
    CodegenX64MetadataCacheEntry entries[CODEGEN_X64_METADATA_CACHE_CAPACITY];
    // Borrowed from the same allocation that holds the cache; `template_mask`
    // is capacity - 1 and capacity is always a power of two.
    CodegenX64TemplateCacheEntry* templates;
    u32 template_mask;
};
BUSTER_CT_CHECK((CODEGEN_X64_METADATA_CACHE_CAPACITY & (CODEGEN_X64_METADATA_CACHE_CAPACITY - 1u)) == 0);

// The cache key is built once as packed 64-bit words and then hashed word at a
// time, rather than folding the query's raw struct bytes twice.  Two things
// follow from that.  The hash loop is an order of magnitude shorter, which
// matters because it runs on every emitted instruction and its multiply chain
// is latency-bound; and the key names its fields explicitly, so structure
// padding never reaches the hash.
#define CODEGEN_X64_METADATA_KEY_WORD_CAPACITY 80u

BUSTER_GLOBAL_LOCAL u64 codegen_canonical_x64_metadata_hash_bytes(u64 hash, void const* pointer, u64 length)
{
    u8 const* bytes = (u8 const*)pointer;
    for (u64 index = 0; index < length; index += 1)
    {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

BUSTER_GLOBAL_LOCAL u64 codegen_canonical_x64_metadata_register_word(BusterX86MetadataPhysicalRegister reg)
{
    return (u64)reg.index | ((u64)reg.width << 16) | ((u64)reg.physical_class << 32) | ((u64)reg.high_byte << 40);
}

BUSTER_GLOBAL_LOCAL u8 codegen_canonical_x64_metadata_immediate_class(BusterX86MetadataPhysicalOperand operand)
{
    if (operand.has_unsigned_value)
    {
        if (operand.unsigned_value == 1) return 1;
        if (operand.unsigned_value <= INT8_MAX) return 2;
        if (operand.unsigned_value <= UINT8_MAX) return 3;
        if (operand.unsigned_value <= INT32_MAX) return 4;
        if (operand.unsigned_value <= UINT32_MAX) return 5;
        return 6;
    }
    if (!operand.has_value) return 0;
    if (operand.value == 1) return 7;
    if (operand.value >= INT8_MIN && operand.value <= INT8_MAX) return 8;
    if (operand.value >= INT32_MIN && operand.value <= INT32_MAX) return 9;
    return 10;
}

BUSTER_GLOBAL_LOCAL u8 codegen_canonical_x64_metadata_displacement_class(BusterX86MetadataPhysicalMemory memory)
{
    if (!memory.has_displacement) return 0;
    if (memory.displacement == 0) return 1;
    if (memory.displacement >= INT8_MIN && memory.displacement <= INT8_MAX) return 2;
    if (memory.displacement >= INT32_MIN && memory.displacement <= INT32_MAX) return 3;
    return 4;
}

// Build the packed key.  Returns the word count, or 0 when the query does not
// fit, in which case the caller emits without consulting the cache rather than
// risking a key that does not separate two different queries.
BUSTER_GLOBAL_LOCAL u32 codegen_canonical_x64_metadata_query_key(BusterX86MetadataPhysicalQuery physical, u64* words)
{
    u32 count = 0;
    // Mnemonics and feature names are short spellings and are the only
    // variable-length input, so they keep a byte fold; everything else is
    // already a small enumerated field.
    words[count++] = codegen_canonical_x64_metadata_hash_bytes(
        UINT64_C(1469598103934665603) ^ physical.mnemonic.length, physical.mnemonic.pointer, physical.mnemonic.length);
    words[count++] = (u64)physical.operand_count | ((u64)physical.features.count << 32);
    BusterX86MetadataPhysicalAttributes attributes = physical.attributes;
    words[count++] = (u64)attributes.decorator_flags | ((u64)attributes.apx_flags << 16) |
                     ((u64)attributes.amx_flags << 32) | ((u64)attributes.mask_register << 48) |
                     ((u64)attributes.broadcast_elements << 56);
    words[count++] = (u64)attributes.rounding_mode | ((u64)attributes.has_mask_register << 8) |
                     ((u64)attributes.zeroing << 9) | ((u64)attributes.sae << 10) | ((u64)attributes.no_flags << 11) |
                     ((u64)attributes.lock << 12) | ((u64)attributes.rep << 13) | ((u64)attributes.repne << 14) |
                     ((u64)attributes.implicit_segment << 16) | ((u64)attributes.branch_hint << 24) |
                     ((u64)attributes.notrack << 32) | ((u64)attributes.dfv << 40) | ((u64)attributes.has_dfv << 48);
    for (u32 feature_index = 0; feature_index < physical.features.count; feature_index += 1)
    {
        if (count >= CODEGEN_X64_METADATA_KEY_WORD_CAPACITY) return 0;
        String8 feature = physical.features.names[feature_index];
        words[count++] = codegen_canonical_x64_metadata_hash_bytes(UINT64_C(1469598103934665603) ^ feature.length,
                                                                    feature.pointer, feature.length);
    }
    for (u32 operand_index = 0; operand_index < physical.operand_count; operand_index += 1)
    {
        if (count + 4u > CODEGEN_X64_METADATA_KEY_WORD_CAPACITY) return 0;
        BusterX86MetadataPhysicalOperand operand = physical.operands[operand_index];
        words[count++] = (u64)operand.kind | ((u64)operand.width << 32);
        if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER)
        {
            words[count++] = codegen_canonical_x64_metadata_register_word(operand.reg);
        }
        else if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY)
        {
            words[count++] = codegen_canonical_x64_metadata_register_word(operand.memory.base);
            words[count++] = codegen_canonical_x64_metadata_register_word(operand.memory.index);
            words[count++] = (u64)operand.memory.address_size | ((u64)operand.memory.scale << 8) |
                             ((u64)operand.memory.segment << 16) | ((u64)operand.memory.has_base << 24) |
                             ((u64)operand.memory.has_index << 25) | ((u64)operand.memory.has_displacement << 26) |
                             ((u64)operand.memory.rip_relative << 27) | ((u64)operand.memory.has_symbol << 28) |
                             ((u64)operand.memory.has_segment << 29) | ((u64)operand.memory.vsib << 30) |
                             ((u64)operand.memory.source_width << 32) |
                             ((u64)codegen_canonical_x64_metadata_displacement_class(operand.memory) << 48);
        }
        else if (operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_IMMEDIATE ||
                 operand.kind == BUSTER_X86_METADATA_PHYSICAL_OPERAND_RELATIVE)
        {
            words[count++] = codegen_canonical_x64_metadata_immediate_class(operand);
        }
    }
    return count;
}

// Both 64-bit halves of the key come from one pass.  The chains use different
// seeds and different multipliers and are independent, so they issue in
// parallel instead of doubling the dependent multiply latency.
BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_metadata_key_hashes(u64 const* words, u32 count, u64* signature, u64* guard)
{
    u64 first = UINT64_C(1469598103934665603);
    u64 second = UINT64_C(0x6a09e667f3bcc909);
    for (u32 index = 0; index < count; index += 1)
    {
        u64 word = words[index];
        first = (first ^ word) * UINT64_C(1099511628211);
        second = (second ^ word) * UINT64_C(0x9e3779b97f4a7c15);
    }
    // The slot index reads the signature's low bits directly, and a word-at-a-
    // time multiply leaves the least mixing there.  Avalanche both halves so
    // neighbouring keys do not land on neighbouring slots.
    first ^= first >> 32;
    first *= UINT64_C(0xd6e8feb86659fd93);
    first ^= first >> 32;
    second ^= second >> 32;
    second *= UINT64_C(0xd6e8feb86659fd93);
    second ^= second >> 32;
    *signature = first;
    *guard = second;
}

BUSTER_GLOBAL_LOCAL CodegenX64MetadataCacheEntry* codegen_canonical_x64_metadata_cache_entry(
    CodegenX64MetadataCache* cache, u64 signature, u64 guard, bool insertion)
{
    if (!cache) return 0;
    u32 slot = (u32)signature & (CODEGEN_X64_METADATA_CACHE_CAPACITY - 1u);
    for (u32 probe = 0; probe < CODEGEN_X64_METADATA_CACHE_CAPACITY; probe += 1)
    {
        CodegenX64MetadataCacheEntry* entry = cache->entries + slot;
        if (!entry->form_id)
        {
            if (!insertion) return 0;
            entry->signature = signature;
            entry->guard = guard;
            return entry;
        }
        if (entry->signature == signature && entry->guard == guard) return entry;
        slot = (slot + 1u) & (CODEGEN_X64_METADATA_CACHE_CAPACITY - 1u);
    }
    return 0;
}

// Fold the operand values into an already-built shape key.  Every field that
// can reach the bytes without changing the shape goes in: the signed and
// unsigned immediate payloads, the memory displacement, and both addends.
// Symbolic operands are excluded at the call site because they emit a
// relocation, and a relocation-bearing row is never templated.
BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_metadata_value_hashes(u64 shape_signature, u64 shape_guard,
                                                                      BusterX86MetadataPhysicalQuery physical,
                                                                      u64* signature, u64* guard)
{
    // Start from the finished shape hashes rather than walking the key words a
    // second time: they already summarize every word, so folding the values
    // into them yields the same separation for half the work.
    u64 first = shape_signature ^ UINT64_C(0x243f6a8885a308d3);
    u64 second = shape_guard ^ UINT64_C(0x13198a2e03707344);
    for (u32 operand_index = 0; operand_index < physical.operand_count; operand_index += 1)
    {
        BusterX86MetadataPhysicalOperand operand = physical.operands[operand_index];
        u64 values[5] = {
            (u64)operand.value, operand.unsigned_value, (u64)operand.addend,
            (u64)operand.memory.displacement, (u64)operand.memory.addend,
        };
        for (u32 value_index = 0; value_index < BUSTER_ARRAY_LENGTH(values); value_index += 1)
        {
            first = (first ^ values[value_index]) * UINT64_C(1099511628211);
            second = (second ^ values[value_index]) * UINT64_C(0x9e3779b97f4a7c15);
        }
    }
    first ^= first >> 32;
    first *= UINT64_C(0xd6e8feb86659fd93);
    first ^= first >> 32;
    second ^= second >> 32;
    second *= UINT64_C(0xd6e8feb86659fd93);
    second ^= second >> 32;
    // Zero is the empty-slot marker, so keep it out of the value space.
    *signature = first ? first : 1;
    *guard = second;
}

// A symbol is the one operand payload the key cannot carry: two different
// symbol names produce the same shape and the same values, so their bytes must
// never share a template.  In practice such a row also needs a relocation and
// this path offers no relocation capacity, so it fails before reaching the
// capture - but the invariant belongs here, next to the key, rather than
// resting on that.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_query_has_symbol(BusterX86MetadataPhysicalQuery physical)
{
    for (u32 operand_index = 0; operand_index < physical.operand_count; operand_index += 1)
    {
        BusterX86MetadataPhysicalOperand operand = physical.operands[operand_index];
        if (operand.has_symbol || operand.symbol.length || operand.memory.has_symbol || operand.memory.symbol.length) return true;
    }
    return false;
}

// The byte table is a cache, not a dictionary, and is probed as one: a short
// window from the home slot, and a claimed victim inside that window when the
// window is full.  It used to probe 64 slots and give up, which cost twice
// over once the table filled.  A lookup that could not succeed still walked
// every one of the 64, and an insertion that found no empty slot did nothing,
// so a full table froze on whatever the module emitted first and never took
// another entry.  Measured on the self-host unit at capacity 65536: the table
// reached 100% occupancy, 32.8% of 4.92 M lookups were futile full walks, and
// the average lookup cost 22.15 probe steps.  Bounding the window puts a
// ceiling on the miss, and claiming a victim keeps the table tracking the
// working set instead of the module's first few thousand distinct spellings.
//
// A key therefore always lives within `home .. home + window - 1`, which is
// the invariant the lookup relies on and the insertion maintains.
//
// The window is narrow because probe steps are paid on every emission while
// the extra associativity only buys hits.  Swept on the self-host unit at the
// then-current 65536 ceiling, single-lane stage-1 instructions were 17.86 G at
// a window of 1, 17.73 G at 2, 17.74 G at 4, 17.83 G at 8 and 18.07 G at 16:
// one slot loses too many hits, and past two the walk costs more than it wins.
#define CODEGEN_X64_TEMPLATE_PROBE_WINDOW 2u
BUSTER_CT_CHECK((CODEGEN_X64_TEMPLATE_PROBE_WINDOW & (CODEGEN_X64_TEMPLATE_PROBE_WINDOW - 1u)) == 0);
BUSTER_CT_CHECK(CODEGEN_X64_TEMPLATE_PROBE_WINDOW <= CODEGEN_X64_TEMPLATE_CACHE_MINIMUM);

BUSTER_GLOBAL_LOCAL CodegenX64TemplateCacheEntry* codegen_canonical_x64_template_entry(CodegenX64MetadataCache* cache,
                                                                                        u64 signature, u64 guard, bool insertion)
{
    if (!cache) return 0;
    if (!cache->templates) return 0;
    u32 home = (u32)signature & cache->template_mask;
    u32 slot = home;
    for (u32 probe = 0; probe < CODEGEN_X64_TEMPLATE_PROBE_WINDOW; probe += 1)
    {
        CodegenX64TemplateCacheEntry* entry = cache->templates + slot;
        if (!entry->signature)
        {
            if (!insertion) return 0;
            entry->signature = signature;
            entry->guard = guard;
            entry->length = 0;
            return entry;
        }
        if (entry->signature == signature && entry->guard == guard) return entry;
        slot = (slot + 1u) & cache->template_mask;
    }
    if (!insertion) return 0;
    // Every slot in the window belongs to another key, so one of them is
    // replaced.  The victim is picked from the guard rather than fixed at the
    // home slot: the two halves of the key are independent hashes, so this
    // spreads eviction across the window instead of letting one slot absorb
    // every conflict, and it stays a pure function of the key, which the
    // byte-identical fixed point requires.
    u32 victim = (home + (u32)(guard & (CODEGEN_X64_TEMPLATE_PROBE_WINDOW - 1u))) & cache->template_mask;
    CodegenX64TemplateCacheEntry* entry = cache->templates + victim;
    entry->signature = signature;
    entry->guard = guard;
    entry->length = 0;
    return entry;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_metadata_emit_attributes(CodegenBuffer* buffer, String8 mnemonic,
                                                                         BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
                                                                         BusterX86MetadataFeatureInput features,
                                                                         BusterX86MetadataPhysicalAttributes attributes)
{
    if (!buffer)
    {
        return false;
    }
    if ((operand_count && !operands) || buffer->error || buffer->count > buffer->capacity)
    {
        if (!buffer->error)
        {
            buffer->error = buffer->count > buffer->capacity ? CODEGEN_ERROR_CAPACITY : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        }
        return false;
    }

    BusterX86MetadataPhysicalQuery physical = {
        .mnemonic = mnemonic,
        .operands = operands,
        .operand_count = operand_count,
        .features = features,
        .attributes = attributes,
        .address_size = 64,
        .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
        .include_privileged = false,
        .include_not64 = false,
        .include_implicit = false,
        .source_semantics = false,
    };
    u64 remaining = buffer->capacity - buffer->count;
    u32 output_capacity = remaining > UINT32_MAX ? UINT32_MAX : (u32)remaining;
    CodegenX64MetadataCache* cache = (CodegenX64MetadataCache*)buffer->x64_metadata_cache;
    // One key build serves both tables: the shape words alone key the form
    // table, and the same words plus the operand values key the byte table.
    u64 words[CODEGEN_X64_METADATA_KEY_WORD_CAPACITY];
    u32 word_count = cache ? codegen_canonical_x64_metadata_query_key(physical, words) : 0;
    u64 signature = 0;
    u64 guard = 0;
    if (word_count) codegen_canonical_x64_metadata_key_hashes(words, word_count, &signature, &guard);
    CodegenX64MetadataCacheEntry* cached = word_count ? codegen_canonical_x64_metadata_cache_entry(cache, signature, guard, false) : 0;
    BusterX86MetadataEmitResult emitted = {0};
    // A value-free row's bytes are already pinned by the shape alone, so its
    // template hangs off the form entry and never needs the value key.
    if (cached && cached->template_length)
    {
        if (output_capacity < cached->template_length)
        {
            codegen_buffer_report_exhausted(buffer);
            return false;
        }
        if (buffer->bytes) memcpy(buffer->bytes + buffer->count, cached->template_bytes, cached->template_length);
        buffer->count += cached->template_length;
        return true;
    }
    // Otherwise the values decide the bytes, so consult the value-keyed table.
    u64 value_signature = 0;
    u64 value_guard = 0;
    CodegenX64TemplateCacheEntry* templated = 0;
    if (word_count)
    {
        codegen_canonical_x64_metadata_value_hashes(signature, guard, physical, &value_signature, &value_guard);
        templated = codegen_canonical_x64_template_entry(cache, value_signature, value_guard, false);
        if (templated && templated->length)
        {
            if (output_capacity < templated->length)
            {
                codegen_buffer_report_exhausted(buffer);
                return false;
            }
            if (buffer->bytes) memcpy(buffer->bytes + buffer->count, templated->bytes, templated->length);
            buffer->count += templated->length;
            return true;
        }
    }
    if (cached)
    {
        emitted = buster_x86_metadata_emit_form_selected(
            (BusterX86MetadataEmitQuery){
                .physical = physical,
                .form_id = cached->form_id - 1u,
                .output = buffer->bytes ? buffer->bytes + buffer->count : 0,
                .output_capacity = output_capacity,
                .relocations = 0,
                .relocation_capacity = 0,
            },
            (BusterX86MetadataFormKey){.form_id = cached->form_id - 1u, .stable_hash = cached->stable_hash});
    }
    if (!cached || emitted.status != BUSTER_X86_METADATA_ENCODE_SUCCESS)
    {
        emitted = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
        .physical = physical,
        .output = buffer->bytes ? buffer->bytes + buffer->count : 0,
        .output_capacity = output_capacity,
        .relocations = 0,
        .relocation_capacity = 0,
        });
        // Only a complete durable key is worth caching: the hit path re-emits
        // through it, and a row without a stable hash cannot be identified.
        if (emitted.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && emitted.form_id != UINT32_MAX && emitted.stable_hash)
        {
            CodegenX64MetadataCacheEntry* insertion =
                word_count ? codegen_canonical_x64_metadata_cache_entry(cache, signature, guard, true) : 0;
            if (insertion && !insertion->form_id)
            {
                insertion->stable_hash = emitted.stable_hash;
                insertion->form_id = emitted.form_id + 1u;
            }
            cached = insertion;
        }
    }
    if (emitted.status != BUSTER_X86_METADATA_ENCODE_SUCCESS || emitted.relocation_count != 0 ||
        emitted.byte_count > output_capacity)
    {
        if (emitted.status == BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY || emitted.byte_count > output_capacity)
        {
            codegen_buffer_report_exhausted(buffer);
        }
        else
        {
            buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        }
        return false;
    }
    // Retain the bytes.  A value-free row goes on the form entry, where the
    // shape alone pins it and nothing can evict it.  Every other
    // relocation-free row goes in the value-keyed table, where the values in
    // the key pin it just as firmly.  Both run on the miss that filled the
    // entry and on any later untemplated hit, so a first pass with no output
    // buffer does not lose the chance.
    if (buffer->bytes && !emitted.relocation_count && emitted.byte_count &&
        emitted.byte_count <= CODEGEN_X64_TEMPLATE_BYTE_CAPACITY)
    {
        if (cached && !cached->template_length && !emitted.value_field_count)
        {
            memcpy(cached->template_bytes, buffer->bytes + buffer->count, emitted.byte_count);
            cached->template_length = (u8)emitted.byte_count;
        }
        else if (word_count && emitted.value_field_count && !codegen_canonical_x64_query_has_symbol(physical))
        {
            if (!templated) templated = codegen_canonical_x64_template_entry(cache, value_signature, value_guard, true);
            if (templated && !templated->length)
            {
                memcpy(templated->bytes, buffer->bytes + buffer->count, emitted.byte_count);
                templated->length = (u8)emitted.byte_count;
            }
        }
    }
    buffer->count += emitted.byte_count;
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_metadata_emit_features(CodegenBuffer* buffer, String8 mnemonic,
                                                                       BusterX86MetadataPhysicalOperand const* operands, u32 operand_count,
                                                                       BusterX86MetadataFeatureInput features)
{
    return codegen_canonical_x64_metadata_emit_attributes(buffer, mnemonic, operands, operand_count, features,
                                                           (BusterX86MetadataPhysicalAttributes){0});
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_metadata_emit(CodegenBuffer* buffer, String8 mnemonic,
                                                              BusterX86MetadataPhysicalOperand const* operands, u32 operand_count)
{
    return codegen_canonical_x64_metadata_emit_features(buffer, mnemonic, operands, operand_count, (BusterX86MetadataFeatureInput){0});
}

// Emit one symbol-bearing instruction through the checked metadata bridge.
// Canonical codegen keeps module relocations in its own format, while the
// metadata encoder owns the instruction shape and the exact displacement
// field.  Give the physical query a private non-empty symbol solely to force
// metadata to materialize its relocation record; callers translate the
// returned field offset into their CodegenModuleRelocation entry.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_metadata_emit_relocation(CodegenBuffer* buffer, String8 mnemonic,
                                                                         BusterX86MetadataPhysicalOperand const* operands,
                                                                         u32 operand_count, u32* relocation_offset)
{
    if (!buffer || (operand_count && !operands) || buffer->error || buffer->count > buffer->capacity || operand_count > 16)
    {
        if (buffer && !buffer->error)
        {
            buffer->error = buffer->count > buffer->capacity ? CODEGEN_ERROR_CAPACITY : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        }
        return false;
    }
    BusterX86MetadataPhysicalOperand relocated_operands[16] = {0};
    if (operand_count)
    {
        memcpy(relocated_operands, operands, operand_count * sizeof(*operands));
    }
    String8 relocation_symbol = S8("__buster_x86_codegen_relocation");
    bool symbolized = false;
    for (u32 operand_index = 0; operand_index < operand_count; operand_index += 1)
    {
        BusterX86MetadataPhysicalOperand* operand = relocated_operands + operand_index;
        if (operand->kind != BUSTER_X86_METADATA_PHYSICAL_OPERAND_MEMORY || operand->memory.has_symbol)
        {
            continue;
        }
        operand->memory.symbol = relocation_symbol;
        operand->memory.has_symbol = true;
        symbolized = true;
    }
    if (!symbolized)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return false;
    }
    BusterX86MetadataPhysicalQuery physical = {
        .mnemonic = mnemonic,
        .operands = relocated_operands,
        .operand_count = operand_count,
        .address_size = 64,
        .execution_mode = BUSTER_X86_METADATA_EXECUTION_MODE_64,
    };
    u64 remaining = buffer->capacity - buffer->count;
    u32 output_capacity = remaining > UINT32_MAX ? UINT32_MAX : (u32)remaining;
    BusterX86MetadataRelocation metadata_relocations[BUSTER_X86_METADATA_EMIT_RELOCATION_CAPACITY] = {0};
    BusterX86MetadataEmitResult emitted = buster_x86_metadata_encode((BusterX86MetadataEncodeQuery){
        .physical = physical,
        .output = buffer->bytes ? buffer->bytes + buffer->count : 0,
        .output_capacity = output_capacity,
        .relocations = metadata_relocations,
        .relocation_capacity = BUSTER_X86_METADATA_EMIT_RELOCATION_CAPACITY,
    });
    if (emitted.status != BUSTER_X86_METADATA_ENCODE_SUCCESS || emitted.relocation_count != 1 || emitted.byte_count > output_capacity ||
        metadata_relocations[0].width != 4)
    {
        if (emitted.status == BUSTER_X86_METADATA_ENCODE_OUTPUT_CAPACITY || emitted.byte_count > output_capacity)
        {
            codegen_buffer_report_exhausted(buffer);
        }
        else
        {
            buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        }
        return false;
    }
    u32 instruction_offset = (u32)buffer->count;
    buffer->count += emitted.byte_count;
    if (relocation_offset)
    {
        *relocation_offset = instruction_offset + metadata_relocations[0].offset;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_asm_load(CodegenBuffer* buffer, X64Register target, X64Register base, u32 displacement, u32 width)
{
    String8 mnemonic = width == 1 || width == 2 ? S8("MOVZX") : S8("MOV");
    u16 memory_width = (u16)(width * 8);
    u16 register_width = width <= 2 ? 32 : memory_width;
    BusterX86MetadataPhysicalOperand operands[2] = {
        codegen_canonical_x64_metadata_gpr(target, register_width),
        codegen_canonical_x64_metadata_memory(base, memory_width, (s64)(s32)displacement),
    };
    (void)codegen_canonical_x64_metadata_emit(buffer, mnemonic, operands, BUSTER_ARRAY_LENGTH(operands));
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_asm_store(CodegenBuffer* buffer, X64Register base, X64Register source, u32 displacement, u32 width)
{
    u16 memory_width = (u16)(width * 8);
    BusterX86MetadataPhysicalOperand operands[2] = {
        codegen_canonical_x64_metadata_memory(base, memory_width, (s64)(s32)displacement),
        codegen_canonical_x64_metadata_gpr(source, memory_width),
    };
    (void)codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), operands, BUSTER_ARRAY_LENGTH(operands));
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_metadata_atomic_register_memory(CodegenBuffer* buffer, String8 mnemonic,
                                                                                 X64Register base, X64Register source, u16 width)
{
    BusterX86MetadataPhysicalOperand operands[2] = {
        codegen_canonical_x64_metadata_memory_relaxed(base, width, 0),
        codegen_canonical_x64_metadata_gpr(source, width),
    };
    return codegen_canonical_x64_metadata_emit_attributes(buffer, mnemonic, operands, BUSTER_ARRAY_LENGTH(operands),
                                                          (BusterX86MetadataFeatureInput){0},
                                                          (BusterX86MetadataPhysicalAttributes){.lock = true});
}


BUSTER_GLOBAL_LOCAL bool codegen_unwind_action_append(CodegenFunctionDescriptor* descriptor, u32 capacity, u32 code_offset,
                                                      CodegenUnwindActionKind kind, u8 register_index, u32 value)
{
    if (descriptor->unwind_action_count >= capacity)
    {
        return false;
    }
    descriptor->unwind_actions[descriptor->unwind_action_count++] = (CodegenUnwindAction){
        .code_offset = code_offset,
        .value = value,
        .kind = kind,
        .register_index = register_index,
    };
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_epilog_offset_append(CodegenFunctionDescriptor* descriptor, u32 capacity, u32 code_offset)
{
    if (!descriptor || !descriptor->epilog_offsets || descriptor->epilog_count >= capacity)
    {
        return false;
    }
    descriptor->epilog_offsets[descriptor->epilog_count++] = code_offset;
    return true;
}

String8 codegen_register_allocator_mode_string(CodegenRegisterAllocatorMode mode)
{
    switch (mode)
    {
        break;
    case CODEGEN_REGISTER_ALLOCATOR_NONE:
        return S8("none");
        break;
    case CODEGEN_REGISTER_ALLOCATOR_MIR_STACK:
        return S8("mir-stack");
        break;
    case CODEGEN_REGISTER_ALLOCATOR_FAST:
        return S8("fast");
        break;
    case CODEGEN_REGISTER_ALLOCATOR_QUALITY:
        return S8("quality");
        break;
    case CODEGEN_REGISTER_ALLOCATOR_MODE_COUNT:
        break;
    }
    return S8("invalid");
}

CodegenAbi codegen_abi_for_target(Target target)
{
    switch (target.cpu_arch)
    {
        break;
    case CPU_ARCH_X86_64:
    {
        switch (target.os)
        {
            break;
        case OPERATING_SYSTEM_WINDOWS:
            return CODEGEN_ABI_X86_64_WINDOWS;
            break;
        case OPERATING_SYSTEM_UEFI:
            return CODEGEN_ABI_X86_64_WINDOWS;
            break;
        case OPERATING_SYSTEM_LINUX:
            return CODEGEN_ABI_X86_64_SYSTEM_V;
            break;
        case OPERATING_SYSTEM_MACOS:
            return CODEGEN_ABI_X86_64_SYSTEM_V;
            break;
        case OPERATING_SYSTEM_ANDROID:
            return CODEGEN_ABI_X86_64_SYSTEM_V;
            break;
        case OPERATING_SYSTEM_IOS:
            return CODEGEN_ABI_X86_64_SYSTEM_V;
            break;
        case OPERATING_SYSTEM_FREESTANDING:
            return CODEGEN_ABI_X86_64_SYSTEM_V;
            break;
        case OPERATING_SYSTEM_COUNT:
            return CODEGEN_ABI_COUNT;
        }
    }
    break;
        break;
    case CPU_ARCH_AARCH64:
    {
        switch (target.os)
        {
            break;
        case OPERATING_SYSTEM_WINDOWS:
            return CODEGEN_ABI_AARCH64_WINDOWS;
            break;
        case OPERATING_SYSTEM_MACOS:
            return CODEGEN_ABI_AARCH64_DARWIN;
            break;
        case OPERATING_SYSTEM_IOS:
            return CODEGEN_ABI_AARCH64_DARWIN;
            break;
        case OPERATING_SYSTEM_LINUX:
            return CODEGEN_ABI_AARCH64_AAPCS64;
            break;
        case OPERATING_SYSTEM_UEFI:
            return CODEGEN_ABI_AARCH64_AAPCS64;
            break;
        case OPERATING_SYSTEM_ANDROID:
            return CODEGEN_ABI_AARCH64_AAPCS64;
            break;
        case OPERATING_SYSTEM_FREESTANDING:
            return CODEGEN_ABI_AARCH64_AAPCS64;
            break;
        case OPERATING_SYSTEM_COUNT:
            return CODEGEN_ABI_COUNT;
        }
    }
    break;
        break;
    case CPU_ARCH_WASM64:
    case CPU_ARCH_BPFEL:
        // Wasm64 and eBPF are emitted directly from canonical IR and do not
        // use a native platform ABI or the native machine-code pipeline.
        return CODEGEN_ABI_COUNT;
        break;
    case CPU_ARCH_COUNT:
        return CODEGEN_ABI_COUNT;
    }
    return CODEGEN_ABI_COUNT;
}

BUSTER_GLOBAL_LOCAL Target codegen_abi_targets[CODEGEN_ABI_COUNT];
BUSTER_GLOBAL_LOCAL bool codegen_abi_targets_built;

// Called per aggregate-ABI classification, so the feature-array fold is
// cached per abi instead of re-run on every query.
Target codegen_target_for_abi(CodegenAbi abi)
{
    if (!codegen_abi_targets_built)
    {
        BUSTER_CHECK_SERIAL_INITIALIZATION();
        for (u32 abi_index = 0; abi_index < CODEGEN_ABI_COUNT; abi_index += 1)
        {
            bool x86 = abi_index == CODEGEN_ABI_X86_64_SYSTEM_V || abi_index == CODEGEN_ABI_X86_64_WINDOWS;
            codegen_abi_targets[abi_index] = (Target){
                .cpu_arch = x86 ? CPU_ARCH_X86_64 : CPU_ARCH_AARCH64,
                .os = abi_index == CODEGEN_ABI_X86_64_WINDOWS    ? OPERATING_SYSTEM_WINDOWS
                      : abi_index == CODEGEN_ABI_AARCH64_DARWIN  ? OPERATING_SYSTEM_MACOS
                      : abi_index == CODEGEN_ABI_AARCH64_WINDOWS ? OPERATING_SYSTEM_WINDOWS
                                                                 : OPERATING_SYSTEM_LINUX,
                .cpu_features_explicit = true,
                .cpu_features = x86 ? target_cpu_features_from_array((TargetCpuFeature const[]){
                                              TARGET_CPU_FEATURE_X86_SSE2, TARGET_CPU_FEATURE_X86_AVX,
                                              TARGET_CPU_FEATURE_X86_AVX2, TARGET_CPU_FEATURE_X86_AVX512F,
                                              TARGET_CPU_FEATURE_X86_AVX512VL, TARGET_CPU_FEATURE_X86_AVX512BW}, 6)
                                    : target_cpu_features_from_array((TargetCpuFeature const[]){
                                          TARGET_CPU_FEATURE_AARCH64_FP_ARMV8,
                                          TARGET_CPU_FEATURE_AARCH64_NEON,
                                      }, 2),
            };
        }
        codegen_abi_targets_built = true;
    }
    // An out-of-range abi used to fall through every x86/Windows/Darwin test,
    // which is exactly the AAPCS64 row.
    return codegen_abi_targets[(u32)abi < CODEGEN_ABI_COUNT ? (u32)abi : CODEGEN_ABI_AARCH64_AAPCS64];
}

// The one codegen table built on first use; asking for any abi fills them all.
void codegen_prewarm(void)
{
    (void)codegen_target_for_abi(CODEGEN_ABI_X86_64_SYSTEM_V);
}

// x86 metadata and exact machine plans are only needed by x86 codegen.  Keep
// their preparation beside the target-aware entry points so AArch64 (and any
// other non-x86 caller) does not pay the full x86 table decode.  The machine
// encoder and ordinary x86 assembly both read these tables while emitting, so
// the complete preparation must finish before a module is generated.
void codegen_prewarm_for_target(Target target)
{
    codegen_prewarm();
    if (target.cpu_arch != CPU_ARCH_X86_64)
    {
        return;
    }
    // Exact machine emission reads these tables without ever filling them, so
    // they are initialized here rather than on first use during emission.
    buster_x86_metadata_prewarm();
    machine_x86_64_exact_prewarm();
}

// The one place code-buffer exhaustion is reported. It lives out of line
// because the scalar emitters are inlined throughout the backend and this is
// the only path none of them take: reporting it in a caller costs more per
// emitted byte, across nineteen megabytes of them, than the report is worth.
BUSTER_GLOBAL_LOCAL BUSTER_COLD BUSTER_PRESERVE_MOST void codegen_buffer_report_exhausted(CodegenBuffer* buffer)
{
    buffer->error = CODEGEN_ERROR_CAPACITY;
    if (buffer->exhausted)
    {
        *buffer->exhausted = true;
    }
}

BUSTER_GLOBAL_LOCAL BUSTER_ALWAYS_INLINE bool codegen_buffer_reserve(CodegenBuffer* buffer, u64 byte_count, u8** output)
{
    if (buffer->count > buffer->capacity || byte_count > buffer->capacity - buffer->count)
    {
        codegen_buffer_report_exhausted(buffer);
        return false;
    }
    *output = buffer->bytes + buffer->count;
    buffer->count += byte_count;
    return true;
}

BUSTER_GLOBAL_LOCAL void codegen_emit_u8(CodegenBuffer* buffer, u8 value)
{
    if (BUSTER_UNLIKELY(buffer->count >= buffer->capacity))
    {
        codegen_buffer_report_exhausted(buffer);
        return;
    }
    buffer->bytes[buffer->count++] = value;
}

BUSTER_GLOBAL_LOCAL void codegen_emit_u32(CodegenBuffer* buffer, u32 value)
{
    u8* output;
    if (!codegen_buffer_reserve(buffer, 4, &output))
    {
        return;
    }
    output[0] = (u8)value;
    output[1] = (u8)(value >> 8);
    output[2] = (u8)(value >> 16);
    output[3] = (u8)(value >> 24);
}

BUSTER_GLOBAL_LOCAL void codegen_emit_u64(CodegenBuffer* buffer, u64 value)
{
    u8* output;
    if (!codegen_buffer_reserve(buffer, 8, &output))
    {
        return;
    }
    output[0] = (u8)value;
    output[1] = (u8)(value >> 8);
    output[2] = (u8)(value >> 16);
    output[3] = (u8)(value >> 24);
    output[4] = (u8)(value >> 32);
    output[5] = (u8)(value >> 40);
    output[6] = (u8)(value >> 48);
    output[7] = (u8)(value >> 56);
}

#if BUSTER_INCLUDE_TESTS
void codegen_test_emit_scalar(CodegenBuffer* buffer, u32 byte_count, u64 value)
{
    switch (byte_count)
    {
    case 1:
        codegen_emit_u8(buffer, (u8)value);
        break;
    case 4:
        codegen_emit_u32(buffer, (u32)value);
        break;
    case 8:
        codegen_emit_u64(buffer, value);
        break;
    default:
        buffer->error = CODEGEN_ERROR_CAPACITY;
        break;
    }
}
#endif




typedef struct CodegenRegisterAllocation CodegenRegisterAllocation;
struct CodegenRegisterAllocation
{
    u8* registers;
    u32 allocated_count;
    u32 spilled_count;
};

#define CODEGEN_REGISTER_UNALLOCATED UINT8_MAX











BUSTER_GLOBAL_LOCAL void x64_emit_load_memory(X64Builder* builder, X64Register target, X64Register base, u32 offset, u32 size)
{
    String8 mnemonic = size == 1 || size == 2 ? S8("MOVZX") : S8("MOV");
    u16 memory_width = (u16)(size * 8);
    u16 register_width = size <= 2 ? 32 : memory_width;
    BusterX86MetadataPhysicalOperand operands[2] = {
        codegen_canonical_x64_metadata_gpr(target, register_width),
        codegen_canonical_x64_metadata_memory(base, memory_width, (s64)(s32)offset),
    };
    (void)codegen_canonical_x64_metadata_emit(&builder->buffer, mnemonic, operands, BUSTER_ARRAY_LENGTH(operands));
}

BUSTER_GLOBAL_LOCAL void x64_emit_store_memory(X64Builder* builder, X64Register base, u32 offset, X64Register source, u32 size)
{
    u16 memory_width = (u16)(size * 8);
    BusterX86MetadataPhysicalOperand operands[2] = {
        codegen_canonical_x64_metadata_memory(base, memory_width, (s64)(s32)offset),
        codegen_canonical_x64_metadata_gpr(source, memory_width),
    };
    (void)codegen_canonical_x64_metadata_emit(&builder->buffer, S8("MOV"), operands, BUSTER_ARRAY_LENGTH(operands));
}

BUSTER_GLOBAL_LOCAL void x64_emit_load_float_bits(X64Builder* builder, u32 target, X64Register base, u32 offset, u32 size)
{
    if (size != 4 && size != 8)
    {
        builder->buffer.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    String8 mnemonic = size == 4 ? S8("MOVSS") : S8("MOVQ");
    BusterX86MetadataPhysicalOperand operands[2] = {
        codegen_canonical_x64_metadata_vector(target, (u16)(size * 8)),
        codegen_canonical_x64_metadata_memory_relaxed(base, (u16)(size * 8), (s64)(s32)offset),
    };
    String8 feature_names[] = {S8("sse"), S8("sse2")};
    (void)codegen_canonical_x64_metadata_emit_features(&builder->buffer, mnemonic, operands, BUSTER_ARRAY_LENGTH(operands),
                                                        (BusterX86MetadataFeatureInput){.names = feature_names, .count = BUSTER_ARRAY_LENGTH(feature_names)});
}

BUSTER_GLOBAL_LOCAL void x64_emit_store_float_bits(X64Builder* builder, X64Register base, u32 offset, u32 source, u32 size)
{
    if (size != 4 && size != 8)
    {
        builder->buffer.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    String8 mnemonic = size == 4 ? S8("MOVSS") : S8("MOVQ");
    BusterX86MetadataPhysicalOperand operands[2] = {
        codegen_canonical_x64_metadata_memory_relaxed(base, (u16)(size * 8), (s64)(s32)offset),
        codegen_canonical_x64_metadata_vector(source, (u16)(size * 8)),
    };
    String8 feature_names[] = {S8("sse"), S8("sse2")};
    (void)codegen_canonical_x64_metadata_emit_features(&builder->buffer, mnemonic, operands, BUSTER_ARRAY_LENGTH(operands),
                                                        (BusterX86MetadataFeatureInput){.names = feature_names, .count = BUSTER_ARRAY_LENGTH(feature_names)});
}







BUSTER_GLOBAL_LOCAL bool x64_emit_windows_stack_allocate(CodegenBuffer* buffer, u32 size, CodegenFunctionDescriptor* descriptor, u32 action_capacity,
                                                          u32 function_offset)
{
    if (!buffer || size <= CODEGEN_X64_STACK_PROBE_PAGE)
    {
        return false;
    }
    // Probe with volatile r10/r11 while RSP still denotes the caller-visible
    // frame. The loop keeps the prolog size constant even for very large
    // frames; only the final SUB changes RSP and therefore needs a UWOP.
    BusterX86MetadataPhysicalOperand move_r10_operands[2] = {
        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R10, 64),
        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSP, 64),
    };
    BusterX86MetadataPhysicalOperand move_r11_operands[2] = {
        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R11, 64),
        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSP, 64),
    };
    BusterX86MetadataPhysicalOperand sub_r10_operands[2] = {
        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R10, 64),
        codegen_canonical_x64_metadata_immediate(size, 32),
    };
    BusterX86MetadataPhysicalOperand sub_r11_operands[2] = {
        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R11, 64),
        codegen_canonical_x64_metadata_immediate(CODEGEN_X64_STACK_PROBE_PAGE, 32),
    };
    BusterX86MetadataPhysicalOperand cmp_operands[2] = {
        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R11, 64),
        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R10, 64),
    };
    BusterX86MetadataPhysicalOperand test_r11_operands[2] = {
        codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_R11, 8, 0),
        codegen_canonical_x64_metadata_immediate(0, 8),
    };
    BusterX86MetadataPhysicalOperand test_r10_operands[2] = {
        codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_R10, 8, 0),
        codegen_canonical_x64_metadata_immediate(0, 8),
    };
    BusterX86MetadataPhysicalOperand sub_rsp_operands[2] = {
        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSP, 64),
        codegen_canonical_x64_metadata_immediate(size, 32),
    };
    if (!codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), move_r10_operands, BUSTER_ARRAY_LENGTH(move_r10_operands)) ||
        !codegen_canonical_x64_metadata_emit(buffer, S8("SUB"), sub_r10_operands, BUSTER_ARRAY_LENGTH(sub_r10_operands)) ||
        !codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), move_r11_operands, BUSTER_ARRAY_LENGTH(move_r11_operands)))
    {
        return true;
    }
    u64 loop_offset = buffer->count;
    if (!codegen_canonical_x64_metadata_emit(buffer, S8("SUB"), sub_r11_operands, BUSTER_ARRAY_LENGTH(sub_r11_operands)) ||
        !codegen_canonical_x64_metadata_emit(buffer, S8("CMP"), cmp_operands, BUSTER_ARRAY_LENGTH(cmp_operands)))
    {
        return true;
    }
    BusterX86MetadataPhysicalOperand final_branch_operand = codegen_canonical_x64_metadata_relative(0, 8);
    u64 final_patch = buffer->count;
    if (!codegen_canonical_x64_metadata_emit(buffer, S8("JBE"), &final_branch_operand, 1) ||
        !codegen_canonical_x64_metadata_emit(buffer, S8("TEST"), test_r11_operands, BUSTER_ARRAY_LENGTH(test_r11_operands)))
    {
        return true;
    }
    u64 loop_patch = buffer->count;
    BusterX86MetadataPhysicalOperand loop_branch_operand = codegen_canonical_x64_metadata_relative(0, 8);
    if (!codegen_canonical_x64_metadata_emit(buffer, S8("JMP"), &loop_branch_operand, 1))
    {
        return true;
    }
    u64 final_offset = buffer->count;
    if (!codegen_canonical_x64_metadata_emit(buffer, S8("TEST"), test_r10_operands, BUSTER_ARRAY_LENGTH(test_r10_operands)) ||
        !codegen_canonical_x64_metadata_emit(buffer, S8("SUB"), sub_rsp_operands, BUSTER_ARRAY_LENGTH(sub_rsp_operands)))
    {
        return true;
    }
    s64 final_displacement = (s64)final_offset - (s64)(final_patch + 2);
    s64 loop_displacement = (s64)loop_offset - (s64)(loop_patch + 2);
    if (buffer->error != CODEGEN_ERROR_NONE || final_displacement < INT8_MIN || final_displacement > INT8_MAX || loop_displacement < INT8_MIN ||
        loop_displacement > INT8_MAX || buffer->count - function_offset > UINT32_MAX)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return true;
    }
    if (final_patch + 2 > buffer->count || loop_patch + 2 > buffer->count)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return true;
    }
    buffer->bytes[final_patch + 1] = (u8)(s8)final_displacement;
    buffer->bytes[loop_patch + 1] = (u8)(s8)loop_displacement;
    if (descriptor && !codegen_unwind_action_append(descriptor, action_capacity, (u32)(buffer->count - function_offset),
                                                    CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, size))
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
    }
    return true;
}













// popcnt eax/rax, eax/rax. The C frontend only produces this operation when
// the target has POPCNT and expands the SWAR form itself otherwise, so there
// is no second sequence to keep in step here.
BUSTER_GLOBAL_LOCAL void x64_emit_population_count(CodegenBuffer* buffer, u32 width)
{
    if (width != 32 && width != 64)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    BusterX86MetadataPhysicalOperand operands[2] = {
        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, (u16)width),
        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, (u16)width),
    };
    String8 feature_names[] = {S8("popcnt")};
    (void)codegen_canonical_x64_metadata_emit_features(buffer, S8("POPCNT"), operands, BUSTER_ARRAY_LENGTH(operands),
                                                        (BusterX86MetadataFeatureInput){.names = feature_names, .count = BUSTER_ARRAY_LENGTH(feature_names)});
}


void x64_emit_vector_native_memory(X64Builder* builder, bool store, u32 size, X64Register base)
{
    if (size != 32 && size != 64)
    {
        builder->buffer.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    u16 vector_width = (u16)(size * 8);
    // The legacy native load/store opcode is VMOVUPS for both YMM and ZMM
    // widths.  Keep the aggregate memory atom at the 256-bit lane shape used
    // by the metadata schema; EVEX selects the 512-bit register width from
    // the ZMM physical class while AVX512F gates the wide form.
    String8 mnemonic = S8("VMOVUPS");
    u16 memory_width = 32;
    BusterX86MetadataPhysicalOperand memory = codegen_canonical_x64_metadata_memory_relaxed(base, memory_width, 0);
    BusterX86MetadataPhysicalOperand vector = codegen_canonical_x64_metadata_vector(0, vector_width);
    BusterX86MetadataPhysicalOperand operands[2] = {store ? memory : vector, store ? vector : memory};
    String8 feature_names[2] = {0};
    u32 feature_count = 0;
    if (size == 64)
    {
        feature_names[feature_count++] = S8("avx512f");
    }
    else
    {
        feature_names[feature_count++] = S8("avx");
    }
    (void)codegen_canonical_x64_metadata_emit_features(
        &builder->buffer, mnemonic, operands, BUSTER_ARRAY_LENGTH(operands),
        (BusterX86MetadataFeatureInput){.names = feature_names, .count = feature_count});
}

// Emit a native packed operation from an explicit IR element kind/width.
// The legacy opcode prefix is not sufficient to classify this operation:
// 0x66 is used by both packed integer and packed-double forms.  Keep the
// classification at the call site and use the physical metadata encoder for
// every form.
BUSTER_GLOBAL_LOCAL void x64_emit_vector_native_binary_operation_kind(X64Builder* builder, bool integer_operation, u16 element_width,
                                                                       u8 prefix, u8 opcode, u32 size, X64Register base)
{
    (void)prefix;
    if (size != 32 && size != 64)
    {
        builder->buffer.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    String8 mnemonic = {0};
    u16 memory_width = 0;
    if (integer_operation)
    {
        switch (opcode)
        {
        case 0xfc: mnemonic = S8("VPADDB"); memory_width = 8; break;
        case 0xfd: mnemonic = S8("VPADDW"); memory_width = 16; break;
        case 0xfe: mnemonic = S8("VPADDD"); memory_width = 32; break;
        case 0xd4: mnemonic = S8("VPADDQ"); memory_width = 64; break;
        case 0xf8: mnemonic = S8("VPSUBB"); memory_width = 8; break;
        case 0xf9: mnemonic = S8("VPSUBW"); memory_width = 16; break;
        case 0xfa: mnemonic = S8("VPSUBD"); memory_width = 32; break;
        case 0xfb: mnemonic = S8("VPSUBQ"); memory_width = 64; break;
        case 0xdb:
            mnemonic = size == 64 ? (element_width <= 32 ? S8("VPANDD") : S8("VPANDQ")) : S8("VPAND");
            // The AVX-512 D/Q logical forms use their tuple element width
            // (dword/qword) for memory matching even when the IR vector is
            // byte/word-granular: the operation is bitwise and does not
            // change its result based on the logical lane size.
            memory_width = size == 64 ? (element_width <= 32 ? 32 : 64) : 256;
            break;
        case 0xeb:
            mnemonic = size == 64 ? (element_width <= 32 ? S8("VPORD") : S8("VPORQ")) : S8("VPOR");
            memory_width = size == 64 ? (element_width <= 32 ? 32 : 64) : 256;
            break;
        case 0xef:
            mnemonic = size == 64 ? (element_width <= 32 ? S8("VPXORD") : S8("VPXORQ")) : S8("VPXOR");
            memory_width = size == 64 ? (element_width <= 32 ? 32 : 64) : 256;
            break;
        default: break;
        }
    }
    else
    {
        bool double_precision = element_width == 64;
        switch (opcode)
        {
        case 0x58: mnemonic = double_precision ? S8("VADDPD") : S8("VADDPS"); memory_width = double_precision ? 64 : 32; break;
        case 0x5c: mnemonic = double_precision ? S8("VSUBPD") : S8("VSUBPS"); memory_width = double_precision ? 64 : 32; break;
        case 0x59: mnemonic = double_precision ? S8("VMULPD") : S8("VMULPS"); memory_width = double_precision ? 64 : 32; break;
        case 0x5e: mnemonic = double_precision ? S8("VDIVPD") : S8("VDIVPS"); memory_width = double_precision ? 64 : 32; break;
        default: break;
        }
    }
    if (!mnemonic.length)
    {
        builder->buffer.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    u16 vector_width = (u16)(size * 8);
    BusterX86MetadataPhysicalOperand memory_operand = codegen_canonical_x64_metadata_memory_relaxed(base, memory_width, 0);
    // Vector memory operands carry the scalar element width used by the
    // tuple encoding together with their aggregate source width.  The
    // latter is what lets metadata distinguish (for example) a dword
    // tuple in a zmmword operand from an ordinary scalar dword load.
    memory_operand.memory.source_width = vector_width;
    BusterX86MetadataPhysicalOperand operands[3] = {
        codegen_canonical_x64_metadata_vector(0, vector_width),
        codegen_canonical_x64_metadata_vector(0, vector_width),
        memory_operand,
    };
    String8 feature_names[2] = {0};
    u32 feature_count = 0;
    if (size == 64)
    {
        feature_names[feature_count++] = S8("avx512f");
        if (integer_operation && (memory_width == 8 || memory_width == 16)) feature_names[feature_count++] = S8("avx512bw");
    }
    else
    {
        feature_names[feature_count++] = integer_operation ? S8("avx2") : S8("avx");
    }
    (void)codegen_canonical_x64_metadata_emit_features(
        &builder->buffer, mnemonic, operands, BUSTER_ARRAY_LENGTH(operands),
        (BusterX86MetadataFeatureInput){.names = feature_names, .count = feature_count});
}

// Keep the internal declaration's historical signature for out-of-line
// callers, while routing the canonical vector path through the explicit
// element-kind helper above.  This wrapper only serves legacy tests/tools;
// production call sites pass element kind and width directly below.
void x64_emit_vector_native_binary_operation(X64Builder* builder, u8 prefix, u8 opcode, u32 size, X64Register base)
{
    bool integer_operation = opcode != 0x58 && opcode != 0x5c && opcode != 0x59 && opcode != 0x5e;
    u16 element_width = 32;
    if (integer_operation)
    {
        element_width = opcode == 0xfc || opcode == 0xf8 ? 8 : opcode == 0xfd || opcode == 0xf9 ? 16 : opcode == 0xfe || opcode == 0xfa ? 32 : 64;
    }
    else if (prefix == 0x66)
    {
        element_width = 64;
    }
    x64_emit_vector_native_binary_operation_kind(builder, integer_operation, element_width, prefix, opcode, size, base);
}

bool x64_target_supports_native_vector(Target target, u64 size, u32 element_width, bool integer_operation)
{
    if (size <= 16 || size > target_vector_register_size(target))
    {
        return false;
    }
    if (!integer_operation)
    {
        return true;
    }
    if (size == 32)
    {
        return target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX2);
    }
    if (size == 64 && element_width < 32)
    {
        return target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512BW);
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool x64_vector_binary_is_commutative(IrBinaryOperation operation)
{
    return operation == IR_BINARY_VECTOR_INTEGER_ADD || operation == IR_BINARY_VECTOR_INTEGER_BITWISE_AND || operation == IR_BINARY_VECTOR_INTEGER_BITWISE_OR ||
           operation == IR_BINARY_VECTOR_INTEGER_BITWISE_XOR;
}

void x64_emit_vzeroupper(X64Builder* builder)
{
    if (!builder->upper_vector_dirty)
    {
        return;
    }
    String8 feature_names[] = {S8("avx")};
    (void)codegen_canonical_x64_metadata_emit_features(
        &builder->buffer, S8("VZEROUPPER"), 0, 0,
        (BusterX86MetadataFeatureInput){.names = feature_names, .count = BUSTER_ARRAY_LENGTH(feature_names)});
    builder->upper_vector_dirty = false;
    builder->last_wide_vector_result = IR_VALUE_ID_INVALID;
    builder->last_wide_vector_size = 0;
    builder->vzeroupper_count += 1;
}

BUSTER_GLOBAL_LOCAL bool x64_vector_comparison_condition(IrBinaryOperation operation, u8* condition_out, bool* ordered_out, bool* unordered_out)
{
    u8 condition = 0;
    bool ordered = false;
    bool unordered = false;
    switch (operation)
    {
    case IR_BINARY_VECTOR_INTEGER_EQUAL:
    case IR_BINARY_VECTOR_FLOAT_EQUAL:
        condition = 0x94;
        ordered = operation == IR_BINARY_VECTOR_FLOAT_EQUAL;
        break;
    case IR_BINARY_VECTOR_INTEGER_NOT_EQUAL:
    case IR_BINARY_VECTOR_FLOAT_NOT_EQUAL:
        condition = 0x95;
        unordered = operation == IR_BINARY_VECTOR_FLOAT_NOT_EQUAL;
        break;
    case IR_BINARY_VECTOR_SIGNED_LESS:
        condition = 0x9c;
        break;
    case IR_BINARY_VECTOR_SIGNED_LESS_EQUAL:
        condition = 0x9e;
        break;
    case IR_BINARY_VECTOR_SIGNED_GREATER:
        condition = 0x9f;
        break;
    case IR_BINARY_VECTOR_SIGNED_GREATER_EQUAL:
        condition = 0x9d;
        break;
    case IR_BINARY_VECTOR_UNSIGNED_LESS:
    case IR_BINARY_VECTOR_FLOAT_LESS:
        condition = 0x92;
        ordered = operation == IR_BINARY_VECTOR_FLOAT_LESS;
        break;
    case IR_BINARY_VECTOR_UNSIGNED_LESS_EQUAL:
    case IR_BINARY_VECTOR_FLOAT_LESS_EQUAL:
        condition = 0x96;
        ordered = operation == IR_BINARY_VECTOR_FLOAT_LESS_EQUAL;
        break;
    case IR_BINARY_VECTOR_UNSIGNED_GREATER:
    case IR_BINARY_VECTOR_FLOAT_GREATER:
        condition = 0x97;
        break;
    case IR_BINARY_VECTOR_UNSIGNED_GREATER_EQUAL:
    case IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL:
        condition = 0x93;
        break;
    default:
        return false;
    }
    *condition_out = condition;
    *ordered_out = ordered;
    *unordered_out = unordered;
    return true;
}


BUSTER_GLOBAL_LOCAL BUSTER_INLINE void codegen_record_line_hot(CodegenLineEntry* entries, u32* count, u32 capacity, u32 code_offset, u32 source,
                                                               u32 line, u32 column)
{
    if (!entries || !line || *count >= capacity)
    {
        return;
    }
    // The 12-byte record stores these as u16; saturate rather than truncate
    // so an overflowing source cannot alias an unrelated file.
    u16 stored_source = source <= UINT16_MAX ? (u16)source : 0;
    u16 stored_column = column <= UINT16_MAX ? (u16)column : UINT16_MAX;
    if (*count)
    {
        CodegenLineEntry* last = entries + (*count - 1);
        if (last->code_offset == code_offset || (last->source == stored_source && last->line == line && last->column == stored_column))
        {
            return;
        }
    }
    entries[*count] = (CodegenLineEntry){
        .code_offset = code_offset,
        .source = stored_source,
        .line = line,
        .column = stored_column,
    };
    *count += 1;
}

void codegen_record_line(CodegenLineEntry* entries, u32* count, u32 capacity, u32 code_offset, u32 source, u32 line, u32 column)
{
    codegen_record_line_hot(entries, count, capacity, code_offset, source, line, column);
}


// Debug locations use the frame pointer as their common base.  x86-64 storage
// offsets are distances below RBP and need their sign changed; AArch64
// codegen stores values relative to the final SP and keeps X29 at the
// pre-allocation SP, so translate those offsets back across the frame here.
s32 codegen_debug_frame_offset(u32 offset, Target target, bool negative_offsets, u32 frame_size)
{
    s64 result = offset > INT32_MAX ? INT32_MAX : (s64)offset;
    if (target.cpu_arch == CPU_ARCH_AARCH64)
    {
        result -= frame_size;
    }
    else if (negative_offsets)
    {
        result = -result;
    }
    if (result < INT32_MIN)
    {
        result = INT32_MIN;
    }
    else if (result > INT32_MAX)
    {
        result = INT32_MAX;
    }
    return (s32)result;
}


BUSTER_GLOBAL_LOCAL DebugLocation codegen_debug_canonical_value_location(IrValueId value, IrFunction* function, u32* value_offsets, Target target,
                                                                         u32 frame_size, s32 frame_base_offset)
{
    if (!function || !value_offsets || value.value >= function->value_count)
    {
        return (DebugLocation){
            .kind = DEBUG_LOCATION_UNAVAILABLE,
        };
    }
    u32 offset = value_offsets[value.value];
    s32 frame_offset = target.cpu_arch == CPU_ARCH_X86_64 ? (s32)((s64)frame_base_offset - (s64)offset)
                                                          : codegen_debug_frame_offset(offset, target, true, frame_size);
    return (DebugLocation){
        .kind = DEBUG_LOCATION_FRAME,
        .frame_offset = frame_offset,
    };
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_location_append(CodegenModule* result, u32 capacity, IrSymbolId symbol, IrLocalId local, u32 start, u32 end,
                                                            DebugLocation location)
{
    if (!result || result->debug_location_count >= capacity || end <= start)
    {
        return;
    }
    result->debug_locations[result->debug_location_count++] = (DebugLocationSeed){
        .function_symbol = symbol,
        .local = local,
        .start = start,
        .end = end,
        .location = location,
    };
}

BUSTER_GLOBAL_LOCAL void codegen_record_canonical_locations(CodegenModule* result, IrFunction* function, u32* value_offsets, u32* block_offsets,
                                                             u32 function_start, u32 function_end, Target target, u32 frame_size,
                                                             s32 frame_base_offset, u32 capacity)
{
    if (!result || !function || !function->debug_local_count || !result->debug_locations)
    {
        return;
    }
    TemporalArena temporary = scratch_begin(0, 0);
    IrValueId* local_places = arena_allocate(temporary.arena, IrValueId, function->local_count);
    memset(local_places, 0xff, sizeof(*local_places) * function->local_count);
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        if (instruction->result.value < function->value_count && instruction->canonical_local.value < function->local_count &&
            (instruction->opcode == IR_OPCODE_LOCAL || instruction->opcode == IR_OPCODE_ARGUMENT) &&
            local_places[instruction->canonical_local.value].value == IR_ID_UNDERLYING_INVALID)
        {
            local_places[instruction->canonical_local.value] = instruction->result;
        }
    }
    for (u32 local_index = 0; local_index < function->debug_local_count; local_index += 1)
    {
        IrDebugLocal* local = function->debug_locals + local_index;
        if (local->id.value == IR_ID_UNDERLYING_INVALID)
        {
            continue;
        }
        bool emitted = false;
        IrValueId place = local->id.value < function->local_count ? local_places[local->id.value] : IR_VALUE_ID_INVALID;
        if (place.value == IR_ID_UNDERLYING_INVALID && local->id.value >= function->local_count)
        {
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = function->instructions + instruction_index;
                if (instruction->result.value < function->value_count && instruction->canonical_local.value == local->id.value &&
                    (instruction->opcode == IR_OPCODE_LOCAL || instruction->opcode == IR_OPCODE_ARGUMENT))
                {
                    place = instruction->result;
                    break;
                }
            }
        }
        if (place.value != IR_ID_UNDERLYING_INVALID)
        {
            codegen_canonical_location_append(result, capacity, function->symbol, local->id, function_start, function_end,
                                              codegen_debug_canonical_value_location(place, function, value_offsets, target, frame_size, frame_base_offset));
            emitted = true;
        }
        if (!emitted && block_offsets)
        {
            for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
            {
                IrBlock* block = function->blocks + block_index;
                IrValueId value = IR_VALUE_ID_INVALID;
                if (block->local_values && local->id.value < function->local_count)
                {
                    value = block->local_values[local->id.value];
                }
                if (value.value == IR_ID_UNDERLYING_INVALID)
                {
                    for (IrBlockParameter* parameter = block->first_parameter; parameter; parameter = parameter->next)
                    {
                        if (parameter->canonical_local.value == local->id.value)
                        {
                            value = parameter->value;
                            break;
                        }
                    }
                }
                if (value.value == IR_ID_UNDERLYING_INVALID)
                {
                    continue;
                }
                u32 start = BUSTER_MAX(block_offsets[block_index], function_start);
                u32 end = block_index + 1 < function->block_count ? block_offsets[block_index + 1] : function_end;
                end = BUSTER_MIN(end, function_end);
                codegen_canonical_location_append(result, capacity, function->symbol, local->id, start, end,
                                                  codegen_debug_canonical_value_location(value, function, value_offsets, target, frame_size, frame_base_offset));
                emitted |= end > start;
            }
        }
        if (!emitted)
        {
            codegen_canonical_location_append(result, capacity, function->symbol, local->id, function_start, function_end,
                                              (DebugLocation){
                                                  .kind = DEBUG_LOCATION_UNAVAILABLE,
                                              });
        }
    }
    scratch_end(temporary);
}

typedef struct A64Relocation A64Relocation;
struct A64Relocation
{
    A64Relocation* next;
    IrBlockId target;
    u32 instruction_offset;
    bool conditional;
    u8 reserved[3];
};

#define A64_VALUE_SLOT_COMPONENT_COUNT 4

BUSTER_GLOBAL_LOCAL void a64_emit_instruction_word(CodegenBuffer* buffer, u32 instruction)
{
    codegen_emit_u32(buffer, instruction);
}

BUSTER_GLOBAL_LOCAL u32 a64_value_offset(IrValueId value)
{
    return value.value * A64_VALUE_SLOT_SIZE;
}

BUSTER_GLOBAL_LOCAL u32 a64_value_component_offset(IrValueId value, u32 component)
{
    return a64_value_offset(value) + component * 8;
}




BUSTER_GLOBAL_LOCAL void a64_emit_store_offset(CodegenBuffer* buffer, u32 source, u32 offset)
{
    if (offset > 32760)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    a64_emit_instruction_word(buffer, 0xf90003e0 | ((offset / 8) << 10) | source);
}

void a64_emit_float_load_offset(CodegenBuffer* buffer, u32 target, u32 offset, u32 size)
{
    u32 scale = size <= 4 ? 4 : size <= 8 ? 8 : 16;
    if (offset % scale || offset / scale > A64_IMM12_MAX)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    a64_emit_instruction_word(buffer, (size <= 4 ? 0xbd4003e0 : size <= 8 ? 0xfd4003e0 : 0x3dc003e0) | ((offset / scale) << 10) | target);
}

void a64_emit_float_store_offset(CodegenBuffer* buffer, u32 source, u32 offset, u32 size)
{
    u32 scale = size <= 4 ? 4 : size <= 8 ? 8 : 16;
    if (offset % scale || offset / scale > A64_IMM12_MAX)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return;
    }
    a64_emit_instruction_word(buffer, (size <= 4 ? 0xbd0003e0 : size <= 8 ? 0xfd0003e0 : 0x3d8003e0) | ((offset / scale) << 10) | source);
}


BUSTER_GLOBAL_LOCAL void a64_emit_store_value_component(CodegenBuffer* buffer, u32 source, IrValueId value, u32 component)
{
    a64_emit_store_offset(buffer, source, a64_value_component_offset(value, component));
}



BUSTER_GLOBAL_LOCAL void a64_emit_constant(CodegenBuffer* buffer, u32 target, u64 value)
{
    a64_emit_instruction_word(buffer, 0xd2800000 | ((u32)(value & 0xffff) << 5) | target);
    for (u32 shift = 16; shift < 64; shift += 16)
    {
        a64_emit_instruction_word(buffer, 0xf2800000 | ((shift / 16) << 21) | ((u32)((value >> shift) & 0xffff) << 5) | target);
    }
}

// The same materialization without the fixed four-word cost: movz on the
// lowest halfword that carries a bit, movk on each higher halfword that
// does. A value below 2^16 therefore still costs exactly the one movz that
// the callers used to emit inline, so widening a caller past that boundary
// changes no byte of any program that already compiled.
BUSTER_GLOBAL_LOCAL void a64_emit_constant_compact(CodegenBuffer* buffer, u32 target, u64 value)
{
    u32 shift = 0;
    while (shift < 48 && (value >> shift) && !((value >> shift) & 0xffff))
    {
        shift += 16;
    }
    a64_emit_instruction_word(buffer, 0xd2800000 | ((shift / 16) << 21) | ((u32)((value >> shift) & 0xffff) << 5) | target);
    for (shift += 16; shift < 64; shift += 16)
    {
        u32 halfword = (u32)((value >> shift) & 0xffff);
        if (halfword)
        {
            a64_emit_instruction_word(buffer, 0xf2800000 | ((shift / 16) << 21) | (halfword << 5) | target);
        }
    }
}

BUSTER_GLOBAL_LOCAL bool a64_emit_windows_large_stack_adjust(CodegenBuffer* buffer, u32 size, bool subtract,
                                                             CodegenFunctionDescriptor* descriptor, u32 action_capacity)
{
    if (size <= A64_SP_ADJUST_CHUNK || size % 16)
    {
        return false;
    }
    u32 units = size / 16;
    if (!subtract)
    {
        a64_emit_constant(buffer, 15, units);
        a64_emit_instruction_word(buffer, 0x8b2f73ff);
        return true;
    }
    u32 instruction_offsets[13] = {0};
    for (u32 shift = 0; shift < 64; shift += 16)
    {
        a64_emit_instruction_word(buffer,
                                  (shift ? 0xf2800000 : 0xd2800000) | ((shift / 16) << 21) |
                                      ((u32)(((u64)units >> shift) & 0xffff) << 5) | 15);
        instruction_offsets[shift / 16] = (u32)buffer->count;
    }
    a64_emit_instruction_word(buffer, 0x910003f0);
    instruction_offsets[4] = (u32)buffer->count;
    a64_emit_instruction_word(buffer, 0xcb0f1210);
    instruction_offsets[5] = (u32)buffer->count;
    a64_emit_instruction_word(buffer, 0x910003f1);
    instruction_offsets[6] = (u32)buffer->count;
    u32 loop_offset = (u32)buffer->count;
    a64_emit_instruction_word(buffer, 0xd1400631);
    instruction_offsets[7] = (u32)buffer->count;
    a64_emit_instruction_word(buffer, 0xeb10023f);
    instruction_offsets[8] = (u32)buffer->count;
    u32 final_branch = (u32)buffer->count;
    a64_emit_instruction_word(buffer, 0x54000009);
    instruction_offsets[9] = (u32)buffer->count;
    a64_emit_instruction_word(buffer, 0xf900023f);
    instruction_offsets[10] = (u32)buffer->count;
    u32 loop_branch = (u32)buffer->count;
    a64_emit_instruction_word(buffer, 0x14000000);
    instruction_offsets[11] = (u32)buffer->count;
    u32 final_offset = (u32)buffer->count;
    a64_emit_instruction_word(buffer, 0xf900021f);
    instruction_offsets[12] = (u32)buffer->count;
    a64_emit_instruction_word(buffer, 0xcb2f73ff);
    if (buffer->error != CODEGEN_ERROR_NONE || (final_offset - final_branch) % 4 || (loop_offset - loop_branch) % 4)
    {
        buffer->error = CODEGEN_ERROR_CAPACITY;
        return true;
    }
    u32 final_words = (final_offset - final_branch) / 4;
    s32 loop_words = ((s32)loop_offset - (s32)loop_branch) / 4;
    u32 final_instruction = 0x54000009 | ((final_words & 0x7ffff) << 5);
    u32 loop_instruction = 0x14000000 | ((u32)loop_words & 0x03ffffff);
    memcpy(buffer->bytes + final_branch, &final_instruction, sizeof(final_instruction));
    memcpy(buffer->bytes + loop_branch, &loop_instruction, sizeof(loop_instruction));
    if (descriptor)
    {
        for (u32 instruction_index = 0; instruction_index < BUSTER_ARRAY_LENGTH(instruction_offsets); instruction_index += 1)
        {
            if (!codegen_unwind_action_append(descriptor, action_capacity, instruction_offsets[instruction_index] - descriptor->code_offset,
                                              CODEGEN_UNWIND_ACTION_NOP, 0, 0))
            {
                buffer->error = CODEGEN_ERROR_CAPACITY;
                return true;
            }
        }
        if (!codegen_unwind_action_append(descriptor, action_capacity, (u32)buffer->count - descriptor->code_offset,
                                          CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, size))
        {
            buffer->error = CODEGEN_ERROR_CAPACITY;
        }
    }
    return true;
}


BUSTER_GLOBAL_LOCAL void a64_emit_stack_address(CodegenBuffer* buffer, u32 target, u32 offset)
{
    a64_emit_instruction_word(buffer, 0x910003e0 | target);
    while (offset)
    {
        u32 chunk = BUSTER_MIN(offset, A64_IMM12_MAX);
        a64_emit_instruction_word(buffer, 0x91000000 | target | (target << 5) | (chunk << 10));
        offset -= chunk;
    }
}

BUSTER_GLOBAL_LOCAL void a64_emit_load_pointer(CodegenBuffer* buffer, u32 target, u32 address, u32 size)
{
    u32 encoded = size == 1 ? 0x39400000 : size == 2 ? 0x79400000 : size == 4 ? 0xb9400000 : size == 8 ? 0xf9400000 : 0;
    if (!encoded)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    a64_emit_instruction_word(buffer, encoded | (address << 5) | target);
}

BUSTER_GLOBAL_LOCAL void a64_emit_store_pointer(CodegenBuffer* buffer, u32 source, u32 address, u32 size)
{
    u32 encoded = size == 1 ? 0x39000000 : size == 2 ? 0x79000000 : size == 4 ? 0xb9000000 : size == 8 ? 0xf9000000 : 0;
    if (!encoded)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    a64_emit_instruction_word(buffer, encoded | (address << 5) | source);
}

BUSTER_GLOBAL_LOCAL void a64_emit_atomic_pointer(CodegenBuffer* buffer, u32 value, u32 address, u32 size, bool store)
{
    u32 size_bits = size == 1 ? 0 : size == 2 ? UINT32_C(0x40000000) : size == 4 ? UINT32_C(0x80000000) : size == 8 ? UINT32_C(0xc0000000) : 0;
    if ((size != 1 && !size_bits) || value > 31 || address > 31)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    a64_emit_instruction_word(buffer, (store ? UINT32_C(0x089ffc00) : UINT32_C(0x08dffc00)) | size_bits | (address << 5) | value);
}

BUSTER_GLOBAL_LOCAL void a64_emit_atomic_exclusive_load(CodegenBuffer* buffer, u32 value, u32 address, u32 size, bool acquire)
{
    u32 size_bits = size == 1 ? 0 : size == 2 ? UINT32_C(0x40000000) : size == 4 ? UINT32_C(0x80000000) : size == 8 ? UINT32_C(0xc0000000) : 0;
    if ((size != 1 && !size_bits) || value > 31 || address > 31)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    a64_emit_instruction_word(buffer, (acquire ? UINT32_C(0x085ffc00) : UINT32_C(0x085f7c00)) | size_bits | (address << 5) | value);
}

BUSTER_GLOBAL_LOCAL void a64_emit_atomic_exclusive_store(CodegenBuffer* buffer, u32 status, u32 value, u32 address, u32 size, bool release)
{
    u32 size_bits = size == 1 ? 0 : size == 2 ? UINT32_C(0x40000000) : size == 4 ? UINT32_C(0x80000000) : size == 8 ? UINT32_C(0xc0000000) : 0;
    if ((size != 1 && !size_bits) || status > 31 || value > 31 || address > 31)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    a64_emit_instruction_word(buffer, (release ? UINT32_C(0x0800fc00) : UINT32_C(0x08007c00)) | size_bits | (status << 16) | (address << 5) | value);
}

void codegen_canonical_a64_base_address(CodegenBuffer* buffer, u32 register_number, u32 base_register, u32 byte_offset);

void a64_emit_load_pointer_offset(CodegenBuffer* buffer, u32 target, u32 address, u32 offset, u32 size)
{
    u32 scale = size == 1 ? 1 : size == 2 ? 2 : size == 4 ? 4 : 8;
    u32 encoded = size == 1 ? 0x39400000 : size == 2 ? 0x79400000 : size == 4 ? 0xb9400000 : size == 8 ? 0xf9400000 : 0;
    if (!encoded || offset % scale || target > 30 || address > 31)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    if (offset / scale > A64_IMM12_MAX)
    {
        codegen_canonical_a64_base_address(buffer, target, address, offset);
        address = target;
        offset = 0;
    }
    a64_emit_instruction_word(buffer, encoded | ((offset / scale) << 10) | (address << 5) | target);
}

void a64_emit_store_pointer_offset(CodegenBuffer* buffer, u32 source, u32 address, u32 offset, u32 size)
{
    u32 scale = size == 1 ? 1 : size == 2 ? 2 : size == 4 ? 4 : 8;
    u32 encoded = size == 1 ? 0x39000000 : size == 2 ? 0x79000000 : size == 4 ? 0xb9000000 : size == 8 ? 0xf9000000 : 0;
    if (!encoded || offset % scale || source > 31 || address > 31)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    if (offset / scale > A64_IMM12_MAX)
    {
        u32 scratch = 16;
        if (scratch == source || scratch == address)
        {
            scratch = 17;
        }
        if (scratch == source || scratch == address)
        {
            scratch = 15;
        }
        codegen_canonical_a64_base_address(buffer, scratch, address, offset);
        address = scratch;
        offset = 0;
    }
    a64_emit_instruction_word(buffer, encoded | ((offset / scale) << 10) | (address << 5) | source);
}


void a64_emit_copy_memory_registers(CodegenBuffer* buffer, u32 destination, u32 source, u32 scratch, u32 size)
{
    u32 offset = 0;
    while (size - offset >= 8)
    {
        a64_emit_instruction_word(buffer, 0xf9400000 | ((offset / 8) << 10) | (source << 5) | scratch);
        a64_emit_instruction_word(buffer, 0xf9000000 | ((offset / 8) << 10) | (destination << 5) | scratch);
        offset += 8;
    }
    if (size - offset >= 4)
    {
        a64_emit_instruction_word(buffer, 0xb9400000 | ((offset / 4) << 10) | (source << 5) | scratch);
        a64_emit_instruction_word(buffer, 0xb9000000 | ((offset / 4) << 10) | (destination << 5) | scratch);
        offset += 4;
    }
    if (size - offset >= 2)
    {
        a64_emit_instruction_word(buffer, 0x79400000 | ((offset / 2) << 10) | (source << 5) | scratch);
        a64_emit_instruction_word(buffer, 0x79000000 | ((offset / 2) << 10) | (destination << 5) | scratch);
        offset += 2;
    }
    if (size != offset)
    {
        a64_emit_instruction_word(buffer, 0x39400000 | (offset << 10) | (source << 5) | scratch);
        a64_emit_instruction_word(buffer, 0x39000000 | (offset << 10) | (destination << 5) | scratch);
    }
}


void a64_emit_initialize_aggregate_result(CodegenBuffer* buffer, u32* value_storage_offsets, IrValueId value)
{
    a64_emit_stack_address(buffer, 16, value_storage_offsets[value.value]);
    a64_emit_store_value_component(buffer, 16, value, 0);
}




BUSTER_GLOBAL_LOCAL bool codegen_canonical_register_is_64_bit(IrProgram* program, IrTypeId type_id)
{
    IrType* type = ir_type_from_id(&program->types, type_id);
    return type && (type->kind == IR_TYPE_POINTER || type->kind == IR_TYPE_FUNCTION || (type->kind == IR_TYPE_INTEGER && type->bit_width > 32) ||
                    (type->kind == IR_TYPE_FLOAT && type->bit_width > 32));
}

BUSTER_GLOBAL_LOCAL IrAbiConvention codegen_canonical_ir_abi_convention(CodegenAbi abi)
{
    return ir_abi_convention_for_target(codegen_target_for_abi(abi));
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_abi_part_is_float(IrAbiClass abi_class)
{
    return abi_class == IR_ABI_CLASS_FLOAT || abi_class == IR_ABI_CLASS_VECTOR;
}

// The canonical x86-64 backend keeps the 80-bit spelling in a sixteen-byte
// slot: ten semantic bytes followed by six zero bytes.  It is deliberately a
// byte-level representation here; host long double has a different size and
// alignment on some targets and must never participate in code generation.
bool codegen_canonical_x64_type_is_f80(IrType* type)
{
    return type && type->kind == IR_TYPE_FLOAT && type->bit_width == 80 && type->layout.resolved && type->layout.size == 16 &&
           type->layout.alignment == 16;
}

typedef enum CodegenCanonicalX64F80State
{
    CODEGEN_CANONICAL_X64_F80_UNKNOWN,
    CODEGEN_CANONICAL_X64_F80_VISITING,
    CODEGEN_CANONICAL_X64_F80_SAFE,
    CODEGEN_CANONICAL_X64_F80_CONTAINS,
} CodegenCanonicalX64F80State;

typedef struct CodegenCanonicalX64F80Work CodegenCanonicalX64F80Work;
struct CodegenCanonicalX64F80Work
{
    IrTypeId type;
    u32 next_child;
};

typedef struct CodegenCanonicalX64F80Cache CodegenCanonicalX64F80Cache;
struct CodegenCanonicalX64F80Cache
{
    IrProgram* program;
    IrType* types;
    u8* state;
    CodegenCanonicalX64F80Work* work;
    u32 capacity;
    bool allocation_failed;
    u8 reserved[3];
};

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_f80_cache_arena_capacity(Arena* arena, u32 count)
{
    if (!arena)
    {
        return false;
    }
    u64 state_bytes = count;
    u64 work_bytes = (u64)count * sizeof(CodegenCanonicalX64F80Work);
    u64 position = arena->position;
    u64 state_end = position > arena->reserved_size || state_bytes > arena->reserved_size - position ? UINT64_MAX : position + state_bytes;
    if (state_end == UINT64_MAX)
    {
        return false;
    }
    u64 work_alignment = BUSTER_ALIGN_OF(CodegenCanonicalX64F80Work);
    if (state_end > UINT64_MAX - (work_alignment - 1))
    {
        return false;
    }
    u64 work_start = (state_end + work_alignment - 1) & ~(work_alignment - 1);
    return work_start <= arena->reserved_size && work_bytes <= arena->reserved_size - work_start;
}

// A type graph is shared by every function in a canonical module.  Classify it
// once, iteratively, and read the immutable result per function.  Pointer
// and function referents are not part of a value representation; vectors are
// included because an element type still contributes to the value's shape.
BUSTER_GLOBAL_LOCAL CodegenCanonicalX64F80Cache codegen_canonical_x64_f80_cache_initialize(Arena* arena, IrProgram* program)
{
    CodegenCanonicalX64F80Cache result = {
        .program = program,
        .types = program ? program->types.types : 0,
        .capacity = program ? program->types.count : 0,
    };
    if (!arena || !program)
    {
        result.allocation_failed = true;
        return result;
    }
    if (!result.capacity)
    {
        return result;
    }
    if (!codegen_canonical_x64_f80_cache_arena_capacity(arena, result.capacity))
    {
        result.allocation_failed = true;
        return result;
    }
    result.state = arena_allocate(arena, u8, result.capacity);
    result.work = arena_allocate(arena, CodegenCanonicalX64F80Work, result.capacity);
    if (!result.state || !result.work)
    {
        result.allocation_failed = true;
        return result;
    }
    memset(result.state, CODEGEN_CANONICAL_X64_F80_UNKNOWN, result.capacity * sizeof(*result.state));
    for (u32 root_index = 0; root_index < result.capacity; root_index += 1)
    {
        if (result.state[root_index] != CODEGEN_CANONICAL_X64_F80_UNKNOWN)
        {
            continue;
        }
        u32 work_count = 1;
        result.state[root_index] = CODEGEN_CANONICAL_X64_F80_VISITING;
        result.work[0] = (CodegenCanonicalX64F80Work){.type = (IrTypeId){.value = root_index}};
        while (work_count)
        {
            CodegenCanonicalX64F80Work* frame = result.work + work_count - 1;
            IrTypeId type_id = frame->type;
            IrType* type = ir_type_from_id(&program->types, type_id);
            if (!type)
            {
                result.state[type_id.value] = CODEGEN_CANONICAL_X64_F80_CONTAINS;
                work_count -= 1;
                continue;
            }
            // Treat every f80 spelling as a wide value here.  The stricter
            // type_is_f80 predicate below then rejects an unresolved,
            // mis-sized, or misaligned spelling instead of silently lowering
            // it as an ordinary scalar.
            if (type->kind == IR_TYPE_FLOAT && type->bit_width == 80)
            {
                result.state[type_id.value] = CODEGEN_CANONICAL_X64_F80_CONTAINS;
                work_count -= 1;
                continue;
            }
            u32 child_count = 0;
            if (type->kind == IR_TYPE_ARRAY || type->kind == IR_TYPE_VECTOR)
            {
                child_count = 1;
            }
            else if (type->kind == IR_TYPE_STRUCT || type->kind == IR_TYPE_UNION)
            {
                child_count = type->field_count;
            }
            if (frame->next_child >= child_count)
            {
                result.state[type_id.value] = CODEGEN_CANONICAL_X64_F80_SAFE;
                work_count -= 1;
                continue;
            }
            IrTypeId child = (type->kind == IR_TYPE_ARRAY || type->kind == IR_TYPE_VECTOR)
                                 ? type->element_type
                                 : (type->fields ? type->fields[frame->next_child].type : IR_TYPE_ID_INVALID);
            if (child.value >= result.capacity)
            {
                result.state[type_id.value] = CODEGEN_CANONICAL_X64_F80_CONTAINS;
                work_count -= 1;
                continue;
            }
            u8 child_state = result.state[child.value];
            if (child_state == CODEGEN_CANONICAL_X64_F80_CONTAINS)
            {
                result.state[type_id.value] = CODEGEN_CANONICAL_X64_F80_CONTAINS;
                work_count -= 1;
            }
            else if (child_state == CODEGEN_CANONICAL_X64_F80_SAFE)
            {
                frame->next_child += 1;
            }
            else if (child_state == CODEGEN_CANONICAL_X64_F80_VISITING)
            {
                // Direct recursive value graphs are invalid IR.  Mark the
                // cycle wide so a caller cannot accidentally lower it.
                result.state[child.value] = CODEGEN_CANONICAL_X64_F80_CONTAINS;
                result.state[type_id.value] = CODEGEN_CANONICAL_X64_F80_CONTAINS;
                work_count -= 1;
            }
            else if (work_count >= result.capacity)
            {
                result.state[type_id.value] = CODEGEN_CANONICAL_X64_F80_CONTAINS;
                work_count -= 1;
            }
            else
            {
                result.state[child.value] = CODEGEN_CANONICAL_X64_F80_VISITING;
                result.work[work_count++] = (CodegenCanonicalX64F80Work){.type = child};
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_type_contains_f80_cached(CodegenCanonicalX64F80Cache const* cache, IrProgram* program, IrTypeId type_id)
{
    // AArch64 never consumes the x86 cache.  The zero value threaded through
    // its shared emitter is deliberately benign so an ordinary narrow value
    // is not mistaken for an allocation failure or an unsupported wide value.
    if (cache && !cache->program && !cache->types && !cache->state && !cache->work && !cache->capacity && !cache->allocation_failed)
    {
        return false;
    }
    if (!cache || cache->allocation_failed || !program || cache->types != program->types.types || !cache->state || type_id.value >= cache->capacity)
    {
        // A missing or mismatched cache is not evidence that the value is
        // narrow.  Force the caller down its explicit unsupported/capacity
        // path rather than silently lowering an unchecked wide value.
        return true;
    }
    return cache->state[type_id.value] == CODEGEN_CANONICAL_X64_F80_CONTAINS;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_type_is_f80_x87_shape_cached(CodegenCanonicalX64F80Cache const* cache, IrProgram* program,
                                                                              IrTypeId type_id);

// Direct callers outside module generation retain the old helper API.  They
// pay one bounded cache build; canonical emission passes its reusable cache.
bool codegen_canonical_x64_type_contains_f80(IrProgram* program, IrTypeId type_id)
{
    TemporalArena temporary = scratch_begin(0, 0);
    CodegenCanonicalX64F80Cache cache = codegen_canonical_x64_f80_cache_initialize(temporary.arena, program);
    bool result = codegen_canonical_x64_type_contains_f80_cached(&cache, program, type_id);
    scratch_end(temporary);
    return result;
}

bool codegen_canonical_x64_abi_is_f80_result(IrType* type, CodegenCanonicalAbiValue const* abi)
{
    if (!type || !abi || abi->memory || abi->indirect || abi->part_count != 2 || type->layout.size != 16)
    {
        return false;
    }
    if (!codegen_canonical_x64_type_is_f80(type) && type->kind != IR_TYPE_STRUCT && type->kind != IR_TYPE_UNION && type->kind != IR_TYPE_ARRAY)
    {
        return false;
    }
    return abi->parts[0].abi_class == IR_ABI_CLASS_X87 && abi->parts[1].abi_class == IR_ABI_CLASS_X87_UP &&
           abi->parts[0].value_offset == 0 && abi->parts[1].value_offset == 8 && abi->parts[0].size == 8 && abi->parts[1].size == 8;
}

// How many consecutive vector registers one ABI part occupies on this target,
// and how much of it each one carries. The IR ABI classifies a vector by the
// psABI rule alone -- a 512-bit vector is one vector part whatever the machine
// -- so a part can be wider than any register the target owns. It then travels
// in as many registers as it takes, which is the lowering clang emits for the
// same declaration: xmm0 through xmm3 for a 64-byte vector without AVX, ymm0
// and ymm1 with it. Zero means the target cannot carry the part at all.
BUSTER_GLOBAL_LOCAL u32 codegen_canonical_x64_vector_part_registers(Target const* target, u32 size, u32* register_size)
{
    // Every x86-64 target has a sixteen-byte vector register -- the psABI puts
    // one in the baseline -- so only a part wider than that has to ask the
    // target what it owns, and every part of a scalar signature can skip a
    // question whose answer is a feature-set walk.
    if (size && size <= 16)
    {
        *register_size = size;
        return 1;
    }
    u32 width = target_vector_register_size(*target);
    if (!width || !size)
    {
        return 0;
    }
    if (size <= width)
    {
        *register_size = size;
        return 1;
    }
    if (size % width)
    {
        return 0;
    }
    *register_size = width;
    return size / width;
}

// Whether this target hands the value over in the registers the classification
// named. A part it has to split is one the psABI expected a single register to
// hold, and the split is only available to a return, whose registers are its
// own; an argument competing for the shared pool is passed in memory instead,
// which is again what clang does for the same declaration.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_abi_value_in_registers(CodegenCanonicalAbiValue const* value, Target const* target)
{
    for (u32 part_index = 0; part_index < value->part_count; part_index += 1)
    {
        CodegenCanonicalAbiPart const* part = value->parts + part_index;
        u32 register_size = 0;
        if (part->size > 16 && codegen_canonical_abi_part_is_float(part->abi_class) &&
            codegen_canonical_x64_vector_part_registers(target, part->size, &register_size) != 1)
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL CodegenCanonicalAbiValue codegen_canonical_aggregate_abi(IrProgram* program, IrTypeId type_id, CodegenAbi abi, bool is_result,
                                                                             bool variadic_argument)
{
    BUSTER_CHECK(abi < CODEGEN_ABI_COUNT);
    IrAbiConvention convention = codegen_canonical_ir_abi_convention(abi);
    IrAbiUse use = is_result ? IR_ABI_USE_RESULT : variadic_argument ? IR_ABI_USE_VARIADIC_ARGUMENT : IR_ABI_USE_ARGUMENT;
    return ir_type_abi_value(program, type_id, convention, use);
}

// The ABI classifier is the authority for aggregate x87 shapes.  It admits
// nested one-field wrappers, one-element arrays, and unions whose alternatives
// all occupy the same x87 payload, while mixed/offset aggregates classify as
// MEMORY or INTEGER and are rejected.  Do not duplicate that recursive walk in
// codegen; use the module-wide contains cache plus the already-resolved result
// classes instead.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_type_is_f80_x87_shape_cached(CodegenCanonicalX64F80Cache const* cache, IrProgram* program,
                                                                              IrTypeId type_id)
{
    if (!cache || cache->allocation_failed || !program)
    {
        return false;
    }
    IrType* type = ir_type_from_id(&program->types, type_id);
    if (!type || !type->layout.resolved || type->layout.size != 16 || type->layout.alignment != 16)
    {
        return false;
    }
    if (codegen_canonical_x64_type_is_f80(type))
    {
        return true;
    }
    if (type->kind != IR_TYPE_STRUCT && type->kind != IR_TYPE_UNION && type->kind != IR_TYPE_ARRAY)
    {
        return false;
    }
    if (!codegen_canonical_x64_type_contains_f80_cached(cache, program, type_id))
    {
        return false;
    }
    CodegenCanonicalAbiValue result_abi = codegen_canonical_aggregate_abi(program, type_id, CODEGEN_ABI_X86_64_SYSTEM_V, true, false);
    return codegen_canonical_x64_abi_is_f80_result(type, &result_abi);
}

bool codegen_canonical_x64_type_is_f80_x87_shape(IrProgram* program, IrTypeId type_id)
{
    TemporalArena temporary = scratch_begin(0, 0);
    CodegenCanonicalX64F80Cache cache = codegen_canonical_x64_f80_cache_initialize(temporary.arena, program);
    bool result = codegen_canonical_x64_type_is_f80_x87_shape_cached(&cache, program, type_id);
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL u32 codegen_canonical_va_list_component_count(IrProgram* program, IrTypeId type_id)
{
    IrType* type = ir_type_from_id(&program->types, type_id);
    if (!type || type->kind != IR_TYPE_VA_LIST || !type->layout.resolved || !type->layout.size || type->layout.size > 32 || (type->layout.size & 7))
    {
        return 0;
    }
    return (u32)(type->layout.size / 8);
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_integer_aggregate_parts(IrProgram* program, IrTypeId type_id, u32* part_count)
{
    IrType* type = ir_type_from_id(&program->types, type_id);
    if (type && type->kind == IR_TYPE_INTEGER && type->layout.resolved && type->bit_width > 64 && type->bit_width <= 128 && type->layout.size <= 16)
    {
        *part_count = (u32)((type->layout.size + 7) / 8);
        return true;
    }
    if (type && type->kind == IR_TYPE_VA_LIST && type->layout.resolved && type->layout.size > 8 && type->layout.size <= 32 && !(type->layout.size & 7))
    {
        *part_count = (u32)(type->layout.size / 8);
        return true;
    }
    if (type && type->kind == IR_TYPE_VECTOR && type->layout.resolved && type->layout.size && type->layout.size <= 64)
    {
        *part_count = (u32)((type->layout.size + 7) / 8);
        return true;
    }
    if (!type || (type->kind != IR_TYPE_STRUCT && type->kind != IR_TYPE_UNION && type->kind != IR_TYPE_ARRAY) || !type->layout.resolved || !type->layout.size ||
        type->layout.size > (u64)UINT32_MAX * 8)
    {
        return false;
    }
    if (type->layout.size > 16)
    {
        *part_count = (u32)((type->layout.size + 7) / 8);
        return true;
    }
    TemporalArena temporary = scratch_begin(0, 0);
    IrTypeId* worklist = arena_allocate(temporary.arena, IrTypeId, program->types.count);
    bool* visited = arena_allocate(temporary.arena, bool, program->types.count);
    memset(visited, 0, sizeof(*visited) * program->types.count);
    u32 work_count = 1;
    worklist[0] = type_id;
    visited[type_id.value] = true;
    bool integer_only = true;
    while (work_count && integer_only)
    {
        IrTypeId current_id = worklist[--work_count];
        if (current_id.value >= program->types.count)
        {
            integer_only = false;
            break;
        }
        IrType* current = ir_type_from_id(&program->types, current_id);
        if (!current)
        {
            integer_only = false;
        }
        else if (current->kind == IR_TYPE_BOOLEAN || current->kind == IR_TYPE_INTEGER || current->kind == IR_TYPE_FLOAT || current->kind == IR_TYPE_POINTER ||
                 current->kind == IR_TYPE_VECTOR || current->kind == IR_TYPE_ENUM)
        {
            continue;
        }
        else if (current->kind == IR_TYPE_ARRAY)
        {
            if (current->element_type.value >= program->types.count)
            {
                integer_only = false;
                break;
            }
            visited[current->element_type.value] = true;
            worklist[work_count++] = current->element_type;
        }
        else if (current->kind == IR_TYPE_STRUCT || current->kind == IR_TYPE_UNION)
        {
            for (u32 field_index = 0; field_index < current->field_count; field_index += 1)
            {
                IrTypeId field_type = current->fields[field_index].type;
                if (field_type.value >= program->types.count)
                {
                    integer_only = false;
                    break;
                }
                if (!visited[field_type.value])
                {
                    visited[field_type.value] = true;
                    worklist[work_count++] = field_type;
                }
            }
        }
        else
        {
            integer_only = false;
        }
    }
    scratch_end(temporary);
    if (!integer_only)
    {
        return false;
    }
    *part_count = (u32)((type->layout.size + 7) / 8);
    return true;
}

// What one argument of this type wants from the outgoing area it lands in. The
// area is addressed in eightbytes, so that is the floor; a type that wants more
// -- a 256- or 512-bit vector, an `_Alignas(64)` aggregate -- is read back by a
// callee with an alignment-requiring move and has to get what it asked for.
u32 codegen_canonical_x64_stack_argument_alignment(IrType* type)
{
    u64 alignment = type && type->layout.resolved ? type->layout.alignment : 0;
    if (alignment < 8 || alignment > INT32_MAX || (alignment & (alignment - 1)))
    {
        return 8;
    }
    return (u32)alignment;
}

// Where the next argument starts. The System V convention places a stack
// argument at an address respecting its alignment rather than immediately after
// the one before it, so the gap this opens is padding the caller writes nothing
// into and the callee reads nothing out of.
BUSTER_GLOBAL_LOCAL u64 codegen_canonical_x64_stack_argument_offset(u64 cursor, u32 alignment)
{
    u64 remainder = cursor & (alignment - 1);
    return remainder ? cursor + alignment - remainder : cursor;
}

CodegenError codegen_canonical_x64_call_layout_cached(Arena* arena, IrProgram* program, CodegenCanonicalX64F80Cache const* f80_cache,
                                                       IrFunction* function, IrInstruction* instruction, CodegenAbi abi, Target target,
                                                       CodegenCanonicalCallLayout* layout)
{
    if (!layout)
    {
        return CODEGEN_ERROR_INVALID_IR;
    }
    *layout = (CodegenCanonicalCallLayout){0};
    if (!arena || !program || !function || !function->values || !instruction || instruction->opcode != IR_OPCODE_CALL || !instruction->operand_count ||
        !instruction->operands || (abi != CODEGEN_ABI_X86_64_SYSTEM_V && abi != CODEGEN_ABI_X86_64_WINDOWS) ||
        instruction->operands[0].value >= function->value_count)
    {
        return CODEGEN_ERROR_INVALID_IR;
    }
    IrType* callee_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
    if (!callee_type)
    {
        return CODEGEN_ERROR_INVALID_IR;
    }
    if (callee_type->kind == IR_TYPE_POINTER)
    {
        callee_type = ir_type_from_id(&program->types, callee_type->element_type);
        if (!callee_type)
        {
            return CODEGEN_ERROR_INVALID_IR;
        }
    }
    if (callee_type->kind != IR_TYPE_FUNCTION)
    {
        return CODEGEN_ERROR_UNSUPPORTED_ABI;
    }
    u32 argument_count = instruction->operand_count - 1;
    if ((!callee_type->is_variadic && argument_count != callee_type->parameter_count) ||
        (callee_type->is_variadic && argument_count < callee_type->parameter_count) ||
        (callee_type->parameter_count && !callee_type->parameter_types))
    {
        return CODEGEN_ERROR_INVALID_IR;
    }
    for (u32 parameter_index = 0; parameter_index < callee_type->parameter_count; parameter_index += 1)
    {
        if (!ir_type_from_id(&program->types, callee_type->parameter_types[parameter_index]))
        {
            return CODEGEN_ERROR_INVALID_IR;
        }
    }
    if (!ir_type_from_id(&program->types, instruction->canonical_type))
    {
        return CODEGEN_ERROR_INVALID_IR;
    }
    layout->argument_count = argument_count;
    layout->return_abi = codegen_canonical_aggregate_abi(program, instruction->canonical_type, abi, true, false);
    bool return_contains_f80 = codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, callee_type->return_type);
    if (return_contains_f80 && (abi != CODEGEN_ABI_X86_64_SYSTEM_V ||
                                !codegen_canonical_x64_type_is_f80_x87_shape_cached(f80_cache, program, callee_type->return_type)))
    {
        return CODEGEN_ERROR_UNSUPPORTED_ABI;
    }
    layout->indirect_return = layout->return_abi.indirect;
    layout->windows_indirect_return = abi == CODEGEN_ABI_X86_64_WINDOWS && layout->return_abi.indirect;
    layout->simulated_registers = layout->indirect_return ? 1 : 0;
    if (argument_count)
    {
        layout->arguments = arena_allocate(arena, CodegenCanonicalCallArgument, argument_count);
    }
    static u8 const system_v[] = {
        7, 6, 2, 1, 8, 9,
    };
    static u8 const windows[] = {
        1,
        2,
        8,
        9,
    };
    u32 register_count = abi == CODEGEN_ABI_X86_64_WINDOWS ? BUSTER_ARRAY_LENGTH(windows) : BUSTER_ARRAY_LENGTH(system_v);
    u64 stack_part_count = 0;
    for (u32 argument_index = 0; argument_index < argument_count; argument_index += 1)
    {
        IrValueId argument = instruction->operands[argument_index + 1];
        if (argument.value >= function->value_count)
        {
            return CODEGEN_ERROR_INVALID_IR;
        }
        IrTypeId type_id = function->values[argument.value].canonical_type;
        IrType* type = ir_type_from_id(&program->types, type_id);
        if (!type)
        {
            return CODEGEN_ERROR_INVALID_IR;
        }
        u32 part_count = 1;
        bool aggregate = codegen_canonical_integer_aggregate_parts(program, type_id, &part_count);
        CodegenCanonicalAbiValue argument_abi = codegen_canonical_aggregate_abi(program, type_id, abi, false, false);
        bool contains_f80 = codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, type_id);
        bool f80_x87_shape = codegen_canonical_x64_type_is_f80_x87_shape_cached(f80_cache, program, type_id);
        if (contains_f80 && (abi != CODEGEN_ABI_X86_64_SYSTEM_V || !f80_x87_shape))
        {
            return CODEGEN_ERROR_UNSUPPORTED_ABI;
        }
        // SysV puts both a scalar f80 and the canonical single-f80 aggregate
        // in a sixteen-byte, sixteen-aligned memory slot.  The aggregate is
        // marked here so the normal stack-copy path does not mistake it for
        // an unsupported register aggregate.
        bool f80_memory = f80_x87_shape && argument_abi.memory && type->layout.size == 16;
        if (f80_memory && (type->kind == IR_TYPE_STRUCT || type->kind == IR_TYPE_UNION || type->kind == IR_TYPE_ARRAY))
        {
            aggregate = true;
        }
        // A value the target cannot carry in the registers its classification
        // named goes on the stack, and its stack image is the eightbyte count
        // the aggregate walk already produced rather than the register count.
        bool argument_in_registers = codegen_canonical_x64_abi_value_in_registers(&argument_abi, &target);
        if (argument_abi.part_count && !argument_abi.memory && !argument_abi.indirect)
        {
            aggregate = true;
            if (argument_in_registers)
            {
                part_count = argument_abi.part_count;
            }
        }
        if (!type || ((type->kind == IR_TYPE_STRUCT || type->kind == IR_TYPE_UNION) && !aggregate))
        {
            return CODEGEN_ERROR_UNSUPPORTED_ABI;
        }
        bool windows_indirect = abi == CODEGEN_ABI_X86_64_WINDOWS && argument_abi.indirect;
        if (aggregate && abi == CODEGEN_ABI_X86_64_WINDOWS)
        {
            part_count = 1;
        }
        if (windows_indirect)
        {
            part_count = 1;
        }
        CodegenCanonicalCallArgument call_argument = {
            .abi = argument_abi,
            .type = type,
            .part_count = part_count,
            .stack_part_count = (u32)((type->layout.size + 7) / 8),
            .float_register = UINT8_MAX,
            .aggregate = aggregate,
            .windows_indirect = windows_indirect,
            .system_v_aggregate = abi == CODEGEN_ABI_X86_64_SYSTEM_V && argument_abi.part_count && !argument_abi.memory && argument_in_registers,
        };
        u64 argument_stack_parts = 0;
        if (abi == CODEGEN_ABI_X86_64_SYSTEM_V && f80_memory)
        {
            call_argument.on_stack = true;
            argument_stack_parts = call_argument.stack_part_count;
        }
        else if (abi == CODEGEN_ABI_X86_64_SYSTEM_V && type->kind == IR_TYPE_FLOAT)
        {
            if (layout->simulated_float_registers < 8)
            {
                call_argument.float_register = (u8)layout->simulated_float_registers++;
            }
            else
            {
                call_argument.on_stack = true;
                argument_stack_parts = 1;
            }
        }
        else if (call_argument.system_v_aggregate)
        {
            u32 integer_count = 0;
            u32 float_count = 0;
            for (u32 part = 0; part < argument_abi.part_count; part += 1)
            {
                if (codegen_canonical_abi_part_is_float(argument_abi.parts[part].abi_class))
                {
                    float_count += 1;
                }
                else
                {
                    integer_count += 1;
                }
            }
            if (layout->simulated_registers <= register_count && integer_count <= register_count - layout->simulated_registers &&
                layout->simulated_float_registers <= 8 && float_count <= 8 - layout->simulated_float_registers)
            {
                call_argument.float_register = (u8)layout->simulated_float_registers;
                layout->simulated_registers += integer_count;
                layout->simulated_float_registers += float_count;
            }
            else
            {
                call_argument.on_stack = true;
                argument_stack_parts = (type->layout.size + 7) / 8;
            }
        }
        else
        {
            bool system_v_memory = aggregate && abi == CODEGEN_ABI_X86_64_SYSTEM_V && type->layout.size > 16;
            if (!system_v_memory && layout->simulated_registers <= register_count && part_count <= register_count - layout->simulated_registers)
            {
                layout->simulated_registers += part_count;
            }
            else
            {
                call_argument.on_stack = true;
                argument_stack_parts = part_count;
            }
        }
        if (call_argument.on_stack)
        {
            // Windows gives every stack argument one eightbyte and passes
            // anything wider by reference, so only System V has an argument
            // whose alignment the area has to answer for.
            u32 argument_alignment = abi == CODEGEN_ABI_X86_64_SYSTEM_V
                                         ? f80_memory ? 16 : codegen_canonical_x64_stack_argument_alignment(type)
                                         : 8;
            u64 offset = codegen_canonical_x64_stack_argument_offset(stack_part_count * 8, argument_alignment);
            if (offset > UINT32_MAX || argument_stack_parts > (UINT32_MAX - offset) / 8)
            {
                return CODEGEN_ERROR_CAPACITY;
            }
            call_argument.stack_offset = (u32)offset;
            stack_part_count = (offset + argument_stack_parts * 8) / 8;
            layout->stack_alignment = BUSTER_MAX(layout->stack_alignment, argument_alignment);
        }
        if (layout->arguments)
        {
            layout->arguments[argument_index] = call_argument;
        }
    }
    layout->stack_part_count = (u32)stack_part_count;
    layout->stack_alignment = BUSTER_MAX(layout->stack_alignment, (u32)CODEGEN_X64_STACK_ALIGNMENT);
    layout->stack_padding = abi == CODEGEN_ABI_X86_64_SYSTEM_V && (layout->stack_part_count & 1) != 0;
    if (abi == CODEGEN_ABI_X86_64_WINDOWS)
    {
        u64 stack_bytes = 32 + stack_part_count * 8;
        u64 copy_cursor = stack_bytes;
        for (u32 argument_index = 0; argument_index < argument_count; argument_index += 1)
        {
            CodegenCanonicalCallArgument* call_argument = layout->arguments ? layout->arguments + argument_index : 0;
            if (!call_argument || !call_argument->windows_indirect)
            {
                continue;
            }
            u64 copy_size = call_argument->type->layout.size;
            // The same question a System V stack argument asks, with the
            // outgoing area's own floor under it: this slot is measured from
            // the stack pointer, so nothing below sixteen buys anything.
            u64 copy_alignment =
                BUSTER_MAX(codegen_canonical_x64_stack_argument_alignment(call_argument->type), (u32)CODEGEN_X64_STACK_ALIGNMENT);
            if (!call_argument->type->layout.resolved || !copy_size || copy_size > UINT32_MAX)
            {
                return CODEGEN_ERROR_INVALID_IR;
            }
            // The slot starts sixteen-aligned like the stack pointer it is
            // measured from; a wider argument -- a 512-bit vector wants sixty
            // four -- is rounded up to its own alignment at the call, so the
            // reserve carries the bytes that round-up can consume.
            u64 copy_slack = copy_alignment - CODEGEN_X64_STACK_ALIGNMENT;
            u64 remainder = copy_cursor & (CODEGEN_X64_STACK_ALIGNMENT - 1);
            if (remainder)
            {
                copy_cursor += CODEGEN_X64_STACK_ALIGNMENT - remainder;
            }
            if (copy_cursor > UINT32_MAX || copy_size > UINT32_MAX - copy_cursor || copy_slack > UINT32_MAX - copy_cursor - copy_size)
            {
                return CODEGEN_ERROR_CAPACITY;
            }
            if (call_argument)
            {
                call_argument->copy_offset = (u32)copy_cursor;
                call_argument->copy_size = (u32)copy_size;
                call_argument->copy_alignment = (u32)copy_alignment;
            }
            copy_cursor += copy_size + copy_slack;
        }
        if (copy_cursor > UINT32_MAX - 15)
        {
            return CODEGEN_ERROR_CAPACITY;
        }
        layout->windows_stack_size = (u32)((copy_cursor + 15) & ~(u64)15);
        if (layout->windows_stack_size > INT32_MAX)
        {
            return CODEGEN_ERROR_CAPACITY;
        }
        layout->windows_copy_storage_size = (u32)(copy_cursor - stack_bytes);
    }
    return CODEGEN_ERROR_NONE;
}

CodegenError codegen_canonical_x64_call_layout(Arena* arena, IrProgram* program, IrFunction* function, IrInstruction* instruction,
                                               CodegenAbi abi, Target target, CodegenCanonicalCallLayout* layout)
{
    CodegenCanonicalX64F80Cache cache = codegen_canonical_x64_f80_cache_initialize(arena, program);
    if (cache.allocation_failed)
    {
        return CODEGEN_ERROR_CAPACITY;
    }
    return codegen_canonical_x64_call_layout_cached(arena, program, &cache, function, instruction, abi, target, layout);
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_a64_adjust_stack_described(CodegenBuffer* buffer, u32 byte_count, bool subtract,
                                                                      CodegenFunctionDescriptor* descriptor, u32 action_capacity, bool windows)
{
    if (windows && a64_emit_windows_large_stack_adjust(buffer, byte_count, subtract, descriptor, action_capacity))
    {
        return;
    }
    while (byte_count)
    {
        u32 chunk = BUSTER_MIN(byte_count, A64_SP_ADJUST_CHUNK);
        codegen_emit_u32(buffer, (subtract ? 0xd10003ff : 0x910003ff) | (chunk << 10));
        if (subtract && descriptor &&
            !codegen_unwind_action_append(descriptor, action_capacity, (u32)buffer->count - descriptor->code_offset,
                                          CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, chunk))
        {
            buffer->error = CODEGEN_ERROR_CAPACITY;
            return;
        }
        if (subtract)
        {
            codegen_emit_u32(buffer, 0xf90003ff);
            if (windows && descriptor &&
                !codegen_unwind_action_append(descriptor, action_capacity, (u32)buffer->count - descriptor->code_offset, CODEGEN_UNWIND_ACTION_NOP, 0, 0))
            {
                buffer->error = CODEGEN_ERROR_CAPACITY;
                return;
            }
        }
        byte_count -= chunk;
    }
}

void codegen_canonical_a64_adjust_stack(CodegenBuffer* buffer, u32 byte_count, bool subtract)
{
    codegen_canonical_a64_adjust_stack_described(buffer, byte_count, subtract, 0, 0, false);
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_adjust_stack_described(CodegenBuffer* buffer, u32 byte_count, bool subtract,
                                                                      CodegenFunctionDescriptor* descriptor, u32 action_capacity, bool windows)
{
    if (!subtract)
    {
        if (!byte_count)
        {
            return;
        }
        BusterX86MetadataPhysicalOperand operands[2] = {
            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSP, 64),
            codegen_canonical_x64_metadata_immediate(byte_count, byte_count <= INT8_MAX ? 8 : 32),
        };
        (void)codegen_canonical_x64_metadata_emit(buffer, S8("ADD"), operands, BUSTER_ARRAY_LENGTH(operands));
        return;
    }
    if (windows && x64_emit_windows_stack_allocate(buffer, byte_count, descriptor, action_capacity, descriptor ? descriptor->code_offset : 0))
    {
        return;
    }
    while (byte_count)
    {
        u32 chunk = BUSTER_MIN(byte_count, CODEGEN_X64_STACK_PROBE_PAGE);
        BusterX86MetadataPhysicalOperand subtract_operands[2] = {
            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSP, 64),
            codegen_canonical_x64_metadata_immediate(chunk, chunk <= INT8_MAX ? 8 : 32),
        };
        (void)codegen_canonical_x64_metadata_emit(buffer, S8("SUB"), subtract_operands, BUSTER_ARRAY_LENGTH(subtract_operands));
        if (descriptor && !codegen_unwind_action_append(descriptor, action_capacity, (u32)buffer->count - descriptor->code_offset,
                                                        CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, chunk))
        {
            buffer->error = CODEGEN_ERROR_CAPACITY;
            return;
        }
        BusterX86MetadataPhysicalOperand probe_operands[2] = {
            codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RSP, 8, 0),
            codegen_canonical_x64_metadata_immediate(0, 8),
        };
        (void)codegen_canonical_x64_metadata_emit(buffer, S8("TEST"), probe_operands, BUSTER_ARRAY_LENGTH(probe_operands));
        byte_count -= chunk;
    }
}

void codegen_canonical_x64_adjust_stack(CodegenBuffer* buffer, u32 byte_count, bool subtract)
{
    codegen_canonical_x64_adjust_stack_described(buffer, byte_count, subtract, 0, 0, false);
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_emit_return(CodegenBuffer* buffer, u32 frame_size, CodegenAbi abi, bool dynamic_stack)
{
    if (abi == CODEGEN_ABI_X86_64_WINDOWS)
    {
        if (dynamic_stack)
        {
            BusterX86MetadataPhysicalOperand lea_operands[2] = {
                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSP, 64),
                codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, frame_size),
            };
            (void)codegen_canonical_x64_metadata_emit(buffer, S8("LEA"), lea_operands, BUSTER_ARRAY_LENGTH(lea_operands));
        }
        else if (frame_size)
        {
            BusterX86MetadataPhysicalOperand add_operands[2] = {
                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSP, 64),
                codegen_canonical_x64_metadata_immediate(frame_size, frame_size <= INT8_MAX ? 8 : 32),
            };
            (void)codegen_canonical_x64_metadata_emit(buffer, S8("ADD"), add_operands, BUSTER_ARRAY_LENGTH(add_operands));
        }
        (void)codegen_canonical_x64_metadata_emit(buffer, S8("POP"), &(BusterX86MetadataPhysicalOperand){
                                                        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
                                                        .width = 64,
                                                        .reg = {.index = X64_REGISTER_RBP, .width = 64, .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_GPR},
                                                    },
                                                   1);
        (void)codegen_canonical_x64_metadata_emit(buffer, S8("RET"), 0, 0);
        return;
    }
    (void)codegen_canonical_x64_metadata_emit(buffer, S8("LEAVE"), 0, 0);
    (void)codegen_canonical_x64_metadata_emit(buffer, S8("RET"), 0, 0);
}

void codegen_canonical_a64_base_address(CodegenBuffer* buffer, u32 register_number, u32 base_register, u32 byte_offset)
{
    if (byte_offset <= A64_IMM12_MAX)
    {
        codegen_emit_u32(buffer, 0x91000000 | (byte_offset << 10) | (base_register << 5) | register_number);
        return;
    }
    u32 offset_register = register_number == base_register ? (register_number == 16 ? 17 : 16) : register_number;
    a64_emit_constant(buffer, offset_register, byte_offset);
    if (base_register == 31)
    {
        u32 stack_register = register_number == 16 || offset_register == 16 ? 17 : 16;
        codegen_emit_u32(buffer, 0x910003e0 | stack_register);
        base_register = stack_register;
    }
    codegen_emit_u32(buffer, 0x8b000000 | (offset_register << 16) | (base_register << 5) | register_number);
}

// EVEX encoding for the target-fixed 512-bit vocabulary. Everything here is
// L'L=10 (512-bit), never broadcasts, and never reaches the extended register
// halves, so the three prefix payload bytes reduce to a handful of fields.
typedef struct X64Evex X64Evex;
struct X64Evex
{
    u8 map;     // 1 = 0F, 2 = 0F38, 3 = 0F3A
    u8 prefix;  // 0 = none, 1 = 66, 2 = F3, 3 = F2
    u8 opcode;
    u8 reg;     // reg field: a zmm, a k register, or an opcode extension
    u8 vvvv;    // the encoded non-destructive source, 0 when the form has none
    u8 mask;    // k1..k7, or 0 for an unmasked operation
    bool zeroing;
    bool wide;  // EVEX.W — every operation in this vocabulary is W0 today
};

BUSTER_GLOBAL_LOCAL BusterX86MetadataFeatureInput codegen_canonical_x64_evex_features(void)
{
    // The SIMD vocabulary is selected only after the target gate above has
    // established these capabilities.  Supplying the complete vocabulary to
    // metadata keeps the bridge fail-closed while allowing each row to check
    // its own required subset (for example VBMI2 for VPCOMPRESSB).
    static String8 features[] = {S8_INITIALIZER("avx512f"), S8_INITIALIZER("avx512bw"), S8_INITIALIZER("avx512vbmi"), S8_INITIALIZER("avx512vbmi2")};
    return (BusterX86MetadataFeatureInput){.names = features, .count = BUSTER_ARRAY_LENGTH(features)};
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand codegen_canonical_x64_metadata_mask(u32 register_index)
{
    return (BusterX86MetadataPhysicalOperand){
        .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
        .width = 64,
        .reg = {
            .index = (u16)register_index,
            .width = 64,
            .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_MASK,
        },
    };
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalOperand codegen_canonical_x64_metadata_evex_memory(X64Register base, u16 element_width,
                                                                                                  u16 source_width, s64 displacement)
{
    BusterX86MetadataPhysicalOperand result = codegen_canonical_x64_metadata_memory(base, element_width, displacement);
    result.memory.source_width = source_width;
    return result;
}

BUSTER_GLOBAL_LOCAL BusterX86MetadataPhysicalAttributes codegen_canonical_x64_evex_attributes(X64Evex evex)
{
    BusterX86MetadataPhysicalAttributes attributes = {0};
    if (evex.mask)
    {
        attributes.decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK |
                                     (evex.zeroing ? BUSTER_X86_METADATA_DECORATOR_ZEROING : 0);
        attributes.has_mask_register = true;
        attributes.mask_register = evex.mask;
        attributes.zeroing = evex.zeroing;
        if (evex.zeroing)
        {
            attributes.decorator_flags |= BUSTER_X86_METADATA_DECORATOR_ZEROING;
        }
    }
    return attributes;
}

// The descriptor is intentionally kept at the call sites so the vocabulary
// remains easy to audit.  It now only chooses a checked metadata shape; the
// metadata encoder owns prefix, ModRM/SIB, displacement, and register bits.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_evex_frame(CodegenBuffer* buffer, X64Evex evex, s32 displacement)
{
    BusterX86MetadataPhysicalOperand operands[3] = {0};
    u32 operand_count = 0;
    String8 mnemonic = {0};
    u16 memory_width = 8;
    BusterX86MetadataPhysicalOperand memory = codegen_canonical_x64_metadata_evex_memory(X64_REGISTER_RBP, memory_width, 512, displacement);
    switch (((u32)evex.map << 16) | ((u32)evex.prefix << 8) | evex.opcode)
    {
    case (1u << 16) | (3u << 8) | 0x6f: // VMOVDQU8 load
        mnemonic = S8("VMOVDQU8");
        operands[0] = codegen_canonical_x64_metadata_vector(evex.reg, 512);
        operands[1] = memory;
        operand_count = 2;
        break;
    case (1u << 16) | (3u << 8) | 0x7f: // VMOVDQU8 store
        mnemonic = S8("VMOVDQU8");
        operands[0] = memory;
        operands[1] = codegen_canonical_x64_metadata_vector(evex.reg, 512);
        operand_count = 2;
        break;
    case (2u << 16) | (1u << 8) | 0x63: // VPCOMPRESSB store
        mnemonic = S8("VPCOMPRESSB");
        operands[0] = memory;
        operands[1] = codegen_canonical_x64_metadata_vector(evex.reg, 512);
        operand_count = 2;
        break;
    case (1u << 16) | (1u << 8) | 0x74: // VPCMPEQB
        mnemonic = S8("VPCMPEQB");
        operands[0] = codegen_canonical_x64_metadata_mask(evex.reg);
        operands[1] = codegen_canonical_x64_metadata_vector(evex.vvvv, 512);
        operands[2] = memory;
        operand_count = 3;
        break;
    case (3u << 16) | (1u << 8) | 0x3e: // VPCMPUB (predicate is emitted by its caller)
        mnemonic = S8("VPCMPUB");
        operands[0] = codegen_canonical_x64_metadata_mask(evex.reg);
        operands[1] = codegen_canonical_x64_metadata_vector(evex.vvvv, 512);
        operands[2] = memory;
        operand_count = 3;
        break;
    case (2u << 16) | (1u << 8) | 0x26: // VPTESTMB
        mnemonic = S8("VPTESTMB");
        operands[0] = codegen_canonical_x64_metadata_mask(evex.reg);
        operands[1] = codegen_canonical_x64_metadata_vector(evex.vvvv, 512);
        operands[2] = memory;
        operand_count = 3;
        break;
    case (2u << 16) | (1u << 8) | 0x7d: // VPERMT2B
        mnemonic = S8("VPERMT2B");
        operands[0] = codegen_canonical_x64_metadata_vector(evex.reg, 512);
        operands[1] = codegen_canonical_x64_metadata_vector(evex.vvvv, 512);
        operands[2] = memory;
        operand_count = 3;
        break;
    case (2u << 16) | (1u << 8) | 0x31: // VPMOVZXBD
        mnemonic = S8("VPMOVZXBD");
        memory.width = 8;
        memory.memory.source_width = 128;
        operands[0] = codegen_canonical_x64_metadata_vector(evex.reg, 512);
        operands[1] = memory;
        operand_count = 2;
        break;
    case (1u << 16) | (1u << 8) | 0x72: // VPSLLD has a caller-emitted immediate
    case (3u << 16) | (1u << 8) | 0x25: // VPTERNLOGD has a caller-emitted immediate
        // These forms carry an immediate byte after ModRM.  Their producers
        // are migrated separately with the immediate in the metadata query;
        // accepting a partial instruction here would violate fail-closed.
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return false;
    default:
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return false;
    }
    return codegen_canonical_x64_metadata_emit_attributes(buffer, mnemonic, operands, operand_count,
                                                           codegen_canonical_x64_evex_features(),
                                                           codegen_canonical_x64_evex_attributes(evex));
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_evex_register(CodegenBuffer* buffer, X64Evex evex, u8 rm)
{
    BusterX86MetadataPhysicalOperand operands[2] = {0};
    String8 mnemonic = {0};
    switch (((u32)evex.map << 16) | ((u32)evex.prefix << 8) | evex.opcode)
    {
    case (2u << 16) | (1u << 8) | 0x63: // VPCOMPRESSB zmm/mask
        mnemonic = S8("VPCOMPRESSB");
        operands[0] = codegen_canonical_x64_metadata_vector(rm, 512);
        operands[1] = codegen_canonical_x64_metadata_vector(evex.reg, 512);
        break;
    case (2u << 16) | (1u << 8) | 0x7a: // VPBROADCASTB zmm, r32
        mnemonic = S8("VPBROADCASTB");
        operands[0] = codegen_canonical_x64_metadata_vector(evex.reg, 512);
        operands[1] = codegen_canonical_x64_metadata_gpr((X64Register)rm, 32);
        break;
    case (2u << 16) | (2u << 8) | 0x29: // VPMOVB2M k, zmm
        mnemonic = S8("VPMOVB2M");
        operands[0] = codegen_canonical_x64_metadata_mask(evex.reg);
        operands[1] = codegen_canonical_x64_metadata_vector(rm, 512);
        break;
    default:
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return false;
    }
    return codegen_canonical_x64_metadata_emit_attributes(buffer, mnemonic, operands, 2,
                                                           codegen_canonical_x64_evex_features(),
                                                           codegen_canonical_x64_evex_attributes(evex));
}

// KMOVQ moves a whole 64-lane mask between a k register and a frame slot in
// one instruction, so a mask never needs a general-purpose register on the way
// through memory. VEX.L0.W1 0F 90 loads, 91 stores.
// Moves one ABI part between an SSE/AVX register and a frame slot. This is the
// only thing that decides which part sizes the canonical ABI can carry in a
// vector register — every caller reports CODEGEN_ERROR_UNSUPPORTED_ABI on a
// false return rather than repeating the size test, so the two cannot drift.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_float_memory(CodegenBuffer* buffer, Target target, u32 vector_register, s32 displacement, u32 size, bool store)
{
    if (vector_register >= 8 || (size != 4 && size != 8 && size != 16 && size != 32 && size != 64))
    {
        return false;
    }
    // A part only travels in a vector register if the target has one that
    // wide. The IR ABI classifies a vector by the psABI rule alone, which is
    // right for a target that can hold it and would otherwise have us encode
    // a zmm move for a machine with no zmm.
    if (size > 16 && size > target_vector_register_size(target))
    {
        return false;
    }
    u16 vector_width = 0;
    u16 memory_width = (u16)(size * 8);
    String8 mnemonic = {0};
    String8 feature_names[2] = {0};
    u32 feature_count = 0;
    if (size == 64)
    {
        vector_width = 512;
        mnemonic = S8("VMOVDQU8");
        feature_names[0] = S8("avx512f");
        feature_names[1] = S8("avx512bw");
        feature_count = 2;
    }
    else if (size == 32)
    {
        vector_width = 256;
        mnemonic = S8("VMOVDQU");
        feature_names[0] = S8("avx2");
        feature_count = 1;
    }
    else if (size == 16)
    {
        vector_width = 128;
        mnemonic = S8("MOVDQU");
        feature_names[0] = S8("sse2");
        feature_count = 1;
    }
    else if (size == 8)
    {
        vector_width = 128;
        memory_width = 64;
        mnemonic = S8("MOVSD");
        feature_names[0] = S8("sse2");
        feature_count = 1;
    }
    else
    {
        vector_width = 128;
        memory_width = 32;
        mnemonic = S8("MOVSS");
        feature_names[0] = S8("sse");
        feature_count = 1;
    }
    BusterX86MetadataPhysicalOperand memory = size == 64
                                                  ? codegen_canonical_x64_metadata_evex_memory(X64_REGISTER_RBP, 8, 512, displacement)
                                                  : codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, memory_width, displacement);
    BusterX86MetadataPhysicalOperand vector = codegen_canonical_x64_metadata_vector(vector_register, vector_width);
    BusterX86MetadataPhysicalOperand operands[2] = {store ? memory : vector, store ? vector : memory};
    return codegen_canonical_x64_metadata_emit_features(buffer, mnemonic, operands, BUSTER_ARRAY_LENGTH(operands),
                                                         (BusterX86MetadataFeatureInput){.names = feature_names, .count = feature_count});
}

// Raw x87 memory forms keep the backend independent of the host C ABI.  A
// disp32 addressing form is used even for zero offsets: it makes RBP/R13 and
// RSP/R12 unambiguous and keeps every frame access the same fixed shape.
void codegen_canonical_x64_x87_memory(CodegenBuffer* buffer, bool store, X64Register base, s32 displacement)
{
    BusterX86MetadataPhysicalOperand operand = codegen_canonical_x64_metadata_memory(base, 80, displacement);
    String8 features[] = {S8("sse2")};
    (void)codegen_canonical_x64_metadata_emit_features(buffer, store ? S8("FSTP") : S8("FLD"), &operand, 1,
                                                        (BusterX86MetadataFeatureInput){.names = features, .count = BUSTER_ARRAY_LENGTH(features)});
}

void codegen_canonical_x64_zero_f80_padding(CodegenBuffer* buffer, X64Register base, s32 displacement)
{
    // xor scratch,scratch; [base+10] = scratch.w; [base+12] = scratch.d.
    // Keep RAX intact when it is the destination pointer: the indirect store
    // path deliberately holds its address there.  The two stores avoid an
    // unaligned eight-byte access while covering exactly the six padding
    // bytes following the x87 ten-byte semantic value.
    X64Register zero_register = base == X64_REGISTER_RAX ? X64_REGISTER_RCX : X64_REGISTER_RAX;
    BusterX86MetadataPhysicalOperand zero32 = codegen_canonical_x64_metadata_gpr(zero_register, 32);
    BusterX86MetadataPhysicalOperand zero16 = codegen_canonical_x64_metadata_gpr(zero_register, 16);
    BusterX86MetadataPhysicalOperand xor_operands[2] = {zero32, zero32};
    BusterX86MetadataPhysicalOperand word_store[2] = {
        codegen_canonical_x64_metadata_memory(base, 16, displacement + 10), zero16};
    BusterX86MetadataPhysicalOperand dword_store[2] = {
        codegen_canonical_x64_metadata_memory(base, 32, displacement + 12), zero32};
    (void)codegen_canonical_x64_metadata_emit(buffer, S8("XOR"), xor_operands, BUSTER_ARRAY_LENGTH(xor_operands));
    (void)codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), word_store, BUSTER_ARRAY_LENGTH(word_store));
    (void)codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), dword_store, BUSTER_ARRAY_LENGTH(dword_store));
}

bool codegen_canonical_x64_emit_f80_copy(CodegenBuffer* buffer, X64Register source_base, s32 source_displacement,
                                         X64Register destination_base, s32 destination_displacement, u32* x87_depth)
{
    // Every copy is a bounded one-entry x87 stack transaction.  Keeping the
    // depth explicit catches an accidental future path that would leave a
    // value live across a call or a branch.
    if (!x87_depth || *x87_depth >= 8)
    {
        return false;
    }
    codegen_canonical_x64_x87_memory(buffer, false, source_base, source_displacement);
    *x87_depth += 1;
    codegen_canonical_x64_x87_memory(buffer, true, destination_base, destination_displacement);
    *x87_depth -= 1;
    codegen_canonical_x64_zero_f80_padding(buffer, destination_base, destination_displacement);
    return buffer->error == CODEGEN_ERROR_NONE;
}

bool codegen_canonical_x64_emit_f80_store_top(CodegenBuffer* buffer, X64Register destination_base, s32 destination_displacement,
                                              u32* x87_depth)
{
    if (!x87_depth || *x87_depth == 0)
    {
        return false;
    }
    codegen_canonical_x64_x87_memory(buffer, true, destination_base, destination_displacement);
    *x87_depth -= 1;
    codegen_canonical_x64_zero_f80_padding(buffer, destination_base, destination_displacement);
    return buffer->error == CODEGEN_ERROR_NONE;
}

bool codegen_canonical_x64_store_f80_constant(CodegenBuffer* buffer, s32 displacement, u64 significand, u16 sign_exponent)
{
    BusterX86MetadataPhysicalOperand rax64 = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64);
    BusterX86MetadataPhysicalOperand rax32 = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32);
    BusterX86MetadataPhysicalOperand rax16 = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 16);
    BusterX86MetadataPhysicalOperand significand_operand = codegen_canonical_x64_metadata_unsigned_immediate(significand, 64);
    BusterX86MetadataPhysicalOperand exponent_operand = codegen_canonical_x64_metadata_unsigned_immediate(sign_exponent, 32);
    BusterX86MetadataPhysicalOperand load_significand[2] = {rax64, significand_operand};
    BusterX86MetadataPhysicalOperand store_significand[2] = {
        codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, displacement), rax64};
    BusterX86MetadataPhysicalOperand load_exponent[2] = {rax32, exponent_operand};
    BusterX86MetadataPhysicalOperand store_exponent[2] = {
        codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 16, displacement + 8), rax16};
    if (!codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), load_significand, BUSTER_ARRAY_LENGTH(load_significand)) ||
        !codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), store_significand, BUSTER_ARRAY_LENGTH(store_significand)) ||
        !codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), load_exponent, BUSTER_ARRAY_LENGTH(load_exponent)) ||
        !codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), store_exponent, BUSTER_ARRAY_LENGTH(store_exponent)))
    {
        return false;
    }
    codegen_canonical_x64_zero_f80_padding(buffer, X64_REGISTER_RBP, displacement);
    return buffer->error == CODEGEN_ERROR_NONE;
}

BUSTER_GLOBAL_LOCAL String8 codegen_global_assembly_trim(String8 value)
{
    while (value.length && (value.pointer[0] == ' ' || value.pointer[0] == '\t' || value.pointer[0] == '\r'))
    {
        value.pointer += 1;
        value.length -= 1;
    }
    while (value.length && (value.pointer[value.length - 1] == ' ' || value.pointer[value.length - 1] == '\t' || value.pointer[value.length - 1] == '\r'))
    {
        value.length -= 1;
    }
    return value;
}

BUSTER_GLOBAL_LOCAL bool codegen_global_assembly_unsigned(String8 value, u64* result)
{
    value = codegen_global_assembly_trim(value);
    if (!value.length)
    {
        return false;
    }
    u32 base = 10;
    u64 index = 0;
    if (value.length > 2 && value.pointer[0] == '0' && (value.pointer[1] == 'x' || value.pointer[1] == 'X'))
    {
        base = 16;
        index = 2;
    }
    u64 number = 0;
    for (; index < value.length; index += 1)
    {
        char8 character = value.pointer[index];
        u32 digit = character >= '0' && character <= '9'   ? (u32)(character - '0')
                    : character >= 'a' && character <= 'f' ? (u32)(character - 'a') + 10
                    : character >= 'A' && character <= 'F' ? (u32)(character - 'A') + 10
                                                           : UINT32_MAX;
        if (digit >= base || number > (UINT64_MAX - digit) / base)
        {
            return false;
        }
        number = number * base + digit;
    }
    *result = number;
    return true;
}

// Prices one `.p2align` directive from the text following the directive name.
// Both the emitter and the module code buffer's reserve go through here, so the
// exponent's spelling and its accepted range cannot drift apart and leave a
// directive demanding more padding than was reserved for it.
BUSTER_GLOBAL_LOCAL bool codegen_global_assembly_alignment(String8 operand, u64* alignment)
{
    u64 exponent = 0;
    if (!codegen_global_assembly_unsigned(operand, &exponent) || exponent > 12)
    {
        return false;
    }
    *alignment = UINT64_C(1) << exponent;
    return true;
}

// Upper bound on the padding a source's `.p2align` directives can demand: each
// one pads to its own boundary, so it can ask for one byte less than its
// alignment. The scan matches the directive spelling anywhere in the source
// instead of re-walking lines the way the emitter does — a match inside a
// comment only over-reserves, and no directive the emitter would honor can
// escape it. Assembly with no alignment directives is charged nothing.
BUSTER_GLOBAL_LOCAL u64 codegen_global_assembly_alignment_padding(String8 source)
{
    String8 directive = S8(".p2align");
    u64 padding = 0;
    u64 index = 0;
    while (index + directive.length <= source.length)
    {
        if (source.pointer[index] != '.' || memcmp(source.pointer + index, directive.pointer, directive.length) != 0)
        {
            index += 1;
            continue;
        }
        u64 operand = index + directive.length;
        u64 line_end = operand;
        while (line_end < source.length && source.pointer[line_end] != '\n')
        {
            line_end += 1;
        }
        u64 alignment = 0;
        // The emitter reads at most one directive per line, so charging the
        // line once and resuming after it cannot miss padding.
        if (codegen_global_assembly_alignment(
                (String8){
                    .pointer = source.pointer + operand,
                    .length = line_end - operand,
                },
                &alignment))
        {
            padding += alignment - 1;
        }
        index = line_end + 1;
    }
    return padding;
}

BUSTER_GLOBAL_LOCAL IrSymbolId codegen_global_assembly_symbol(IrProgram* program, String8 name, Target target)
{
    String8 alternate = name;
    if ((target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS) && alternate.length && alternate.pointer[0] == '_')
    {
        alternate.pointer += 1;
        alternate.length -= 1;
    }
    for (u32 symbol_index = 0; symbol_index < program->symbols.count; symbol_index += 1)
    {
        IrSymbol* symbol = &program->symbols.symbols[symbol_index];
        String8 link_name = symbol->link_name.length ? symbol->link_name : symbol->name;
        if (symbol->kind == IR_SYMBOL_FUNCTION && (string_equal(link_name, name) || string_equal(link_name, alternate)))
        {
            return (IrSymbolId){
                .value = symbol_index,
            };
        }
    }
    return IR_SYMBOL_ID_INVALID;
}

BUSTER_GLOBAL_LOCAL bool codegen_emit_global_assembly(Arena* arena, IrProgram* program, IrModuleAssembly assembly, Target target, CodegenBuffer* buffer,
                                                      CodegenModule* result)
{
    u64 line_start = 0;
    while (line_start < assembly.source.length)
    {
        u64 line_end = line_start;
        while (line_end < assembly.source.length && assembly.source.pointer[line_end] != '\n')
        {
            line_end += 1;
        }
        String8 line = codegen_global_assembly_trim((String8){
            .pointer = assembly.source.pointer + line_start,
            .length = line_end - line_start,
        });
        line_start = line_end < assembly.source.length ? line_end + 1 : assembly.source.length;
        if (!line.length || line.pointer[0] == '#')
        {
            continue;
        }
        for (;;)
        {
            u64 colon = UINT64_MAX;
            for (u64 index = 0; index < line.length; index += 1)
            {
                if (line.pointer[index] == ':')
                {
                    colon = index;
                    break;
                }
            }
            if (colon == UINT64_MAX)
            {
                break;
            }
            String8 name = codegen_global_assembly_trim((String8){
                .pointer = line.pointer,
                .length = colon,
            });
            IrSymbolId symbol = codegen_global_assembly_symbol(program, name, target);
            if (symbol.value == IR_ID_UNDERLYING_INVALID)
            {
                return false;
            }
            program->symbols.symbols[symbol.value].is_definition = true;
            result->entries[result->entry_count++] = (CodegenModuleEntry){
                .symbol = symbol,
                .offset = (u32)buffer->count,
            };
            line.pointer += colon + 1;
            line.length -= colon + 1;
            line = codegen_global_assembly_trim(line);
            if (!line.length)
            {
                break;
            }
        }
        if (!line.length)
        {
            continue;
        }
        if (line.pointer[0] == '.')
        {
            if ((line.length >= 5 && memcmp(line.pointer, ".byte", 5) == 0))
            {
                String8 values = {
                    .pointer = line.pointer + 5,
                    .length = line.length - 5,
                };
                while (values.length)
                {
                    u64 comma = values.length;
                    for (u64 index = 0; index < values.length; index += 1)
                    {
                        if (values.pointer[index] == ',')
                        {
                            comma = index;
                            break;
                        }
                    }
                    u64 value = 0;
                    if (!codegen_global_assembly_unsigned(
                            (String8){
                                .pointer = values.pointer,
                                .length = comma,
                            },
                            &value) ||
                        value > UINT8_MAX)
                    {
                        return false;
                    }
                    codegen_emit_u8(buffer, (u8)value);
                    if (comma == values.length)
                    {
                        break;
                    }
                    values.pointer += comma + 1;
                    values.length -= comma + 1;
                }
                continue;
            }
            if (line.length >= 8 && memcmp(line.pointer, ".p2align", 8) == 0)
            {
                u64 alignment = 0;
                if (!codegen_global_assembly_alignment(
                        (String8){
                            .pointer = line.pointer + 8,
                            .length = line.length - 8,
                        },
                        &alignment))
                {
                    return false;
                }
                // The emit helpers refuse a byte the buffer cannot hold without
                // advancing its count, so a reserve too small for this padding
                // has to end the loop here instead of asking forever.
                while ((buffer->count & (alignment - 1)) && buffer->error == CODEGEN_ERROR_NONE)
                {
                    if (target.cpu_arch == CPU_ARCH_X86_64)
                    {
                        if (!codegen_canonical_x64_metadata_emit(buffer, S8("NOP"), 0, 0))
                        {
                            return false;
                        }
                    }
                    else
                    {
                        if (buffer->count & 3)
                        {
                            return false;
                        }
                        codegen_emit_u32(buffer, 0xd503201f);
                    }
                }
                continue;
            }
            bool recognized = (line.length >= 5 && memcmp(line.pointer, ".text", 5) == 0) || (line.length >= 6 && memcmp(line.pointer, ".globl", 6) == 0) ||
                              (line.length >= 7 && memcmp(line.pointer, ".global", 7) == 0) || (line.length >= 5 && memcmp(line.pointer, ".type", 5) == 0) ||
                              (line.length >= 5 && memcmp(line.pointer, ".size", 5) == 0);
            if (!recognized)
            {
                return false;
            }
            continue;
        }
        char8* normalized = arena_allocate(arena, char8, line.length);
        u64 normalized_length = 0;
        for (u64 index = 0; index < line.length; index += 1)
        {
            if (line.pointer[index] != ' ' && line.pointer[index] != '\t')
            {
                normalized[normalized_length++] = line.pointer[index];
            }
        }
        String8 instruction = {
            .pointer = normalized,
            .length = normalized_length,
        };
        if (target.cpu_arch == CPU_ARCH_X86_64)
        {
            if (string_equal(instruction, S8("ret")) || string_equal(instruction, S8("retq")))
            {
                if (!codegen_canonical_x64_metadata_emit(buffer, S8("RET"), 0, 0))
                {
                    return false;
                }
            }
            else if (string_equal(instruction, S8("nop")))
            {
                if (!codegen_canonical_x64_metadata_emit(buffer, S8("NOP"), 0, 0))
                {
                    return false;
                }
            }
            else if (string_equal(instruction, S8("ud2")))
            {
                if (!codegen_canonical_x64_metadata_emit(buffer, S8("UD2"), 0, 0))
                {
                    return false;
                }
            }
            else if (string_equal(instruction, S8("pause")))
            {
                AssemblyEncodeResult encoded = assembly_encode(arena, instruction,
                                                                (AssemblyEncodeOptions){.target = target, .syntax = ASSEMBLY_SYNTAX_ATT});
                if (encoded.diagnostic_count || encoded.relocation_count)
                {
                    return false;
                }
                for (u32 symbol_index = 0; symbol_index < encoded.symbol_count; symbol_index += 1)
                {
                    if (!encoded.symbols[symbol_index].defined)
                    {
                        return false;
                    }
                }
                u8* encoded_bytes = 0;
                if (!codegen_buffer_reserve(buffer, encoded.bytes.length, &encoded_bytes))
                {
                    return false;
                }
                if (encoded.bytes.length)
                {
                    memcpy(encoded_bytes, encoded.bytes.pointer, encoded.bytes.length);
                }
            }
            else
            {
                String8 prefixes[] = {
                    S8("mov$"),
                    S8("movl$"),
                };
                bool emitted = false;
                for (u32 prefix_index = 0; prefix_index < BUSTER_ARRAY_LENGTH(prefixes); prefix_index += 1)
                {
                    String8 prefix = prefixes[prefix_index];
                    String8 suffix = S8(",%eax");
                    if (instruction.length <= prefix.length + suffix.length || memcmp(instruction.pointer, prefix.pointer, prefix.length) != 0 ||
                        memcmp(instruction.pointer + instruction.length - suffix.length, suffix.pointer, suffix.length) != 0)
                    {
                        continue;
                    }
                    u64 immediate = 0;
                    if (!codegen_global_assembly_unsigned(
                            (String8){
                                .pointer = instruction.pointer + prefix.length,
                                .length = instruction.length - prefix.length - suffix.length,
                            },
                            &immediate) ||
                        immediate > UINT32_MAX)
                    {
                        return false;
                    }
                    BusterX86MetadataPhysicalOperand operands[2] = {
                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                        codegen_canonical_x64_metadata_unsigned_immediate(immediate, 32),
                    };
                    if (!codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), operands, BUSTER_ARRAY_LENGTH(operands)))
                    {
                        return false;
                    }
                    emitted = true;
                    break;
                }
                if (!emitted)
                {
                    return false;
                }
            }
        }
        else
        {
            if (string_equal(instruction, S8("ret")))
            {
                codegen_emit_u32(buffer, 0xd65f03c0);
            }
            else if (string_equal(instruction, S8("nop")))
            {
                codegen_emit_u32(buffer, 0xd503201f);
            }
            else if (string_equal(instruction, S8("brk#0")))
            {
                codegen_emit_u32(buffer, 0xd4200000);
            }
            else
            {
                String8 prefix = S8("movw0,#");
                if (instruction.length <= prefix.length || memcmp(instruction.pointer, prefix.pointer, prefix.length) != 0)
                {
                    return false;
                }
                u64 immediate = 0;
                if (!codegen_global_assembly_unsigned(
                        (String8){
                            .pointer = instruction.pointer + prefix.length,
                            .length = instruction.length - prefix.length,
                        },
                        &immediate) ||
                    immediate > UINT16_MAX)
                {
                    return false;
                }
                codegen_emit_u32(buffer, 0x52800000 | ((u32)immediate << 5));
            }
        }
        if (buffer->error != CODEGEN_ERROR_NONE)
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_address(CodegenBuffer* buffer, X64Register target, s32 displacement)
{
    BusterX86MetadataPhysicalOperand operands[2] = {
        codegen_canonical_x64_metadata_gpr(target, 64),
        codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, displacement),
    };
    (void)codegen_canonical_x64_metadata_emit(buffer, S8("LEA"), operands, BUSTER_ARRAY_LENGTH(operands));
}

BUSTER_GLOBAL_LOCAL void codegen_canonical_x64_sign_extend(CodegenBuffer* buffer, X64Register value, u32 width)
{
    if (width >= 64)
    {
        return;
    }
    if (!width)
    {
        buffer->error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return;
    }
    BusterX86MetadataPhysicalOperand operands[2] = {
        codegen_canonical_x64_metadata_gpr(value, 64),
        codegen_canonical_x64_metadata_immediate(64 - width, 8),
    };
    (void)codegen_canonical_x64_metadata_emit(buffer, S8("SHL"), operands, BUSTER_ARRAY_LENGTH(operands));
    (void)codegen_canonical_x64_metadata_emit(buffer, S8("SHR"), operands, BUSTER_ARRAY_LENGTH(operands));
}

enum
{
    // The canonical path allocates no registers: every operand is reloaded
    // from its frame slot and every result is stored back. These are the fixed
    // scratch names the vocabulary lowers through.
    X64_SIMD_VECTOR_FIRST = 0,
    X64_SIMD_VECTOR_SECOND = 1,
    X64_SIMD_VECTOR_THIRD = 2,
    X64_SIMD_MASK = 1,
};

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_simd_supported(Target target, IrSimdOperation operation)
{
    if (target.cpu_arch != CPU_ARCH_X86_64 || !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512F) ||
        !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512BW))
    {
        return false;
    }
    if (operation == IR_SIMD_PERMUTE2_BYTE)
    {
        return target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512VBMI);
    }
    if (operation == IR_SIMD_COMPRESS_BYTE || operation == IR_SIMD_COMPRESS_STORE_BYTE)
    {
        return target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_AVX512VBMI2);
    }
    return true;
}

BUSTER_GLOBAL_LOCAL s32 codegen_canonical_x64_rebase_frame_displacement(CodegenBuffer* buffer, s64 displacement, u32 frame_base_offset);

BUSTER_GLOBAL_LOCAL BusterX86MetadataFeatureInput codegen_canonical_x64_simd_features(void)
{
    static String8 const names[] = {S8_INITIALIZER("avx512f"), S8_INITIALIZER("avx512bw"), S8_INITIALIZER("avx512vbmi"), S8_INITIALIZER("avx512vbmi2")};
    return (BusterX86MetadataFeatureInput){.names = names, .count = BUSTER_ARRAY_LENGTH(names)};
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_simd_emit_zmm_memory(CodegenBuffer* buffer, String8 mnemonic, u32 vector_index,
                                                                     X64Register base, s32 displacement, bool store, u16 memory_width,
                                                                     BusterX86MetadataPhysicalAttributes attributes)
{
    BusterX86MetadataPhysicalOperand vector = codegen_canonical_x64_metadata_vector(vector_index, 512);
    u16 element_width = memory_width == 512 ? 8 : memory_width;
    BusterX86MetadataPhysicalOperand memory = codegen_canonical_x64_metadata_evex_memory(base, element_width, 512, displacement);
    BusterX86MetadataPhysicalOperand operands[2] = {store ? memory : vector, store ? vector : memory};
    return codegen_canonical_x64_metadata_emit_attributes(buffer, mnemonic, operands, BUSTER_ARRAY_LENGTH(operands),
                                                          codegen_canonical_x64_simd_features(), attributes);
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_simd_emit_zmm_gpr(CodegenBuffer* buffer, String8 mnemonic, u32 destination,
                                                                  X64Register source, BusterX86MetadataPhysicalAttributes attributes)
{
    BusterX86MetadataPhysicalOperand operands[2] = {
        codegen_canonical_x64_metadata_vector(destination, 512),
        codegen_canonical_x64_metadata_gpr(source, 32),
    };
    return codegen_canonical_x64_metadata_emit_attributes(buffer, mnemonic, operands, BUSTER_ARRAY_LENGTH(operands),
                                                          codegen_canonical_x64_simd_features(), attributes);
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_simd_emit_mask_zmm_memory(CodegenBuffer* buffer, String8 mnemonic, u32 mask, u32 vector_index,
                                                                          X64Register base, s32 displacement, u16 memory_width,
                                                                          u8 immediate, bool has_immediate)
{
    BusterX86MetadataPhysicalOperand operands[4] = {
        codegen_canonical_x64_metadata_mask(mask),
        codegen_canonical_x64_metadata_vector(vector_index, 512),
        codegen_canonical_x64_metadata_memory(base, memory_width, displacement),
        codegen_canonical_x64_metadata_immediate(immediate, 8),
    };
    return codegen_canonical_x64_metadata_emit_attributes(buffer, mnemonic, operands, has_immediate ? 4 : 3,
                                                          codegen_canonical_x64_simd_features(), (BusterX86MetadataPhysicalAttributes){0});
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_simd_emit_mask_zmm_register(CodegenBuffer* buffer, String8 mnemonic, u32 mask, u32 vector_index)
{
    BusterX86MetadataPhysicalOperand operands[2] = {
        codegen_canonical_x64_metadata_mask(mask),
        codegen_canonical_x64_metadata_vector(vector_index, 512),
    };
    return codegen_canonical_x64_metadata_emit_attributes(buffer, mnemonic, operands, BUSTER_ARRAY_LENGTH(operands),
                                                          codegen_canonical_x64_simd_features(), (BusterX86MetadataPhysicalAttributes){0});
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_simd_emit_zmm_register_immediate(CodegenBuffer* buffer, String8 mnemonic, u32 destination,
                                                                                  u32 source, u8 immediate)
{
    BusterX86MetadataPhysicalOperand operands[3] = {
        codegen_canonical_x64_metadata_vector(destination, 512),
        codegen_canonical_x64_metadata_vector(source, 512),
        codegen_canonical_x64_metadata_immediate(immediate, 8),
    };
    return codegen_canonical_x64_metadata_emit_attributes(buffer, mnemonic, operands, BUSTER_ARRAY_LENGTH(operands),
                                                          codegen_canonical_x64_simd_features(), (BusterX86MetadataPhysicalAttributes){0});
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_simd_emit_zmm_ternary_immediate(CodegenBuffer* buffer, String8 mnemonic, u32 destination,
                                                                                u32 source_one, u32 source_two, u8 immediate)
{
    BusterX86MetadataPhysicalOperand operands[4] = {
        codegen_canonical_x64_metadata_vector(destination, 512),
        codegen_canonical_x64_metadata_vector(source_one, 512),
        codegen_canonical_x64_metadata_vector(source_two, 512),
        codegen_canonical_x64_metadata_immediate(immediate, 8),
    };
    return codegen_canonical_x64_metadata_emit_attributes(buffer, mnemonic, operands, BUSTER_ARRAY_LENGTH(operands),
                                                          codegen_canonical_x64_simd_features(), (BusterX86MetadataPhysicalAttributes){0});
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_simd_emit_kmov_frame(CodegenBuffer* buffer, u32 mask, bool store, s32 displacement)
{
    BusterX86MetadataPhysicalOperand mask_operand = codegen_canonical_x64_metadata_mask(mask);
    BusterX86MetadataPhysicalOperand memory = codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, displacement);
    BusterX86MetadataPhysicalOperand operands[2] = {store ? memory : mask_operand, store ? mask_operand : memory};
    return codegen_canonical_x64_metadata_emit_attributes(buffer, S8("KMOVQ"), operands, BUSTER_ARRAY_LENGTH(operands),
                                                          codegen_canonical_x64_simd_features(), (BusterX86MetadataPhysicalAttributes){0});
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_simd_operation(CodegenBuffer* buffer, IrInstruction* instruction, u32 const* value_offsets,
                                                              u32 frame_base_offset, Target target)
{
    IrSimdOperation operation = (IrSimdOperation)instruction->simd_operation;
    IrSimdShape shape = ir_simd_operation_shape(operation);
    if (!codegen_canonical_x64_simd_supported(target, operation) || instruction->operand_count != shape.operand_count ||
        instruction->immediate_count != shape.immediate_count)
    {
        return false;
    }
    s32 slots[4] = {0};
    for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
    {
        slots[operand_index] =
            codegen_canonical_x64_rebase_frame_displacement(buffer, -(s64)value_offsets[instruction->operands[operand_index].value], frame_base_offset);
    }
    s32 result_slot = shape.has_result
                          ? codegen_canonical_x64_rebase_frame_displacement(buffer, -(s64)value_offsets[instruction->result.value], frame_base_offset)
                          : 0;
    u8 immediate = instruction->immediate_count ? (u8)instruction->immediates[0] : 0;
    X64Evex const move_load = {.map = 1, .prefix = 3, .opcode = 0x6f};
    X64Evex const move_store = {.map = 1, .prefix = 3, .opcode = 0x7f};
    switch (operation)
    {
    case IR_SIMD_LOAD:
    case IR_SIMD_LOAD_MASKED:
    {
        bool masked = operation == IR_SIMD_LOAD_MASKED;
        if (masked)
        {
            if (!codegen_canonical_x64_simd_emit_kmov_frame(buffer, X64_SIMD_MASK, false, slots[1]))
            {
                return false;
            }
        }
        BusterX86MetadataPhysicalOperand pointer_load[2] = {
            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
            codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, slots[0]),
        };
        BusterX86MetadataPhysicalAttributes attributes = masked
                                                              ? (BusterX86MetadataPhysicalAttributes){
                                                                    .decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK | BUSTER_X86_METADATA_DECORATOR_ZEROING,
                                                                    .mask_register = X64_SIMD_MASK,
                                                                    .has_mask_register = true,
                                                                    .zeroing = true,
                                                                }
                                                              : (BusterX86MetadataPhysicalAttributes){0};
        if (!codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), pointer_load, BUSTER_ARRAY_LENGTH(pointer_load)) ||
            !codegen_canonical_x64_simd_emit_zmm_memory(buffer, S8("VMOVDQU8"), X64_SIMD_VECTOR_FIRST, X64_REGISTER_RAX, 0, false, 8,
                                                        attributes) ||
            !codegen_canonical_x64_simd_emit_zmm_memory(buffer, S8("VMOVDQU8"), X64_SIMD_VECTOR_FIRST, X64_REGISTER_RBP, result_slot, true, 8,
                                                        (BusterX86MetadataPhysicalAttributes){0}))
        {
            return false;
        }
        return buffer->error == CODEGEN_ERROR_NONE;
    }
    case IR_SIMD_STORE:
    case IR_SIMD_STORE_MASKED:
    case IR_SIMD_COMPRESS_STORE_BYTE:
    {
        bool masked = operation != IR_SIMD_STORE;
        u32 vector_operand = masked ? 2 : 1;
        if (masked)
        {
            if (!codegen_canonical_x64_simd_emit_kmov_frame(buffer, X64_SIMD_MASK, false, slots[1]))
            {
                return false;
            }
        }
        BusterX86MetadataPhysicalOperand pointer_load[2] = {
            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
            codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, slots[0]),
        };
        BusterX86MetadataPhysicalAttributes attributes = masked
                                                              ? (BusterX86MetadataPhysicalAttributes){
                                                                    .decorator_flags = BUSTER_X86_METADATA_DECORATOR_MASK,
                                                                    .mask_register = X64_SIMD_MASK,
                                                                    .has_mask_register = true,
                                                                }
                                                              : (BusterX86MetadataPhysicalAttributes){0};
        // vpcompressb writes its destination through the rm operand, so the
        // compressing store and the plain store share this shape exactly.
        String8 store_mnemonic = operation == IR_SIMD_COMPRESS_STORE_BYTE ? S8("VPCOMPRESSB") : S8("VMOVDQU8");
        u16 store_memory_width = 8;
        if (!codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), pointer_load, BUSTER_ARRAY_LENGTH(pointer_load)) ||
            !codegen_canonical_x64_simd_emit_zmm_memory(buffer, S8("VMOVDQU8"), X64_SIMD_VECTOR_FIRST, X64_REGISTER_RBP,
                                                        slots[vector_operand], false, 8, (BusterX86MetadataPhysicalAttributes){0}) ||
            !codegen_canonical_x64_simd_emit_zmm_memory(buffer, store_mnemonic, X64_SIMD_VECTOR_FIRST, X64_REGISTER_RAX, 0, true, store_memory_width,
                                                        attributes))
        {
            return false;
        }
        return buffer->error == CODEGEN_ERROR_NONE;
    }
    case IR_SIMD_SPLAT_BYTE:
    {
        BusterX86MetadataPhysicalOperand load_operands[2] = {
            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
            codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 8, slots[0]),
        };
        if (!codegen_canonical_x64_metadata_emit(buffer, S8("MOVZX"), load_operands, BUSTER_ARRAY_LENGTH(load_operands)) ||
            !codegen_canonical_x64_simd_emit_zmm_gpr(buffer, S8("VPBROADCASTB"), X64_SIMD_VECTOR_FIRST, X64_REGISTER_RAX,
                                                     (BusterX86MetadataPhysicalAttributes){0}) ||
            !codegen_canonical_x64_simd_emit_zmm_memory(buffer, S8("VMOVDQU8"), X64_SIMD_VECTOR_FIRST, X64_REGISTER_RBP, result_slot, true, 8,
                                                        (BusterX86MetadataPhysicalAttributes){0}))
        {
            return false;
        }
        return buffer->error == CODEGEN_ERROR_NONE;
    }
    case IR_SIMD_COMPARE_EQUAL_BYTE:
    case IR_SIMD_COMPARE_LESS_BYTE:
    case IR_SIMD_TEST_MASK_BYTE:
    {
        String8 mnemonic = operation == IR_SIMD_COMPARE_EQUAL_BYTE ? S8("VPCMPEQB")
                            : operation == IR_SIMD_COMPARE_LESS_BYTE ? S8("VPCMPUB")
                                                                      : S8("VPTESTMB");
        if (!codegen_canonical_x64_simd_emit_zmm_memory(buffer, S8("VMOVDQU8"), X64_SIMD_VECTOR_FIRST, X64_REGISTER_RBP, slots[0], false, 8,
                                                        (BusterX86MetadataPhysicalAttributes){0}) ||
            !codegen_canonical_x64_simd_emit_mask_zmm_memory(buffer, mnemonic, X64_SIMD_MASK, X64_SIMD_VECTOR_FIRST,
                                                             X64_REGISTER_RBP, slots[1], 8, operation == IR_SIMD_COMPARE_LESS_BYTE ? 1 : 0,
                                                             operation == IR_SIMD_COMPARE_LESS_BYTE) ||
            !codegen_canonical_x64_simd_emit_kmov_frame(buffer, X64_SIMD_MASK, true, result_slot))
        {
            return false;
        }
        return buffer->error == CODEGEN_ERROR_NONE;
    }
    case IR_SIMD_SIGN_MASK_BYTE:
    {
        if (!codegen_canonical_x64_simd_emit_zmm_memory(buffer, S8("VMOVDQU8"), X64_SIMD_VECTOR_FIRST, X64_REGISTER_RBP, slots[0], false, 8,
                                                        (BusterX86MetadataPhysicalAttributes){0}) ||
            !codegen_canonical_x64_simd_emit_mask_zmm_register(buffer, S8("VPMOVB2M"), X64_SIMD_MASK, X64_SIMD_VECTOR_FIRST) ||
            !codegen_canonical_x64_simd_emit_kmov_frame(buffer, X64_SIMD_MASK, true, result_slot))
        {
            return false;
        }
        return buffer->error == CODEGEN_ERROR_NONE;
    }
    case IR_SIMD_PERMUTE2_BYTE:
    {
        if (!codegen_canonical_x64_simd_emit_kmov_frame(buffer, X64_SIMD_MASK, false, slots[0]))
        {
            return false;
        }
        X64Evex load = move_load;
        load.reg = X64_SIMD_VECTOR_FIRST;
        if (!codegen_canonical_x64_evex_frame(buffer, load, slots[1]))
        {
            return false;
        }
        load.reg = X64_SIMD_VECTOR_SECOND;
        if (!codegen_canonical_x64_evex_frame(buffer, load, slots[2]))
        {
            return false;
        }
        // vpermt2b reads the low table from its destination and the high table
        // from rm, so the destination is loaded with the low table first and
        // the masked write lands on top of it.
        X64Evex permute = {
            .map = 2,
            .prefix = 1,
            .opcode = 0x7d,
            .reg = X64_SIMD_VECTOR_FIRST,
            .vvvv = X64_SIMD_VECTOR_SECOND,
            .mask = X64_SIMD_MASK,
            .zeroing = true,
        };
        if (!codegen_canonical_x64_evex_frame(buffer, permute, slots[3]))
        {
            return false;
        }
        X64Evex spill = move_store;
        spill.reg = X64_SIMD_VECTOR_FIRST;
        return codegen_canonical_x64_evex_frame(buffer, spill, result_slot);
    }
    case IR_SIMD_COMPRESS_BYTE:
    {
        if (!codegen_canonical_x64_simd_emit_kmov_frame(buffer, X64_SIMD_MASK, false, slots[0]))
        {
            return false;
        }
        X64Evex load = move_load;
        load.reg = X64_SIMD_VECTOR_SECOND;
        if (!codegen_canonical_x64_evex_frame(buffer, load, slots[1]))
        {
            return false;
        }
        // Register form: rm is the destination and reg is the source, so the
        // source is the one that gets loaded and the result lands in the
        // first register like every other operation's does.
        X64Evex compress = {
            .map = 2,
            .prefix = 1,
            .opcode = 0x63,
            .reg = X64_SIMD_VECTOR_SECOND,
            .mask = X64_SIMD_MASK,
            .zeroing = true,
        };
        if (!codegen_canonical_x64_evex_register(buffer, compress, X64_SIMD_VECTOR_FIRST))
        {
            return false;
        }
        X64Evex spill = move_store;
        spill.reg = X64_SIMD_VECTOR_FIRST;
        return codegen_canonical_x64_evex_frame(buffer, spill, result_slot);
    }
    case IR_SIMD_WIDEN_BYTE_TO_WORD:
    {
        // The source already lives in memory, so the quarter selection is an
        // address offset and vpmovzxbd reads its 16 bytes straight from there
        // — no vextracti32x4 in front of it.
        X64Evex widen = {.map = 2, .prefix = 1, .opcode = 0x31, .reg = X64_SIMD_VECTOR_FIRST};
        if (!codegen_canonical_x64_evex_frame(buffer, widen, slots[0] + (s32)immediate * 16))
        {
            return false;
        }
        X64Evex spill = move_store;
        spill.reg = X64_SIMD_VECTOR_FIRST;
        return codegen_canonical_x64_evex_frame(buffer, spill, result_slot);
    }
    case IR_SIMD_SHIFT_LEFT_WORD:
    {
        if (!codegen_canonical_x64_evex_frame(buffer, move_load, slots[0]) ||
            !codegen_canonical_x64_simd_emit_zmm_register_immediate(buffer, S8("VPSLLD"), X64_SIMD_VECTOR_FIRST,
                                                                      X64_SIMD_VECTOR_FIRST, immediate))
        {
            return false;
        }
        X64Evex spill = move_store;
        spill.reg = X64_SIMD_VECTOR_FIRST;
        return codegen_canonical_x64_evex_frame(buffer, spill, result_slot);
    }
    case IR_SIMD_TERNARY_WORD:
    {
        X64Evex load = move_load;
        load.reg = X64_SIMD_VECTOR_FIRST;
        if (!codegen_canonical_x64_evex_frame(buffer, load, slots[0]))
        {
            return false;
        }
        load.reg = X64_SIMD_VECTOR_SECOND;
        if (!codegen_canonical_x64_evex_frame(buffer, load, slots[1]))
        {
            return false;
        }
        load.reg = X64_SIMD_VECTOR_THIRD;
        if (!codegen_canonical_x64_evex_frame(buffer, load, slots[2]) ||
            !codegen_canonical_x64_simd_emit_zmm_ternary_immediate(buffer, S8("VPTERNLOGD"), X64_SIMD_VECTOR_FIRST,
                                                                     X64_SIMD_VECTOR_SECOND, X64_SIMD_VECTOR_THIRD, immediate))
        {
            return false;
        }
        X64Evex spill = move_store;
        spill.reg = X64_SIMD_VECTOR_FIRST;
        return codegen_canonical_x64_evex_frame(buffer, spill, result_slot);
    }
    case IR_SIMD_COUNT:
        break;
    }
    return false;
}


BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_instruction_uses_wide_vector(IrProgram* program, IrFunction* function, IrInstruction* instruction, Target target)
{
    if (instruction->opcode == IR_OPCODE_SIMD)
    {
        // A run of these is the whole point of the vocabulary; splitting it
        // with a vzeroupper between every pair would cost more than the
        // transition it avoids.
        return codegen_canonical_x64_simd_supported(target, (IrSimdOperation)instruction->simd_operation);
    }
    if (instruction->opcode != IR_OPCODE_BINARY || instruction->operand_count != 2 || instruction->binary_operation >= IR_BINARY_VECTOR_INTEGER_EQUAL)
    {
        return false;
    }
    IrType* vector = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
    IrType* element = vector && vector->kind == IR_TYPE_VECTOR ? ir_type_from_id(&program->types, vector->element_type) : 0;
    if (!element || (element->kind != IR_TYPE_INTEGER && element->kind != IR_TYPE_FLOAT) ||
        !x64_target_supports_native_vector(target, vector->layout.size, element->bit_width, element->kind == IR_TYPE_INTEGER))
    {
        return false;
    }
    switch (instruction->binary_operation)
    {
    case IR_BINARY_VECTOR_FLOAT_ADD:
    case IR_BINARY_VECTOR_FLOAT_SUBTRACT:
    case IR_BINARY_VECTOR_FLOAT_MULTIPLY:
    case IR_BINARY_VECTOR_FLOAT_DIVIDE:
    case IR_BINARY_VECTOR_INTEGER_ADD:
    case IR_BINARY_VECTOR_INTEGER_SUBTRACT:
    case IR_BINARY_VECTOR_INTEGER_BITWISE_AND:
    case IR_BINARY_VECTOR_INTEGER_BITWISE_OR:
    case IR_BINARY_VECTOR_INTEGER_BITWISE_XOR:
        return true;
    default:
        return false;
    }
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_instruction_preserves_wide_vector(IrProgram* program, IrInstruction* instruction)
{
    if (instruction->opcode == IR_OPCODE_FIELD)
    {
        return true;
    }
    if (instruction->opcode != IR_OPCODE_LOAD || instruction->result.value == IR_ID_UNDERLYING_INVALID)
    {
        return false;
    }
    IrType* result_type = ir_type_from_id(&program->types, instruction->canonical_type);
    return result_type && result_type->kind == IR_TYPE_VECTOR && result_type->layout.resolved && result_type->layout.size && result_type->layout.size <= 64;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_vector_operation(CodegenBuffer* output, IrProgram* program, IrFunction* function, IrInstruction* instruction,
                                                                u32 const* value_offsets, u32 frame_base_offset, Target target, u64* native_operation_count,
                                                                u64* split_operation_count, bool* upper_vector_dirty, IrValueId* last_wide_vector_result,
                                                                u32* last_wide_vector_size, u64* forwarded_wide_vector_load_count)
{
    if (!instruction->operand_count)
    {
        return false;
    }
    IrTypeId operand_type_id = function->values[instruction->operands[0].value].canonical_type;
    IrType* vector = ir_type_from_id(&program->types, operand_type_id);
    IrType* element = vector ? ir_type_from_id(&program->types, vector->element_type) : 0;
    if (!vector || vector->kind != IR_TYPE_VECTOR || !element || (element->kind != IR_TYPE_INTEGER && element->kind != IR_TYPE_FLOAT) ||
        (element->bit_width != 8 && element->bit_width != 16 && element->bit_width != 32 && element->bit_width != 64) ||
        instruction->result.value == IR_ID_UNDERLYING_INVALID)
    {
        return false;
    }
    u32 lane_size = element->bit_width / 8;
    if ((u64)lane_size * vector->element_count != vector->layout.size || vector->element_count > UINT32_MAX)
    {
        return false;
    }
    // Frame slots are addressed through the same rebase every other canonical
    // emission uses: a Win64 function with a dynamic stack sets rbp to the
    // bottom of the frame and reaches its values at positive displacements,
    // where every other target keeps rbp at the top and uses negative ones.
    // Spelling the displacement as a bare negation is only right where the
    // rebase is the identity, so it silently addressed outside the frame for
    // exactly the Windows functions this path is reached from.
    s32 left_displacement =
        codegen_canonical_x64_rebase_frame_displacement(output, -(s64)value_offsets[instruction->operands[0].value], frame_base_offset);
    s32 result_displacement =
        codegen_canonical_x64_rebase_frame_displacement(output, -(s64)value_offsets[instruction->result.value], frame_base_offset);
    s32 right_displacement =
        instruction->operand_count == 2
            ? codegen_canonical_x64_rebase_frame_displacement(output, -(s64)value_offsets[instruction->operands[1].value], frame_base_offset)
            : 0;
    X64Builder builder = {
        .buffer = *output,
    };
    codegen_canonical_x64_address(&builder.buffer, X64_REGISTER_R8, left_displacement);
    if (instruction->operand_count == 2)
    {
        codegen_canonical_x64_address(&builder.buffer, X64_REGISTER_R9, right_displacement);
    }
    codegen_canonical_x64_address(&builder.buffer, X64_REGISTER_R10, result_displacement);
    if (builder.buffer.error != CODEGEN_ERROR_NONE)
    {
        return false;
    }
    bool comparison = instruction->opcode == IR_OPCODE_BINARY && instruction->binary_operation >= IR_BINARY_VECTOR_INTEGER_EQUAL &&
                      instruction->binary_operation <= IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL;
    u8 condition = 0;
    bool ordered = false;
    bool unordered = false;
    if (comparison && !x64_vector_comparison_condition(instruction->binary_operation, &condition, &ordered, &unordered))
    {
        return false;
    }
    bool wide_native = x64_target_supports_native_vector(target, vector->layout.size, element->bit_width, element->kind == IR_TYPE_INTEGER) &&
                       instruction->opcode == IR_OPCODE_BINARY && instruction->operand_count == 2 && !comparison;
    if (wide_native)
    {
        u8 operation = 0;
        u8 prefix = 0;
        if (element->kind == IR_TYPE_FLOAT)
        {
            prefix = element->bit_width == 64 ? 0x66 : 0;
            operation = instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_ADD        ? 0x58
                        : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_SUBTRACT ? 0x5c
                        : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_MULTIPLY ? 0x59
                        : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_DIVIDE   ? 0x5e
                                                                                           : 0;
        }
        else
        {
            prefix = 0x66;
            IrBinaryOperation binary = instruction->binary_operation;
            if (binary == IR_BINARY_VECTOR_INTEGER_ADD)
            {
                operation = element->bit_width == 8 ? 0xfc : element->bit_width == 16 ? 0xfd : element->bit_width == 32 ? 0xfe : 0xd4;
            }
            else if (binary == IR_BINARY_VECTOR_INTEGER_SUBTRACT)
            {
                operation = element->bit_width == 8 ? 0xf8 : element->bit_width == 16 ? 0xf9 : element->bit_width == 32 ? 0xfa : 0xfb;
            }
            else if (binary == IR_BINARY_VECTOR_INTEGER_BITWISE_AND || binary == IR_BINARY_VECTOR_INTEGER_BITWISE_OR ||
                     binary == IR_BINARY_VECTOR_INTEGER_BITWISE_XOR)
            {
                operation = binary == IR_BINARY_VECTOR_INTEGER_BITWISE_AND ? 0xdb : binary == IR_BINARY_VECTOR_INTEGER_BITWISE_OR ? 0xeb : 0xef;
            }
        }
        if (operation)
        {
            bool forwarded_left =
                *upper_vector_dirty && last_wide_vector_result->value == instruction->operands[0].value && *last_wide_vector_size == vector->layout.size;
            bool forwarded_right = *upper_vector_dirty && x64_vector_binary_is_commutative(instruction->binary_operation) &&
                                   last_wide_vector_result->value == instruction->operands[1].value && *last_wide_vector_size == vector->layout.size;
            if (forwarded_left || forwarded_right)
            {
                *forwarded_wide_vector_load_count += 1;
            }
            else
            {
                x64_emit_vector_native_memory(&builder, false, (u32)vector->layout.size, X64_REGISTER_R8);
            }
            x64_emit_vector_native_binary_operation_kind(&builder, element->kind == IR_TYPE_INTEGER, (u16)element->bit_width, prefix, operation,
                                                         (u32)vector->layout.size, forwarded_right ? X64_REGISTER_R8 : X64_REGISTER_R9);
            x64_emit_vector_native_memory(&builder, true, (u32)vector->layout.size, X64_REGISTER_R10);
            *output = builder.buffer;
            *native_operation_count += 1;
            *upper_vector_dirty = true;
            *last_wide_vector_result = instruction->result;
            *last_wide_vector_size = (u32)vector->layout.size;
            return true;
        }
    }
    if (vector->layout.size == 16 && instruction->opcode == IR_OPCODE_BINARY && instruction->operand_count == 2 && !comparison)
    {
        u8 operation = 0;
        bool native = false;
        if (element->kind == IR_TYPE_FLOAT)
        {
            operation = instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_ADD        ? 0x58
                        : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_SUBTRACT ? 0x5c
                        : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_MULTIPLY ? 0x59
                        : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_DIVIDE   ? 0x5e
                                                                                           : 0;
            native = operation != 0;
            if (native)
            {
                String8 load_mnemonic = S8("MOVUPS");
                String8 arithmetic_mnemonic = element->bit_width == 32
                                                   ? (operation == 0x58 ? S8("ADDPS")
                                                      : operation == 0x5c ? S8("SUBPS")
                                                      : operation == 0x59 ? S8("MULPS")
                                                                          : S8("DIVPS"))
                                                   : (operation == 0x58 ? S8("ADDPD")
                                                      : operation == 0x5c ? S8("SUBPD")
                                                      : operation == 0x59 ? S8("MULPD")
                                                                          : S8("DIVPD"));
                String8 store_mnemonic = S8("MOVUPS");
                BusterX86MetadataFeatureInput features = {
                    .names = (String8[]){element->bit_width == 32 ? S8("sse") : S8("sse2")}, .count = 1,
                };
                BusterX86MetadataPhysicalOperand load_left[2] = {
                    codegen_canonical_x64_metadata_vector(0, 128),
                    codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_R8, 32, 0),
                };
                BusterX86MetadataPhysicalOperand load_right[2] = {
                    codegen_canonical_x64_metadata_vector(1, 128),
                    codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_R9, 32, 0),
                };
                BusterX86MetadataPhysicalOperand arithmetic[2] = {
                    codegen_canonical_x64_metadata_vector(0, 128),
                    codegen_canonical_x64_metadata_vector(1, 128),
                };
                BusterX86MetadataPhysicalOperand store[2] = {
                    codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_R10, 32, 0),
                    codegen_canonical_x64_metadata_vector(0, 128),
                };
                native = codegen_canonical_x64_metadata_emit_features(&builder.buffer, load_mnemonic, load_left,
                                                                        BUSTER_ARRAY_LENGTH(load_left), features) &&
                        codegen_canonical_x64_metadata_emit_features(&builder.buffer, load_mnemonic, load_right,
                                                                        BUSTER_ARRAY_LENGTH(load_right), features) &&
                        codegen_canonical_x64_metadata_emit_features(&builder.buffer, arithmetic_mnemonic, arithmetic,
                                                                        BUSTER_ARRAY_LENGTH(arithmetic), features) &&
                        codegen_canonical_x64_metadata_emit_features(&builder.buffer, store_mnemonic, store,
                                                                        BUSTER_ARRAY_LENGTH(store), features);
            }
        }
        else
        {
            IrBinaryOperation binary = instruction->binary_operation;
            if (binary == IR_BINARY_VECTOR_INTEGER_ADD)
            {
                native = true;
            }
            else if (binary == IR_BINARY_VECTOR_INTEGER_SUBTRACT)
            {
                native = true;
            }
            else if (binary == IR_BINARY_VECTOR_INTEGER_BITWISE_AND || binary == IR_BINARY_VECTOR_INTEGER_BITWISE_OR ||
                     binary == IR_BINARY_VECTOR_INTEGER_BITWISE_XOR)
            {
                native = true;
            }
            if (native)
            {
                String8 arithmetic_mnemonic = binary == IR_BINARY_VECTOR_INTEGER_ADD
                                                   ? (element->bit_width == 8 ? S8("PADDB")
                                                      : element->bit_width == 16 ? S8("PADDW")
                                                      : element->bit_width == 32 ? S8("PADDD")
                                                                                  : S8("PADDQ"))
                                                   : binary == IR_BINARY_VECTOR_INTEGER_SUBTRACT
                                                   ? (element->bit_width == 8 ? S8("PSUBB")
                                                      : element->bit_width == 16 ? S8("PSUBW")
                                                      : element->bit_width == 32 ? S8("PSUBD")
                                                                                  : S8("PSUBQ"))
                                                   : binary == IR_BINARY_VECTOR_INTEGER_BITWISE_AND ? S8("PAND")
                                                   : binary == IR_BINARY_VECTOR_INTEGER_BITWISE_OR  ? S8("POR")
                                                                                                     : S8("PXOR");
                String8 features[] = {S8("sse2")};
                BusterX86MetadataPhysicalOperand load_left[2] = {
                    codegen_canonical_x64_metadata_vector(0, 128),
                    codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_R8, 128, 0),
                };
                BusterX86MetadataPhysicalOperand load_right[2] = {
                    codegen_canonical_x64_metadata_vector(1, 128),
                    codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_R9, 128, 0),
                };
                BusterX86MetadataPhysicalOperand arithmetic[2] = {
                    codegen_canonical_x64_metadata_vector(0, 128),
                    codegen_canonical_x64_metadata_vector(1, 128),
                };
                BusterX86MetadataPhysicalOperand store[2] = {
                    codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_R10, 128, 0),
                    codegen_canonical_x64_metadata_vector(0, 128),
                };
                native = codegen_canonical_x64_metadata_emit_features(
                             &builder.buffer, S8("MOVDQU"), load_left, BUSTER_ARRAY_LENGTH(load_left),
                             (BusterX86MetadataFeatureInput){.names = features, .count = BUSTER_ARRAY_LENGTH(features)}) &&
                         codegen_canonical_x64_metadata_emit_features(
                             &builder.buffer, S8("MOVDQU"), load_right, BUSTER_ARRAY_LENGTH(load_right),
                             (BusterX86MetadataFeatureInput){.names = features, .count = BUSTER_ARRAY_LENGTH(features)}) &&
                         codegen_canonical_x64_metadata_emit_features(
                             &builder.buffer, arithmetic_mnemonic, arithmetic, BUSTER_ARRAY_LENGTH(arithmetic),
                             (BusterX86MetadataFeatureInput){.names = features, .count = BUSTER_ARRAY_LENGTH(features)}) &&
                         codegen_canonical_x64_metadata_emit_features(
                             &builder.buffer, S8("MOVDQU"), store, BUSTER_ARRAY_LENGTH(store),
                             (BusterX86MetadataFeatureInput){.names = features, .count = BUSTER_ARRAY_LENGTH(features)});
            }
        }
        if (native)
        {
            *output = builder.buffer;
            *native_operation_count += 1;
            return true;
        }
    }
    if (vector->layout.size > 16)
    {
        *split_operation_count += 1;
    }
    for (u32 lane = 0; lane < (u32)vector->element_count; lane += 1)
    {
        u32 offset = lane * lane_size;
        if (instruction->opcode == IR_OPCODE_UNARY)
        {
            x64_emit_load_memory(&builder, X64_REGISTER_RAX, X64_REGISTER_R8, offset, lane_size);
            if (builder.buffer.error != CODEGEN_ERROR_NONE)
            {
                return false;
            }
            if (instruction->unary_operation == IR_UNARY_VECTOR_FLOAT_NEGATE)
            {
                if (element->kind != IR_TYPE_FLOAT || (element->bit_width != 32 && element->bit_width != 64))
                {
                    return false;
                }
                BusterX86MetadataPhysicalOperand sign_mask_operands[2] = {
                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 32),
                    codegen_canonical_x64_metadata_immediate(INT64_C(0x80000000), 32),
                };
                if (!codegen_canonical_x64_metadata_emit(&builder.buffer, S8("MOV"), sign_mask_operands,
                                                          BUSTER_ARRAY_LENGTH(sign_mask_operands)))
                {
                    return false;
                }
                if (element->bit_width == 64)
                {
                    BusterX86MetadataPhysicalOperand shift_operands[2] = {
                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                        codegen_canonical_x64_metadata_immediate(32, 8),
                    };
                    if (!codegen_canonical_x64_metadata_emit(&builder.buffer, S8("SHL"), shift_operands,
                                                              BUSTER_ARRAY_LENGTH(shift_operands)))
                    {
                        return false;
                    }
                }
                BusterX86MetadataPhysicalOperand xor_operands[2] = {
                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                };
                if (!codegen_canonical_x64_metadata_emit(&builder.buffer, S8("XOR"), xor_operands, BUSTER_ARRAY_LENGTH(xor_operands)))
                {
                    return false;
                }
            }
            else if (instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_NEGATE)
            {
                if (element->kind != IR_TYPE_INTEGER)
                {
                    return false;
                }
                BusterX86MetadataPhysicalOperand unary_operands[] = {codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64)};
                if (!codegen_canonical_x64_metadata_emit(&builder.buffer, S8("NEG"), unary_operands, BUSTER_ARRAY_LENGTH(unary_operands)))
                {
                    return false;
                }
            }
            else if (instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_BITWISE_NOT)
            {
                if (element->kind != IR_TYPE_INTEGER)
                {
                    return false;
                }
                BusterX86MetadataPhysicalOperand unary_operands[] = {codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64)};
                if (!codegen_canonical_x64_metadata_emit(&builder.buffer, S8("NOT"), unary_operands, BUSTER_ARRAY_LENGTH(unary_operands)))
                {
                    return false;
                }
            }
            else
            {
                return false;
            }
            x64_emit_store_memory(&builder, X64_REGISTER_R10, offset, X64_REGISTER_RAX, lane_size);
            if (builder.buffer.error != CODEGEN_ERROR_NONE)
            {
                return false;
            }
            continue;
        }
        if (instruction->operand_count != 2)
        {
            return false;
        }
        if (element->kind == IR_TYPE_FLOAT)
        {
            if (element->bit_width != 32 && element->bit_width != 64)
            {
                return false;
            }
            x64_emit_load_float_bits(&builder, 0, X64_REGISTER_R8, offset, lane_size);
            x64_emit_load_float_bits(&builder, 1, X64_REGISTER_R9, offset, lane_size);
            if (builder.buffer.error != CODEGEN_ERROR_NONE)
            {
                return false;
            }
            if (!comparison)
            {
                u8 opcode = instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_ADD        ? 0x58
                            : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_SUBTRACT ? 0x5c
                            : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_MULTIPLY ? 0x59
                            : instruction->binary_operation == IR_BINARY_VECTOR_FLOAT_DIVIDE   ? 0x5e
                                                                                               : 0;
                if (!opcode)
                {
                    return false;
                }
                String8 mnemonic = element->bit_width == 32
                                       ? (opcode == 0x58 ? S8("ADDSS")
                                          : opcode == 0x5c ? S8("SUBSS")
                                          : opcode == 0x59 ? S8("MULSS")
                                                           : S8("DIVSS"))
                                       : (opcode == 0x58 ? S8("ADDSD")
                                          : opcode == 0x5c ? S8("SUBSD")
                                          : opcode == 0x59 ? S8("MULSD")
                                                           : S8("DIVSD"));
                BusterX86MetadataPhysicalOperand operands[2] = {
                    codegen_canonical_x64_metadata_vector(0, (u16)element->bit_width),
                    codegen_canonical_x64_metadata_vector(1, (u16)element->bit_width),
                };
                String8 feature_names[] = {element->bit_width == 32 ? S8("sse") : S8("sse2")};
                if (!codegen_canonical_x64_metadata_emit_features(
                        &builder.buffer, mnemonic, operands, BUSTER_ARRAY_LENGTH(operands),
                        (BusterX86MetadataFeatureInput){.names = feature_names, .count = BUSTER_ARRAY_LENGTH(feature_names)}))
                {
                    return false;
                }
                x64_emit_store_float_bits(&builder, X64_REGISTER_R10, offset, 0, lane_size);
                if (builder.buffer.error != CODEGEN_ERROR_NONE)
                {
                    return false;
                }
                continue;
            }
            BusterX86MetadataPhysicalOperand compare_operands[2] = {
                codegen_canonical_x64_metadata_vector(0, (u16)element->bit_width),
                codegen_canonical_x64_metadata_vector(1, (u16)element->bit_width),
            };
            String8 feature_names[] = {element->bit_width == 32 ? S8("sse") : S8("sse2")};
            String8 mnemonic = element->bit_width == 32 ? S8("UCOMISS") : S8("UCOMISD");
            if (!codegen_canonical_x64_metadata_emit_features(
                    &builder.buffer, mnemonic, compare_operands, BUSTER_ARRAY_LENGTH(compare_operands),
                    (BusterX86MetadataFeatureInput){.names = feature_names, .count = BUSTER_ARRAY_LENGTH(feature_names)}))
            {
                return false;
            }
        }
        else
        {
            x64_emit_load_memory(&builder, X64_REGISTER_RAX, X64_REGISTER_R8, offset, lane_size);
            x64_emit_load_memory(&builder, X64_REGISTER_RCX, X64_REGISTER_R9, offset, lane_size);
            if (builder.buffer.error != CODEGEN_ERROR_NONE)
            {
                return false;
            }
            IrBinaryOperation operation = instruction->binary_operation;
            bool signed_semantics = operation == IR_BINARY_VECTOR_SIGNED_DIVIDE || operation == IR_BINARY_VECTOR_SIGNED_REMAINDER ||
                                    (operation >= IR_BINARY_VECTOR_SIGNED_LESS && operation <= IR_BINARY_VECTOR_SIGNED_GREATER_EQUAL);
            if (signed_semantics)
            {
                codegen_canonical_x64_sign_extend(&builder.buffer, X64_REGISTER_RAX, element->bit_width);
                codegen_canonical_x64_sign_extend(&builder.buffer, X64_REGISTER_RCX, element->bit_width);
            }
            if (!comparison)
            {
                switch (operation)
                {
                case IR_BINARY_VECTOR_INTEGER_ADD:
                case IR_BINARY_VECTOR_INTEGER_SUBTRACT:
                case IR_BINARY_VECTOR_INTEGER_MULTIPLY:
                case IR_BINARY_VECTOR_INTEGER_BITWISE_AND:
                case IR_BINARY_VECTOR_INTEGER_BITWISE_OR:
                case IR_BINARY_VECTOR_INTEGER_BITWISE_XOR:
                {
                    String8 mnemonic = operation == IR_BINARY_VECTOR_INTEGER_ADD        ? S8("ADD")
                                       : operation == IR_BINARY_VECTOR_INTEGER_SUBTRACT   ? S8("SUB")
                                       : operation == IR_BINARY_VECTOR_INTEGER_MULTIPLY   ? S8("IMUL")
                                       : operation == IR_BINARY_VECTOR_INTEGER_BITWISE_AND ? S8("AND")
                                       : operation == IR_BINARY_VECTOR_INTEGER_BITWISE_OR  ? S8("OR")
                                                                                           : S8("XOR");
                    BusterX86MetadataPhysicalOperand operands[2] = {
                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                    };
                    if (!codegen_canonical_x64_metadata_emit(&builder.buffer, mnemonic, operands, BUSTER_ARRAY_LENGTH(operands)))
                    {
                        return false;
                    }
                    break;
                }
                case IR_BINARY_VECTOR_SHIFT_LEFT:
                case IR_BINARY_VECTOR_SIGNED_SHIFT_RIGHT:
                case IR_BINARY_VECTOR_UNSIGNED_SHIFT_RIGHT:
                {
                    BusterX86MetadataPhysicalOperand shift_operands[2] = {
                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 8),
                    };
                    String8 shift_mnemonic = operation == IR_BINARY_VECTOR_SHIFT_LEFT
                                                 ? S8("SHL")
                                                 : operation == IR_BINARY_VECTOR_SIGNED_SHIFT_RIGHT ? S8("SAR") : S8("SHR");
                    if (!codegen_canonical_x64_metadata_emit(&builder.buffer, shift_mnemonic, shift_operands,
                                                               BUSTER_ARRAY_LENGTH(shift_operands)))
                    {
                        return false;
                    }
                    break;
                }
                case IR_BINARY_VECTOR_SIGNED_DIVIDE:
                case IR_BINARY_VECTOR_SIGNED_REMAINDER:
                {
                    if (!codegen_canonical_x64_metadata_emit(&builder.buffer, S8("CQO"), 0, 0))
                    {
                        return false;
                    }
                    BusterX86MetadataPhysicalOperand divide_operands[] = {
                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                    };
                    if (!codegen_canonical_x64_metadata_emit(&builder.buffer, S8("IDIV"), divide_operands,
                                                              BUSTER_ARRAY_LENGTH(divide_operands)))
                    {
                        return false;
                    }
                    if (operation == IR_BINARY_VECTOR_SIGNED_REMAINDER)
                    {
                        BusterX86MetadataPhysicalOperand move_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                        };
                        if (!codegen_canonical_x64_metadata_emit(&builder.buffer, S8("MOV"), move_operands,
                                                                  BUSTER_ARRAY_LENGTH(move_operands)))
                        {
                            return false;
                        }
                    }
                    break;
                }
                case IR_BINARY_VECTOR_UNSIGNED_DIVIDE:
                case IR_BINARY_VECTOR_UNSIGNED_REMAINDER:
                {
                    BusterX86MetadataPhysicalOperand zero_operands[2] = {
                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 32),
                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 32),
                    };
                    if (!codegen_canonical_x64_metadata_emit(&builder.buffer, S8("XOR"), zero_operands,
                                                              BUSTER_ARRAY_LENGTH(zero_operands)))
                    {
                        return false;
                    }
                    BusterX86MetadataPhysicalOperand divide_operands[] = {
                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                    };
                    if (!codegen_canonical_x64_metadata_emit(&builder.buffer, S8("DIV"), divide_operands,
                                                              BUSTER_ARRAY_LENGTH(divide_operands)))
                    {
                        return false;
                    }
                    if (operation == IR_BINARY_VECTOR_UNSIGNED_REMAINDER)
                    {
                        BusterX86MetadataPhysicalOperand move_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                        };
                        if (!codegen_canonical_x64_metadata_emit(&builder.buffer, S8("MOV"), move_operands,
                                                                  BUSTER_ARRAY_LENGTH(move_operands)))
                        {
                            return false;
                        }
                    }
                    break;
                }
                default:
                    return false;
                }
                x64_emit_store_memory(&builder, X64_REGISTER_R10, offset, X64_REGISTER_RAX, lane_size);
                if (builder.buffer.error != CODEGEN_ERROR_NONE)
                {
                    return false;
                }
                continue;
            }
            BusterX86MetadataPhysicalOperand compare_operands[2] = {
                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, (u16)(lane_size * 8)),
                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, (u16)(lane_size * 8)),
            };
            if (!codegen_canonical_x64_metadata_emit(&builder.buffer, S8("CMP"), compare_operands,
                                                      BUSTER_ARRAY_LENGTH(compare_operands)))
            {
                return false;
            }
        }
        String8 condition_mnemonic = condition == 0x94 ? S8("SETZ")
                                      : condition == 0x95 ? S8("SETNZ")
                                      : condition == 0x9c ? S8("SETL")
                                      : condition == 0x9e ? S8("SETLE")
                                      : condition == 0x9f ? S8("SETNLE")
                                      : condition == 0x9d ? S8("SETNL")
                                      : condition == 0x92 ? S8("SETB")
                                      : condition == 0x96 ? S8("SETBE")
                                      : condition == 0x97 ? S8("SETNBE")
                                      : condition == 0x93 ? S8("SETNB")
                                                         : (String8){0};
        if (!condition_mnemonic.length)
        {
            return false;
        }
        BusterX86MetadataPhysicalOperand set_operands[] = {
            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 8),
        };
        if (!codegen_canonical_x64_metadata_emit(&builder.buffer, condition_mnemonic, set_operands,
                                                  BUSTER_ARRAY_LENGTH(set_operands)))
        {
            return false;
        }
        if (ordered || unordered)
        {
            BusterX86MetadataPhysicalOperand unordered_operands[] = {
                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 8),
            };
            if (!codegen_canonical_x64_metadata_emit(&builder.buffer, unordered ? S8("SETP") : S8("SETNP"), unordered_operands,
                                                      BUSTER_ARRAY_LENGTH(unordered_operands)))
            {
                return false;
            }
            BusterX86MetadataPhysicalOperand ordered_operands[2] = {
                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 8),
                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 8),
            };
            if (!codegen_canonical_x64_metadata_emit(&builder.buffer, unordered ? S8("OR") : S8("AND"), ordered_operands,
                                                      BUSTER_ARRAY_LENGTH(ordered_operands)))
            {
                return false;
            }
        }
        BusterX86MetadataPhysicalOperand widen_operands[2] = {
            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 8),
        };
        if (!codegen_canonical_x64_metadata_emit(&builder.buffer, S8("MOVZX"), widen_operands,
                                                  BUSTER_ARRAY_LENGTH(widen_operands)))
        {
            return false;
        }
        BusterX86MetadataPhysicalOperand negate_operands[] = {
            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
        };
        if (!codegen_canonical_x64_metadata_emit(&builder.buffer, S8("NEG"), negate_operands,
                                                  BUSTER_ARRAY_LENGTH(negate_operands)))
        {
            return false;
        }
        x64_emit_store_memory(&builder, X64_REGISTER_R10, offset, X64_REGISTER_RAX, lane_size);
        if (builder.buffer.error != CODEGEN_ERROR_NONE)
        {
            return false;
        }
    }
    *output = builder.buffer;
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_a64_memory_operation_base(CodegenBuffer* buffer, u32 register_number, u32 offset, u32 size, bool store,
                                                                     bool sign_extend, u32 base_register)
{
    u32 scale = size == 8 ? 8 : size == 4 ? 4 : size == 2 ? 2 : size == 1 ? 1 : 0;
    if (!scale || offset % scale || register_number > 31)
    {
        return false;
    }
    bool indirect = offset / scale > A64_IMM12_MAX;
    if (indirect)
    {
        codegen_canonical_a64_base_address(buffer, 16, base_register, offset);
        offset = 0;
    }
    u32 instruction = 0;
    if (store)
    {
        instruction = size == 8 ? 0xf90003e0 : size == 4 ? 0xb90003e0 : size == 2 ? 0x790003e0 : 0x390003e0;
    }
    else if (sign_extend)
    {
        instruction = size == 4 ? 0xb98003e0 : size == 2 ? 0x798003e0 : size == 1 ? 0x398003e0 : 0xf94003e0;
    }
    else
    {
        instruction = size == 8 ? 0xf94003e0 : size == 4 ? 0xb94003e0 : size == 2 ? 0x794003e0 : 0x394003e0;
    }
    codegen_emit_u32(buffer, (instruction & ~(31u << 5)) | ((offset / scale) << 10) | ((indirect ? 16u : base_register) << 5) | register_number);
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_a64_memory_operation(CodegenBuffer* buffer, u32 register_number, u32 offset, u32 size, bool store, bool sign_extend)
{
    return codegen_canonical_a64_memory_operation_base(buffer, register_number, offset, size, store, sign_extend, 31);
}

bool codegen_canonical_a64_frame_memory_operation(CodegenBuffer* buffer, u32 register_number, u32 offset, u32 size, bool store,
                                                                      bool sign_extend)
{
    return codegen_canonical_a64_memory_operation_base(buffer, register_number, offset, size, store, sign_extend, 28);
}

u32 codegen_canonical_a64_remainder_divide_instruction(bool signed_remainder, bool wide)
{
    return (signed_remainder ? 0x1aca0d2b : 0x1aca092b) | (wide ? 0x80000000 : 0);
}

BUSTER_GLOBAL_LOCAL u32 codegen_canonical_copy_chunk(u64 remaining, u64 source_offset, u64 destination_offset)
{
    if (remaining >= 8 && source_offset % 8 == 0 && destination_offset % 8 == 0)
    {
        return 8;
    }
    if (remaining >= 4 && source_offset % 4 == 0 && destination_offset % 4 == 0)
    {
        return 4;
    }
    if (remaining >= 2 && source_offset % 2 == 0 && destination_offset % 2 == 0)
    {
        return 2;
    }
    return 1;
}

// The address of an outgoing-argument slot. The stack pointer is only sixteen
// aligned through the body, so a slot an argument needs more alignment than
// that is reserved with room to spare and rounded up here, the same lea/add/and
// an over-aligned local is given.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_rsp_address(CodegenBuffer* buffer, u32 register_number, u32 offset, u32 alignment)
{
    if (!buffer || register_number > 15 || offset > INT32_MAX ||
        (alignment > CODEGEN_X64_STACK_ALIGNMENT && (alignment > INT32_MAX || (alignment & (alignment - 1)))))
    {
        return false;
    }
    X64Register register_index = (X64Register)register_number;
    BusterX86MetadataPhysicalOperand address_operands[2] = {
        codegen_canonical_x64_metadata_gpr(register_index, 64),
        codegen_canonical_x64_metadata_memory(X64_REGISTER_RSP, 64, offset),
    };
    if (!codegen_canonical_x64_metadata_emit(buffer, S8("LEA"), address_operands, BUSTER_ARRAY_LENGTH(address_operands)))
    {
        return false;
    }
    if (alignment > CODEGEN_X64_STACK_ALIGNMENT)
    {
        BusterX86MetadataPhysicalOperand add_operands[2] = {
            codegen_canonical_x64_metadata_gpr(register_index, 64),
            codegen_canonical_x64_metadata_immediate(alignment - 1, 32),
        };
        BusterX86MetadataPhysicalOperand and_operands[2] = {
            codegen_canonical_x64_metadata_gpr(register_index, 64),
            codegen_canonical_x64_metadata_immediate(-(s64)alignment, 32),
        };
        if (!codegen_canonical_x64_metadata_emit(buffer, S8("ADD"), add_operands, BUSTER_ARRAY_LENGTH(add_operands)) ||
            !codegen_canonical_x64_metadata_emit(buffer, S8("AND"), and_operands, BUSTER_ARRAY_LENGTH(and_operands)))
        {
            return false;
        }
    }
    return buffer->error == CODEGEN_ERROR_NONE;
}

BUSTER_GLOBAL_LOCAL s32 codegen_canonical_x64_rebase_frame_displacement(CodegenBuffer* buffer, s64 displacement, u32 frame_base_offset)
{
    s64 rebased = (s64)frame_base_offset + displacement;
    if (rebased < INT32_MIN || rebased > INT32_MAX)
    {
        if (buffer)
        {
            buffer->error = CODEGEN_ERROR_CAPACITY;
        }
        return 0;
    }
    return (s32)rebased;
}

// The caller-owned copy of one indirectly passed argument, moved from its frame
// slot into the outgoing area an eightbyte at a time. An argument that wants
// more than the stack pointer's sixteen bytes of alignment is written through
// r11 instead, holding the rounded-up address of its over-reserved slot: r11 is
// volatile, carries no argument, and every copy re-materializes it.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_copy_frame_to_rsp(CodegenBuffer* buffer, u32 source_offset, u32 frame_base_offset,
                                                                 u32 destination_offset, u32 size, u32 alignment)
{
    if (!buffer || !size || source_offset > INT32_MAX || destination_offset > INT32_MAX)
    {
        return false;
    }
    bool through_scratch = alignment > CODEGEN_X64_STACK_ALIGNMENT;
    if (through_scratch)
    {
        if (!codegen_canonical_x64_rsp_address(buffer, X64_REGISTER_R11, destination_offset, alignment))
        {
            return false;
        }
        destination_offset = 0;
    }
    u64 copied = 0;
    while (copied < size)
    {
        u64 source_offset_within_value = (u64)source_offset + copied;
        u64 destination = (u64)destination_offset + copied;
        s32 source = codegen_canonical_x64_rebase_frame_displacement(buffer, -(s64)source_offset + (s64)copied, frame_base_offset);
        u32 chunk = codegen_canonical_copy_chunk((u64)size - copied, source_offset_within_value, destination);
        if (source_offset_within_value > INT32_MAX || destination > INT32_MAX || buffer->error != CODEGEN_ERROR_NONE)
        {
            return false;
        }
        u16 width = (u16)(chunk * 8);
        BusterX86MetadataPhysicalOperand load_operands[2] = {
            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, chunk <= 2 ? 32 : width),
            codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, width, source),
        };
        String8 load_mnemonic = chunk <= 2 ? S8("MOVZX") : S8("MOV");
        if (!codegen_canonical_x64_metadata_emit(buffer, load_mnemonic, load_operands, BUSTER_ARRAY_LENGTH(load_operands)))
        {
            return false;
        }
        BusterX86MetadataPhysicalOperand store_operands[2] = {
            codegen_canonical_x64_metadata_memory(through_scratch ? X64_REGISTER_R11 : X64_REGISTER_RSP, width, (s64)destination),
            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, width),
        };
        if (!codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), store_operands, BUSTER_ARRAY_LENGTH(store_operands)))
        {
            return false;
        }
        copied += chunk;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_a64_float_memory_operation_base(CodegenBuffer* buffer, u32 register_number, u32 offset, u32 size, bool store,
                                                                           u32 base_register)
{
    u32 scale = size == 16 ? 16 : size == 8 ? 8 : size == 4 ? 4 : 0;
    if (!scale || offset % scale || register_number > 31)
    {
        return false;
    }
    bool indirect = offset / scale > A64_IMM12_MAX;
    if (indirect)
    {
        codegen_canonical_a64_base_address(buffer, 16, base_register, offset);
        offset = 0;
    }
    u32 instruction = store ? (size == 16 ? 0x3d8003e0 : size == 8 ? 0xfd0003e0 : 0xbd0003e0) : (size == 16 ? 0x3dc003e0 : size == 8 ? 0xfd4003e0 : 0xbd4003e0);
    codegen_emit_u32(buffer, (instruction & ~(31u << 5)) | ((offset / scale) << 10) | ((indirect ? 16u : base_register) << 5) | register_number);
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_a64_float_memory_operation(CodegenBuffer* buffer, u32 register_number, u32 offset, u32 size, bool store)
{
    return codegen_canonical_a64_float_memory_operation_base(buffer, register_number, offset, size, store, 31);
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_a64_frame_float_memory_operation(CodegenBuffer* buffer, u32 register_number, u32 offset, u32 size, bool store)
{
    return codegen_canonical_a64_float_memory_operation_base(buffer, register_number, offset, size, store, 28);
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_a64_vector_operation(CodegenBuffer* buffer, IrProgram* program, IrFunction* function, IrInstruction* instruction,
                                                                u32 const* value_offsets)
{
    if (!instruction->operand_count)
    {
        return false;
    }
    IrTypeId operand_type_id = function->values[instruction->operands[0].value].canonical_type;
    IrType* vector = ir_type_from_id(&program->types, operand_type_id);
    IrType* element = vector ? ir_type_from_id(&program->types, vector->element_type) : 0;
    if (!vector || vector->kind != IR_TYPE_VECTOR || !element || (element->kind != IR_TYPE_INTEGER && element->kind != IR_TYPE_FLOAT) ||
        (element->bit_width != 8 && element->bit_width != 16 && element->bit_width != 32 && element->bit_width != 64) ||
        instruction->result.value == IR_ID_UNDERLYING_INVALID || vector->element_count > UINT32_MAX)
    {
        return false;
    }
    u32 lane_size = element->bit_width / 8;
    if ((u64)lane_size * vector->element_count != vector->layout.size)
    {
        return false;
    }
    u32 left_base = value_offsets[instruction->operands[0].value];
    u32 right_base = instruction->operand_count == 2 ? value_offsets[instruction->operands[1].value] : 0;
    u32 result_base = value_offsets[instruction->result.value];
    bool comparison = instruction->opcode == IR_OPCODE_BINARY && instruction->binary_operation >= IR_BINARY_VECTOR_INTEGER_EQUAL &&
                      instruction->binary_operation <= IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL;
    for (u32 lane = 0; lane < (u32)vector->element_count; lane += 1)
    {
        u32 lane_offset = lane * lane_size;
        u32 left_offset = left_base + lane_offset;
        u32 result_offset = result_base + lane_offset;
        if (instruction->opcode == IR_OPCODE_UNARY)
        {
            if (element->kind == IR_TYPE_FLOAT)
            {
                if ((element->bit_width != 32 && element->bit_width != 64) || instruction->unary_operation != IR_UNARY_VECTOR_FLOAT_NEGATE ||
                    !codegen_canonical_a64_float_memory_operation(buffer, 0, left_offset, lane_size, false))
                {
                    return false;
                }
                codegen_emit_u32(buffer, element->bit_width == 32 ? 0x1e214000 : 0x1e614000);
                if (!codegen_canonical_a64_float_memory_operation(buffer, 0, result_offset, lane_size, true))
                {
                    return false;
                }
                continue;
            }
            if (!codegen_canonical_a64_memory_operation(buffer, 9, left_offset, lane_size, false, false))
            {
                return false;
            }
            u32 encoded = instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_NEGATE        ? 0xcb0903e9
                          : instruction->unary_operation == IR_UNARY_VECTOR_INTEGER_BITWISE_NOT ? 0xaa2903e9
                                                                                                : 0;
            if (!encoded)
            {
                return false;
            }
            codegen_emit_u32(buffer, encoded);
            if (!codegen_canonical_a64_memory_operation(buffer, 9, result_offset, lane_size, true, false))
            {
                return false;
            }
            continue;
        }
        if (instruction->operand_count != 2)
        {
            return false;
        }
        u32 right_offset = right_base + lane_offset;
        IrBinaryOperation operation = instruction->binary_operation;
        if (element->kind == IR_TYPE_FLOAT)
        {
            if ((element->bit_width != 32 && element->bit_width != 64) ||
                !codegen_canonical_a64_float_memory_operation(buffer, 0, left_offset, lane_size, false) ||
                !codegen_canonical_a64_float_memory_operation(buffer, 1, right_offset, lane_size, false))
            {
                return false;
            }
            if (!comparison)
            {
                u32 encoded = operation == IR_BINARY_VECTOR_FLOAT_ADD        ? 0x1e212800
                              : operation == IR_BINARY_VECTOR_FLOAT_SUBTRACT ? 0x1e213800
                              : operation == IR_BINARY_VECTOR_FLOAT_MULTIPLY ? 0x1e210800
                              : operation == IR_BINARY_VECTOR_FLOAT_DIVIDE   ? 0x1e211800
                                                                             : 0;
                if (!encoded)
                {
                    return false;
                }
                if (element->bit_width == 64)
                {
                    encoded |= 0x00400000;
                }
                codegen_emit_u32(buffer, encoded);
                if (!codegen_canonical_a64_float_memory_operation(buffer, 0, result_offset, lane_size, true))
                {
                    return false;
                }
                continue;
            }
            codegen_emit_u32(buffer, element->bit_width == 32 ? 0x1e212000 : 0x1e612000);
            u32 condition = operation == IR_BINARY_VECTOR_FLOAT_EQUAL           ? 0
                            : operation == IR_BINARY_VECTOR_FLOAT_NOT_EQUAL     ? 1
                            : operation == IR_BINARY_VECTOR_FLOAT_LESS          ? 4
                            : operation == IR_BINARY_VECTOR_FLOAT_LESS_EQUAL    ? 9
                            : operation == IR_BINARY_VECTOR_FLOAT_GREATER       ? 12
                            : operation == IR_BINARY_VECTOR_FLOAT_GREATER_EQUAL ? 10
                                                                                : UINT32_MAX;
            if (condition == UINT32_MAX)
            {
                return false;
            }
            codegen_emit_u32(buffer, 0x1a9f07e9 | ((condition ^ 1) << 12));
        }
        else
        {
            bool signed_semantics = operation == IR_BINARY_VECTOR_SIGNED_DIVIDE || operation == IR_BINARY_VECTOR_SIGNED_REMAINDER ||
                                    (operation >= IR_BINARY_VECTOR_SIGNED_LESS && operation <= IR_BINARY_VECTOR_SIGNED_GREATER_EQUAL);
            if (!codegen_canonical_a64_memory_operation(buffer, 9, left_offset, lane_size, false, signed_semantics) ||
                !codegen_canonical_a64_memory_operation(buffer, 10, right_offset, lane_size, false, signed_semantics))
            {
                return false;
            }
            if (!comparison)
            {
                u32 encoded = 0;
                switch (operation)
                {
                case IR_BINARY_VECTOR_INTEGER_ADD:
                    encoded = 0x8b0a0129;
                    break;
                case IR_BINARY_VECTOR_INTEGER_SUBTRACT:
                    encoded = 0xcb0a0129;
                    break;
                case IR_BINARY_VECTOR_INTEGER_MULTIPLY:
                    encoded = 0x9b0a7d29;
                    break;
                case IR_BINARY_VECTOR_SIGNED_DIVIDE:
                    encoded = 0x9aca0d29;
                    break;
                case IR_BINARY_VECTOR_UNSIGNED_DIVIDE:
                    encoded = 0x9aca0929;
                    break;
                case IR_BINARY_VECTOR_SIGNED_REMAINDER:
                    codegen_emit_u32(buffer, 0x9aca0d2b);
                    encoded = 0x9b0aa569;
                    break;
                case IR_BINARY_VECTOR_UNSIGNED_REMAINDER:
                    codegen_emit_u32(buffer, 0x9aca096b);
                    encoded = 0x9b0aa569;
                    break;
                case IR_BINARY_VECTOR_SHIFT_LEFT:
                    encoded = 0x9aca2129;
                    break;
                case IR_BINARY_VECTOR_SIGNED_SHIFT_RIGHT:
                    encoded = 0x9aca2929;
                    break;
                case IR_BINARY_VECTOR_UNSIGNED_SHIFT_RIGHT:
                    encoded = 0x9aca2529;
                    break;
                case IR_BINARY_VECTOR_INTEGER_BITWISE_AND:
                    encoded = 0x8a0a0129;
                    break;
                case IR_BINARY_VECTOR_INTEGER_BITWISE_OR:
                    encoded = 0xaa0a0129;
                    break;
                case IR_BINARY_VECTOR_INTEGER_BITWISE_XOR:
                    encoded = 0xca0a0129;
                    break;
                default:
                    return false;
                }
                codegen_emit_u32(buffer, encoded);
                if (!codegen_canonical_a64_memory_operation(buffer, 9, result_offset, lane_size, true, false))
                {
                    return false;
                }
                continue;
            }
            codegen_emit_u32(buffer, 0xeb0a013f);
            u32 encoded = operation == IR_BINARY_VECTOR_INTEGER_EQUAL            ? 0x1a9f17e9
                          : operation == IR_BINARY_VECTOR_INTEGER_NOT_EQUAL      ? 0x1a9f07e9
                          : operation == IR_BINARY_VECTOR_SIGNED_LESS            ? 0x1a9fa7e9
                          : operation == IR_BINARY_VECTOR_SIGNED_LESS_EQUAL      ? 0x1a9fc7e9
                          : operation == IR_BINARY_VECTOR_SIGNED_GREATER         ? 0x1a9fd7e9
                          : operation == IR_BINARY_VECTOR_SIGNED_GREATER_EQUAL   ? 0x1a9fb7e9
                          : operation == IR_BINARY_VECTOR_UNSIGNED_LESS          ? 0x1a9f27e9
                          : operation == IR_BINARY_VECTOR_UNSIGNED_LESS_EQUAL    ? 0x1a9f87e9
                          : operation == IR_BINARY_VECTOR_UNSIGNED_GREATER       ? 0x1a9f97e9
                          : operation == IR_BINARY_VECTOR_UNSIGNED_GREATER_EQUAL ? 0x1a9f37e9
                                                                                 : 0;
            if (!encoded)
            {
                return false;
            }
            codegen_emit_u32(buffer, encoded);
        }
        codegen_emit_u32(buffer, 0xcb0903e9);
        if (!codegen_canonical_a64_memory_operation(buffer, 9, result_offset, lane_size, true, false))
        {
            return false;
        }
    }
    return true;
}

BUSTER_GLOBAL_LOCAL u8* codegen_canonical_direct_call_uses(Arena* arena, IrFunction* function)
{
    u8* uses = arena_allocate(arena, u8, function->value_count);
    memset(uses, 0, sizeof(*uses) * function->value_count);
    for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
    {
        IrInstruction* instruction = function->instructions + instruction_index;
        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
        {
            IrValueId operand = instruction->operands[operand_index];
            if (operand.value >= function->value_count || uses[operand.value] == 2)
            {
                continue;
            }
            bool direct = instruction->opcode == IR_OPCODE_CALL && operand_index == 0;
            if (direct)
            {
                IrInstructionId definition = function->values[operand.value].definition;
                IrInstruction* reference = definition.value < function->instruction_count ? function->instructions + definition.value : 0;
                direct = reference && reference->opcode == IR_OPCODE_FUNCTION && reference->symbol.value == instruction->symbol.value;
            }
            uses[operand.value] = direct ? 1 : 2;
        }
    }
    return uses;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_value_is_global_place(IrFunction* function, u32 value_index)
{
    if (!function || value_index >= function->value_count)
    {
        return false;
    }
    IrInstructionId definition = function->values[value_index].definition;
    return definition.value < function->instruction_count && function->instructions[definition.value].opcode == IR_OPCODE_GLOBAL;
}

// A branch whose target block was not placed when the branch was emitted. The
// generator records the field to overwrite and fills every one of them once
// the block offsets are known.
typedef struct CCanonicalBranchPatch CCanonicalBranchPatch;
struct CCanonicalBranchPatch
{
    IrBlockId target;
    u32 offset;
    u32 secondary_offset;
    bool aarch64;
    bool conditional;
    bool label_address;
    u8 reserved[3];
};

// The per-function emission state the canonical generator's inner loop works
// against: the code buffer, the frame each canonical value owns a slot in, and
// the two records that survive between instructions -- the forwarded rax store
// and the pending branch patches. Gathering it here is what lets the load,
// store and address helpers below be ordinary functions instead of macros
// reaching into the generator's locals.
typedef struct CCanonicalEmitter CCanonicalEmitter;
struct CCanonicalEmitter
{
    CodegenBuffer* buffer;
    u32 const* value_offsets;
    u32 frame_base_offset;
    bool save_rbx;
    u32 rbx_save_offset;
    // Buffer position immediately after the last full-width rax store and the
    // frame displacement it wrote. While nothing else has been emitted, rax
    // still holds that slot, so reloading it is a no-op. Any other emission
    // moves the position and invalidates the record.
    u64 forwarded_store_end;
    s32 forwarded_store_displacement;
    CCanonicalBranchPatch* branch_patches;
    u32 branch_patch_count;
    u32 branch_patch_capacity;
};

// False only when the patch list is full, which the caller reports as a
// capacity error and retries with a larger reservation.
BUSTER_GLOBAL_LOCAL bool c_branch_patch_push(CCanonicalEmitter* emitter, CCanonicalBranchPatch patch)
{
    if (emitter->branch_patch_count >= emitter->branch_patch_capacity)
    {
        return false;
    }
    emitter->branch_patches[emitter->branch_patch_count] = patch;
    emitter->branch_patch_count += 1;
    return true;
}

BUSTER_GLOBAL_LOCAL s32 c_x64_frame_displacement(CCanonicalEmitter* emitter, u32 offset)
{
    return codegen_canonical_x64_rebase_frame_displacement(emitter->buffer, -(s64)offset, emitter->frame_base_offset);
}

BUSTER_GLOBAL_LOCAL s32 c_x64_value_displacement(CCanonicalEmitter* emitter, IrValueId value_id)
{
    return c_x64_frame_displacement(emitter, emitter->value_offsets[value_id.value]);
}

// `register_opcode` is the ModRM byte the caller would have emitted; its
// register field names the destination. 0x85 is the plain rax reload, the only
// one the forwarded store can answer.
BUSTER_GLOBAL_LOCAL void c_x64_load(CCanonicalEmitter* emitter, u8 register_opcode, IrValueId value_id)
{
    s32 displacement = c_x64_value_displacement(emitter, value_id);
    if (register_opcode == 0x85 && emitter->buffer->count == emitter->forwarded_store_end && displacement == emitter->forwarded_store_displacement)
    {
        return;
    }
    codegen_canonical_x64_asm_load(emitter->buffer, (X64Register)((register_opcode >> 3) & 7), X64_REGISTER_RBP, (u32)displacement, 8);
}

BUSTER_GLOBAL_LOCAL void c_x64_load_high(CCanonicalEmitter* emitter, u8 register_opcode, IrValueId value_id)
{
    s32 displacement = c_x64_value_displacement(emitter, value_id);
    codegen_canonical_x64_asm_load(emitter->buffer, (X64Register)((register_opcode >> 3) & 7), X64_REGISTER_RBP, (u32)(displacement + 8), 8);
}

BUSTER_GLOBAL_LOCAL void c_x64_store_result(CCanonicalEmitter* emitter, s32 result_displacement)
{
    codegen_canonical_x64_asm_store(emitter->buffer, X64_REGISTER_RBP, X64_REGISTER_RAX, (u32)result_displacement, 8);
    if (!emitter->buffer->error)
    {
        emitter->forwarded_store_end = emitter->buffer->count;
        emitter->forwarded_store_displacement = result_displacement;
    }
}

BUSTER_GLOBAL_LOCAL void c_x64_store_high_rdx(CCanonicalEmitter* emitter, s32 result_displacement)
{
    codegen_canonical_x64_asm_store(emitter->buffer, X64_REGISTER_RBP, X64_REGISTER_RDX, (u32)(result_displacement + 8), 8);
}

// Leaves the addressed location in r10, either by loading the pointer value or
// by taking the address of the place's own frame slot.
BUSTER_GLOBAL_LOCAL void c_x64_atomic_address(CCanonicalEmitter* emitter, IrValueId place_id, bool indirect_place)
{
    if (indirect_place)
    {
        c_x64_load(emitter, 0x85, place_id);
        BusterX86MetadataPhysicalOperand move_operands[2] = {
            codegen_canonical_x64_metadata_gpr(X64_REGISTER_R10, 64),
            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
        };
        (void)codegen_canonical_x64_metadata_emit(emitter->buffer, S8("MOV"), move_operands, BUSTER_ARRAY_LENGTH(move_operands));
    }
    else
    {
        BusterX86MetadataPhysicalOperand address_operands[2] = {
            codegen_canonical_x64_metadata_gpr(X64_REGISTER_R10, 64),
            codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, c_x64_value_displacement(emitter, place_id)),
        };
        (void)codegen_canonical_x64_metadata_emit(emitter->buffer, S8("LEA"), address_operands, BUSTER_ARRAY_LENGTH(address_operands));
    }
}

BUSTER_GLOBAL_LOCAL void c_x64_load_float(CCanonicalEmitter* emitter, u32 register_index, IrValueId value_id, u32 width)
{
    BusterX86MetadataPhysicalOperand load_operands[2] = {
        codegen_canonical_x64_metadata_vector(register_index, (u16)width),
        codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, (u16)width, c_x64_value_displacement(emitter, value_id)),
    };
    String8 load_features[] = {width == 32 ? S8("sse") : S8("sse2")};
    (void)codegen_canonical_x64_metadata_emit_features(emitter->buffer, width == 32 ? S8("MOVSS") : S8("MOVSD"), load_operands,
                                                       BUSTER_ARRAY_LENGTH(load_operands),
                                                       (BusterX86MetadataFeatureInput){.names = load_features,
                                                                                       .count = BUSTER_ARRAY_LENGTH(load_features)});
}

BUSTER_GLOBAL_LOCAL void c_x64_restore_rbx(CCanonicalEmitter* emitter)
{
    if (!emitter->save_rbx)
    {
        return;
    }
    BusterX86MetadataPhysicalOperand restore_operands[2] = {
        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RBX, 64),
        codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, c_x64_frame_displacement(emitter, emitter->rbx_save_offset)),
    };
    (void)codegen_canonical_x64_metadata_emit(emitter->buffer, S8("MOV"), restore_operands, BUSTER_ARRAY_LENGTH(restore_operands));
}

BUSTER_GLOBAL_LOCAL void c_a64_load(CCanonicalEmitter* emitter, u32 register_number, IrValueId value_id)
{
    (void)codegen_canonical_a64_frame_memory_operation(emitter->buffer, register_number, emitter->value_offsets[value_id.value], 8, false, false);
}

BUSTER_GLOBAL_LOCAL void c_a64_store(CCanonicalEmitter* emitter, u32 register_number, u32 result_offset)
{
    (void)codegen_canonical_a64_frame_memory_operation(emitter->buffer, register_number, result_offset, 8, true, false);
}

// One generation of the whole module -- globals, functions and global assembly
// -- into a code buffer reserved at `capacity_scale` times the flat estimate
// below. Everything it produces comes out of `arena`, so a caller that does not
// like the answer can rewind and ask again; the target, the program ABI and the
// IR validation are its caller's business and are not repeated per attempt.
BUSTER_GLOBAL_LOCAL CodegenModule codegen_generate_canonical_module_attempt(Arena* arena, IrProgram* program,
                                                                           CodegenCanonicalX64F80Cache const* f80_cache, IrModule* module,
                                                                           Target target, CodegenModuleOptions options, u64 capacity_scale,
                                                                           bool* code_buffer_exhausted,
                                                                           CodegenX64MetadataCache* x64_metadata_cache)
{
    CodegenModule result = {
        .ir_module = module,
        .abi = codegen_abi_for_target(target),
    };
    result.globals = arena_allocate(arena, CodegenModuleGlobal, module->global_count);
    u64 read_only_capacity = 0;
    u64 writable_capacity = 0;
    u64 thread_local_capacity = 0;
    u64 zero_fill_capacity = 0;
    u64 thread_local_zero_capacity = 0;
    for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
    {
        IrGlobal* global = module->globals + global_index;
        IrType* type = ir_type_from_id(&program->types, global->type);
        if (!type || !type->layout.resolved || !type->layout.alignment || type->layout.size > UINT32_MAX)
        {
            result.error = CODEGEN_ERROR_INVALID_IR;
            return result;
        }
        u32 global_alignment = global->alignment ? global->alignment : type->layout.alignment;
        if (global_alignment < type->layout.alignment || (global_alignment & (global_alignment - 1)))
        {
            result.error = CODEGEN_ERROR_INVALID_IR;
            return result;
        }
        bool zero_fill = !global->is_read_only && global->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO;
        u64* capacity = zero_fill && global->is_thread_local ? &thread_local_zero_capacity
                        : zero_fill                           ? &zero_fill_capacity
                        : global->is_thread_local ? &thread_local_capacity
                        : global->is_read_only    ? &read_only_capacity
                                                  : &writable_capacity;
        u64 remainder = *capacity % global_alignment;
        if (remainder)
        {
            *capacity += global_alignment - remainder;
        }
        *capacity += type->layout.size;
    }
    u8* read_only_bytes = arena_allocate(arena, u8, read_only_capacity);
    u8* writable_bytes = arena_allocate(arena, u8, writable_capacity);
    u8* thread_local_bytes = arena_allocate(arena, u8, thread_local_capacity);
    if (read_only_capacity)
    {
        memset(read_only_bytes, 0, read_only_capacity);
    }
    if (writable_capacity)
    {
        memset(writable_bytes, 0, writable_capacity);
    }
    if (thread_local_capacity)
    {
        memset(thread_local_bytes, 0, thread_local_capacity);
    }
    u64 read_only_count = 0;
    u64 writable_count = 0;
    u64 thread_local_count = 0;
    u64 zero_fill_count = 0;
    u64 thread_local_zero_count = 0;
    for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
    {
        IrGlobal* global = module->globals + global_index;
        IrType* type = ir_type_from_id(&program->types, global->type);
        u32 global_alignment = global->alignment ? global->alignment : type->layout.alignment;
        bool zero_fill = !global->is_read_only && global->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO;
        u64* count = zero_fill && global->is_thread_local ? &thread_local_zero_count
                     : zero_fill                           ? &zero_fill_count
                     : global->is_thread_local ? &thread_local_count
                     : global->is_read_only    ? &read_only_count
                                               : &writable_count;
        u8* bytes = zero_fill ? 0 : global->is_thread_local ? thread_local_bytes : global->is_read_only ? read_only_bytes : writable_bytes;
        u64 remainder = *count % global_alignment;
        if (remainder)
        {
            *count += global_alignment - remainder;
        }
        u32 offset = (u32)*count;
        result.globals[result.global_count++] = (CodegenModuleGlobal){
            .symbol = global->symbol,
            .offset = offset,
            .size = (u32)type->layout.size,
            .alignment = global_alignment,
            .read_only = global->is_read_only,
            .is_thread_local = global->is_thread_local,
            .zero_fill = zero_fill,
        };
        if (global->initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER || global->initializer_kind == IR_GLOBAL_INITIALIZER_FLOAT)
        {
            u64 bits = global->initializer_bits;
            if (global->initializer_kind == IR_GLOBAL_INITIALIZER_INTEGER && global->initializer_is_negative)
            {
                bits = 0 - bits;
            }
            u64 copy_size = BUSTER_MIN(type->layout.size, sizeof(bits));
            memcpy(bytes + offset, &bits, copy_size);
        }
        else if (global->initializer_kind == IR_GLOBAL_INITIALIZER_BYTES)
        {
            memcpy(bytes + offset, global->bytes.pointer, global->bytes.length);
        }
        else if (global->initializer_kind != IR_GLOBAL_INITIALIZER_ZERO && global->initializer_kind != IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS)
        {
            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
            return result;
        }
        *count += type->layout.size;
    }
    result.read_only_data = (ByteSlice){
        .pointer = read_only_bytes,
        .length = read_only_count,
    };
    result.writable_data = (ByteSlice){
        .pointer = writable_bytes,
        .length = writable_count,
    };
    result.zero_fill_size = zero_fill_count;
    result.thread_local_data = (ByteSlice){
        .pointer = thread_local_bytes,
        .length = thread_local_count,
    };
    result.thread_local_zero_size = thread_local_zero_count;
    u64 assembly_capacity = 0;
    // Alignment padding is the one part of global assembly whose size is not
    // bounded by the source that asks for it: a dozen source bytes of
    // `.p2align 12` can demand 4095 bytes of padding. It is reserved separately
    // so the source-length term keeps bounding the label entries below.
    u64 assembly_alignment_capacity = 0;
    for (u32 assembly_index = 0; assembly_index < module->assembly_count; assembly_index += 1)
    {
        assembly_capacity += module->assemblies[assembly_index].source.length;
        assembly_alignment_capacity += codegen_global_assembly_alignment_padding(module->assemblies[assembly_index].source);
    }
    u32 entry_capacity = module->function_count + (u32)BUSTER_MIN(assembly_capacity, UINT32_MAX - module->function_count);
    result.entries = arena_allocate(arena, CodegenModuleEntry, entry_capacity);
    result.functions = arena_allocate(arena, CodegenFunctionDescriptor, entry_capacity);
    u32 instruction_count = 0;
    u64 debug_location_capacity_64 = 0;
    u64 stack_probe_capacity = 0;
    u64 aligned_argument_capacity = 0;
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        instruction_count += function->instruction_count;
        u64 local_capacity = function->debug_local_count ? function->debug_local_count : function->local_count;
        debug_location_capacity_64 += local_capacity * ((u64)function->block_count + 1);
        if (debug_location_capacity_64 > UINT32_MAX)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
        u64 function_value_bytes = 0;
        for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
        {
            IrType* value_type = ir_type_from_id(&program->types, function->values[value_index].canonical_type);
            if (!value_type || !value_type->layout.resolved || value_type->layout.size > UINT32_MAX - 7)
            {
                result.error = CODEGEN_ERROR_INVALID_IR;
                return result;
            }
            bool global_place = codegen_canonical_value_is_global_place(function, value_index);
            u64 slot_size = global_place ? 8 : (value_type->layout.size + 7) & ~(u64)7;
            slot_size = BUSTER_MAX(slot_size, 8u);
            u64 slot_alignment = global_place ? 8 : BUSTER_MAX(BUSTER_MAX(value_type->layout.alignment, function->values[value_index].alignment), 8u);
            if (target.cpu_arch == CPU_ARCH_X86_64)
            {
                function_value_bytes += slot_size;
            }
            // slot_alignment is always a power of two: type layout alignments
            // bottom out in target_data_layout's 1..16 table (aggregates take
            // a max of member alignments, vectors a power-of-two byte size),
            // and requested value alignments pass c_ir_alignment_evaluate's
            // power-of-two check. That licenses align_forward's mask here and
            // in the offset-assignment loop below; a `%` compiles to a
            // hardware divide in a loop that visits every value of every
            // function.
            function_value_bytes = align_forward(function_value_bytes, slot_alignment);
            if (target.cpu_arch == CPU_ARCH_AARCH64)
            {
                function_value_bytes += slot_size;
            }
            if (function->values[value_index].alignment > 16 && function->values[value_index].definition.value < function->instruction_count &&
                function->instructions[function->values[value_index].definition.value].opcode == IR_OPCODE_LOCAL)
            {
                function_value_bytes += value_type->layout.size + function->values[value_index].alignment - 1;
            }
            // A value this wide can be handed to a call on the stack, and an
            // area aligned for it is filled an eightbyte at a time rather than
            // pushed. That is more code than the flat per-instruction reserve
            // below carries, so the value pays for the copy it can provoke.
            if (target.cpu_arch == CPU_ARCH_X86_64 && slot_alignment > CODEGEN_X64_STACK_ALIGNMENT)
            {
                aligned_argument_capacity += (slot_size / 8) * 15 + 32;
            }
        }
        u64 probe_count = (function_value_bytes + A64_SP_ADJUST_CHUNK - 1) / A64_SP_ADJUST_CHUNK;
        stack_probe_capacity += probe_count * 11;
    }
    u32 debug_location_capacity = (u32)debug_location_capacity_64;
    u32 global_relocation_count = 0;
    for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
    {
        IrGlobal* global = module->globals + global_index;
        global_relocation_count += global->relocation_count;
        global_relocation_count += global->initializer_kind == IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS;
    }
    result.relocations = arena_allocate(arena, CodegenModuleRelocation, instruction_count * 3 + global_relocation_count);
    for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
    {
        IrGlobal* global = module->globals + global_index;
        CodegenModuleGlobal generated = result.globals[global_index];
        if (global->initializer_kind == IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS)
        {
            result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                .symbol = global->initializer_symbol,
                .addend = global->initializer_addend,
                .offset = generated.offset,
                .source = generated.is_thread_local ? CODEGEN_MODULE_RELOCATION_THREAD_LOCAL_DATA
                          : generated.read_only     ? CODEGEN_MODULE_RELOCATION_READ_ONLY_DATA
                                                    : CODEGEN_MODULE_RELOCATION_DATA,
                .kind = CODEGEN_MODULE_RELOCATION_ABSOLUTE64,
                .absolute = true,
            };
        }
        for (u32 relocation_index = 0; relocation_index < global->relocation_count; relocation_index += 1)
        {
            IrGlobalRelocation relocation = global->relocations[relocation_index];
            if (relocation.offset > UINT32_MAX - generated.offset)
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
            result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                .symbol = relocation.symbol,
                .label_block = relocation.label_block,
                .addend = relocation.addend,
                .offset = generated.offset + (u32)relocation.offset,
                .source = generated.is_thread_local ? CODEGEN_MODULE_RELOCATION_THREAD_LOCAL_DATA
                          : generated.read_only     ? CODEGEN_MODULE_RELOCATION_READ_ONLY_DATA
                                                    : CODEGEN_MODULE_RELOCATION_DATA,
                .kind = CODEGEN_MODULE_RELOCATION_ABSOLUTE64,
                .absolute = true,
                .label_address = relocation.is_label_address,
            };
        }
    }
    // Label-address relocations only come from the global initializers just
    // emitted, and each is resolved exactly once by its owning function, so
    // the per-function resolution below walks this side list instead of
    // rescanning every module relocation.
    u32* label_address_relocation_indices = arena_allocate(arena, u32, result.relocation_count);
    u32 label_address_relocation_count = 0;
    for (u32 relocation_index = 0; relocation_index < result.relocation_count; relocation_index += 1)
    {
        if (result.relocations[relocation_index].label_address)
        {
            label_address_relocation_indices[label_address_relocation_count++] = relocation_index;
        }
    }
    u64 instruction_capacity = target.cpu_arch == CPU_ARCH_AARCH64 ? 128 : 48;
    u64 capacity = ((u64)instruction_count * instruction_capacity + (u64)module->function_count * 64 + stack_probe_capacity + aligned_argument_capacity +
                    assembly_capacity * 4 + assembly_alignment_capacity + 64) *
                   capacity_scale;
    // Every offset the module hands out is a u32, so a buffer past that is
    // unusable however much of it the arena would give. This is also what ends
    // the caller's retry: a scale that cannot fit stops here instead of
    // reporting the code buffer exhausted and being doubled again.
    if (capacity > UINT32_MAX)
    {
        result.error = CODEGEN_ERROR_CAPACITY;
        return result;
    }
    CodegenBuffer buffer = {
        .bytes = arena_allocate(arena, u8, capacity),
        .capacity = capacity,
        .exhausted = code_buffer_exhausted,
        .x64_metadata_cache = x64_metadata_cache,
    };
    // Every function contributes a row for its own declaration on top of the
    // per-instruction rows.
    u32 line_entry_capacity = instruction_count + module->function_count;
    result.line_entries = options.debug_info ? arena_allocate(arena, CodegenLineEntry, line_entry_capacity) : 0;
    result.debug_locations = options.debug_info ? arena_allocate(arena, DebugLocationSeed, debug_location_capacity) : 0;
    result.debug_info = options.debug_info;
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        result.failed_function = (IrFunctionId){
            .value = function_index,
        };
        if (function->state != IR_FUNCTION_LOWERED)
        {
            continue;
        }
        u64 alignment = target.cpu_arch == CPU_ARCH_AARCH64 ? 4 : 16;
        // A full buffer stops accepting bytes without advancing its count, so
        // padding to an alignment it can no longer reach never terminates.
        while (buffer.error == CODEGEN_ERROR_NONE && buffer.count % alignment)
        {
            if (target.cpu_arch == CPU_ARCH_X86_64)
            {
                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("NOP"), 0, 0))
                {
                    result.error = buffer.error;
                    return result;
                }
            }
            else
            {
                codegen_emit_u8(&buffer, 0);
            }
        }
        if (buffer.error != CODEGEN_ERROR_NONE)
        {
            result.error = buffer.error;
            return result;
        }
        result.entries[result.entry_count++] = (CodegenModuleEntry){
            .symbol = function->symbol,
            .offset = (u32)buffer.count,
        };
        if (function->source.source.value != IR_ID_UNDERLYING_INVALID)
        {
            // A row at the function start makes the prologue map to the
            // declaration line instead of falling outside the line table.
            IrSourcePosition declaration = ir_source_position(program, function->source);
            codegen_record_line_hot(result.line_entries, &result.line_entry_count, line_entry_capacity, (u32)buffer.count,
                                    function->source.source.value, declaration.line, declaration.column);
        }
        // The declaration row is not an instruction's; the next instruction
        // must still be able to record one.
        IrSourceRange recorded_source = {.source = IR_SOURCE_ID_INVALID, .offset = UINT32_MAX};
        IrBlock* entry = function->blocks + function->entry.value;
        if (entry->first_instruction.value >= function->instruction_count)
        {
            result.error = CODEGEN_ERROR_INVALID_IR;
            return result;
        }
        bool x64_save_rbx = false;
        if (target.cpu_arch == CPU_ARCH_X86_64 && !codegen_canonical_x64_function_shape(function, &x64_save_rbx))
        {
            result.error = CODEGEN_ERROR_INVALID_IR;
            return result;
        }
        CodegenFunctionDescriptor* descriptor = result.functions + result.function_count;
        result.function_count += 1;
        *descriptor = (CodegenFunctionDescriptor){
            .symbol = function->symbol,
            .code_offset = result.entries[result.entry_count - 1].offset,
        };
        result.statistics.function_count += 1;
        result.statistics.instruction_count += function->instruction_count;
        result.statistics.value_count += function->value_count;
        u32 unwind_action_capacity = 0;
        u32 machine_simd_operation_count = 0;
        u32 machine_stack_frame_size = 0;
        bool machine_function_emitted = false;
        // Selection is attempted before canonical-only frame, ABI, and call
        // metadata is built. A supported machine function never needs that
        // preparation; the fallback edge below enters it exactly once.
        goto machine_attempt;

    canonical_prep:
        ;
        // This state is reached only for NONE/PE-unwind paths or a machine
        // attempt that could not be kept. It owns all canonical emitter data.
        bool windows_aarch64 = target.cpu_arch == CPU_ARCH_AARCH64 && target_uses_pe_unwind(target);
        bool windows_dynamic_stack = false;
        if (target.cpu_arch == CPU_ARCH_X86_64 && result.abi == CODEGEN_ABI_X86_64_WINDOWS)
        {
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrOpcode opcode = function->instructions[instruction_index].opcode;
                windows_dynamic_stack |= opcode == IR_OPCODE_STACK_ALLOCATE || opcode == IR_OPCODE_STACK_RESTORE;
            }
        }
        bool x64_aligned_argument_call = false;
        u32* value_offsets = arena_allocate(arena, u32, function->value_count);
        u8* direct_call_uses = codegen_canonical_direct_call_uses(arena, function);
        // The Windows x64 sizing pass below already computes every call's
        // layout to find the outgoing stack area; keep those layouts so the
        // emission pass reuses them instead of recomputing per call. Other
        // ABIs compute layouts once during emission exactly as before.
        CodegenCanonicalCallLayout** call_layout_cache = 0;
        if (target.cpu_arch == CPU_ARCH_X86_64 && result.abi == CODEGEN_ABI_X86_64_WINDOWS)
        {
            call_layout_cache = arena_allocate(arena, CodegenCanonicalCallLayout*, function->instruction_count);
            memset(call_layout_cache, 0, sizeof(*call_layout_cache) * function->instruction_count);
        }
        // Keep the x28 frame-base save in the directly encodable ARM64
        // Windows unwind range. Ordinary values start after its reserved slot.
        u64 value_bytes = windows_aarch64 ? 16 : 0;
        for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
        {
            IrType* value_type = ir_type_from_id(&program->types, function->values[value_index].canonical_type);
            if (!value_type || !value_type->layout.resolved || value_type->layout.size > UINT32_MAX - 7)
            {
                result.error = CODEGEN_ERROR_INVALID_IR;
                return result;
            }
            bool global_place = codegen_canonical_value_is_global_place(function, value_index);
            u32 slot_size = global_place ? 8 : ((u32)value_type->layout.size + 7) & ~(u32)7;
            slot_size = BUSTER_MAX(slot_size, 8u);
            u64 slot_alignment = global_place ? 8 : BUSTER_MAX(BUSTER_MAX(value_type->layout.alignment, function->values[value_index].alignment), 8u);
            if (target.cpu_arch == CPU_ARCH_X86_64)
            {
                value_bytes += slot_size;
            }
            // Power-of-two slot_alignment; see the capacity-estimation loop.
            value_bytes = align_forward(value_bytes, slot_alignment);
            if (value_bytes > UINT32_MAX)
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
            value_offsets[value_index] = (u32)value_bytes;
            // A value wanting more than sixteen bytes is a value a call can be
            // asked to pass on the stack, and such a call has to realign the
            // stack pointer and put it back afterwards from somewhere a call
            // cannot clobber. The slot alignment this loop already computed
            // answers that, and answers yes a little too often -- an
            // over-aligned local that is never an argument also reserves the
            // eight bytes -- which costs a frame slot and no work.
            x64_aligned_argument_call |= slot_alignment > CODEGEN_X64_STACK_ALIGNMENT;
            if (target.cpu_arch == CPU_ARCH_AARCH64)
            {
                value_bytes += slot_size;
                if (value_bytes > UINT32_MAX)
                {
                    result.error = CODEGEN_ERROR_CAPACITY;
                    return result;
                }
            }
        }
        u32 x64_stack_save_offset = 0;
        if (x64_aligned_argument_call && target.cpu_arch == CPU_ARCH_X86_64 && result.abi == CODEGEN_ABI_X86_64_SYSTEM_V)
        {
            if (value_bytes > UINT32_MAX - 8)
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
            value_bytes += 8;
            x64_stack_save_offset = (u32)value_bytes;
        }
        u32 x64_rbx_save_offset = 0;
        if (x64_save_rbx)
        {
            if (value_bytes > UINT32_MAX - 8)
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
            x64_rbx_save_offset = (u32)value_bytes + 8;
            value_bytes += 8;
        }
        u32* aligned_local_offsets = arena_allocate(arena, u32, function->value_count);
        for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
        {
            IrValue* value = function->values + value_index;
            if (value->alignment <= 16 || value->definition.value >= function->instruction_count ||
                function->instructions[value->definition.value].opcode != IR_OPCODE_LOCAL)
            {
                continue;
            }
            IrType* value_type = ir_type_from_id(&program->types, value->canonical_type);
            if (!value_type || !value_type->layout.resolved || value_type->layout.size > UINT32_MAX - (value->alignment - 1))
            {
                result.error = CODEGEN_ERROR_INVALID_IR;
                return result;
            }
            u64 raw_size = value_type->layout.size + value->alignment - 1;
            if (target.cpu_arch == CPU_ARCH_AARCH64)
            {
                aligned_local_offsets[value_index] = (u32)value_bytes;
            }
            value_bytes += raw_size;
            if (value_bytes > UINT32_MAX)
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
            if (target.cpu_arch == CPU_ARCH_X86_64)
            {
                aligned_local_offsets[value_index] = (u32)value_bytes;
            }
        }
        IrType* canonical_function_type = ir_type_from_id(&program->types, function->canonical_type);
        IrTypeId canonical_return_type =
            canonical_function_type && canonical_function_type->kind == IR_TYPE_FUNCTION ? canonical_function_type->return_type : IR_TYPE_ID_INVALID;
        bool canonical_variadic = canonical_function_type && canonical_function_type->kind == IR_TYPE_FUNCTION && canonical_function_type->is_variadic;
        if (target.cpu_arch == CPU_ARCH_X86_64)
        {
            bool canonical_f80_supported = result.abi == CODEGEN_ABI_X86_64_SYSTEM_V;
            if (!canonical_f80_supported)
            {
                bool has_f80 = codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, canonical_return_type);
                if (canonical_function_type && canonical_function_type->kind == IR_TYPE_FUNCTION)
                {
                    for (u32 parameter_index = 0; parameter_index < canonical_function_type->parameter_count && !has_f80; parameter_index += 1)
                    {
                        has_f80 |= codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, canonical_function_type->parameter_types[parameter_index]);
                    }
                }
                for (u32 value_index = 0; value_index < function->value_count && !has_f80; value_index += 1)
                {
                    has_f80 |= codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, function->values[value_index].canonical_type);
                }
                if (has_f80)
                {
                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                    return result;
                }
            }
            else
            {
                // The recursive contains query is broader than the x87 payload
                // we can interpret.  Reject incompatible aggregate shapes
                // before an instruction-specific path can mistake their bytes
                // for scalar f80 data.
                if (codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, canonical_return_type) &&
                    !codegen_canonical_x64_type_is_f80_x87_shape_cached(f80_cache, program, canonical_return_type))
                {
                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                    return result;
                }
                if (canonical_function_type && canonical_function_type->kind == IR_TYPE_FUNCTION)
                {
                    for (u32 parameter_index = 0; parameter_index < canonical_function_type->parameter_count; parameter_index += 1)
                    {
                        IrTypeId parameter_type_id = canonical_function_type->parameter_types[parameter_index];
                        if (codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, parameter_type_id) &&
                            !codegen_canonical_x64_type_is_f80_x87_shape_cached(f80_cache, program, parameter_type_id))
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                            return result;
                        }
                    }
                }
                for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
                {
                    IrTypeId value_type_id = function->values[value_index].canonical_type;
                    if (codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, value_type_id) &&
                        !codegen_canonical_x64_type_is_f80_x87_shape_cached(f80_cache, program, value_type_id))
                    {
                        result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                        return result;
                    }
                }
            }
        }
        CodegenCanonicalAbiValue canonical_return_abi = codegen_canonical_aggregate_abi(program, canonical_return_type, result.abi, true, false);
        bool windows_indirect_return = target.cpu_arch == CPU_ARCH_X86_64 && result.abi == CODEGEN_ABI_X86_64_WINDOWS && canonical_return_abi.indirect;
        bool system_v_indirect_return = target.cpu_arch == CPU_ARCH_X86_64 && result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && canonical_return_abi.indirect;
        bool x64_indirect_return = windows_indirect_return || system_v_indirect_return;
        bool aarch64_indirect_return = target.cpu_arch == CPU_ARCH_AARCH64 && canonical_return_abi.indirect;
        u64 frame_size_64 = (value_bytes + 7) & ~(u64)7;
        u32 aarch64_va_save_offset = 0;
        bool aarch64_darwin = target.cpu_arch == CPU_ARCH_AARCH64 &&
                              (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS);
        bool aarch64_darwin_variadic = canonical_variadic && aarch64_darwin;
        if (target.cpu_arch == CPU_ARCH_AARCH64 && canonical_variadic && !aarch64_darwin_variadic)
        {
            aarch64_va_save_offset = (u32)frame_size_64;
            frame_size_64 += 64;
        }
        s32 canonical_va_save_displacement = 0;
        if (target.cpu_arch == CPU_ARCH_X86_64 && result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && canonical_variadic)
        {
            canonical_va_save_displacement = -(s32)(frame_size_64 + 176);
            frame_size_64 += 176;
        }
        s32 hidden_result_displacement = -(s32)(frame_size_64 + 8);
        u32 aarch64_hidden_result_offset = (u32)frame_size_64;
        if (x64_indirect_return || aarch64_indirect_return)
        {
            frame_size_64 += 8;
        }
        u32 aarch64_frame_base_save_offset = 0;
        if (target.cpu_arch == CPU_ARCH_AARCH64)
        {
            if (!windows_aarch64)
            {
                aarch64_frame_base_save_offset = (u32)frame_size_64;
                frame_size_64 += 8;
            }
        }
        frame_size_64 = (frame_size_64 + 15) & ~(u64)15;
        if (frame_size_64 > UINT32_MAX)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
        u32 frame_size = (u32)frame_size_64;
        u32 windows_outgoing_size = 0;
        if (target.cpu_arch == CPU_ARCH_X86_64 && result.abi == CODEGEN_ABI_X86_64_WINDOWS)
        {
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = function->instructions + instruction_index;
                if (instruction->opcode != IR_OPCODE_CALL)
                {
                    continue;
                }
                CodegenCanonicalCallLayout* call_layout = arena_allocate(arena, CodegenCanonicalCallLayout, 1);
                *call_layout = (CodegenCanonicalCallLayout){0};
                CodegenError call_error =
                    codegen_canonical_x64_call_layout_cached(arena, program, f80_cache, function, instruction, result.abi, target, call_layout);
                if (call_error != CODEGEN_ERROR_NONE)
                {
                    // This pass runs before the emitting one that keeps the
                    // failing instruction up to date, so it has to name its own
                    // call or the diagnostic blames whatever ran last.
                    result.failed_instruction = (IrInstructionId){.value = instruction_index};
                    result.failed_opcode = instruction->opcode;
                    result.error = call_error;
                    return result;
                }
                call_layout_cache[instruction_index] = call_layout;
                windows_outgoing_size = BUSTER_MAX(windows_outgoing_size, call_layout->windows_stack_size);
            }
            if (frame_size > UINT32_MAX - windows_outgoing_size)
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
            frame_size += windows_outgoing_size;
        }
        if (windows_dynamic_stack && frame_size > INT32_MAX)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
        u32 canonical_x64_frame_base_offset = windows_dynamic_stack ? frame_size : 0;
        u32 stack_action_count = target.cpu_arch == CPU_ARCH_X86_64
                                     ? result.abi == CODEGEN_ABI_X86_64_WINDOWS ? (frame_size != 0) : frame_size / CODEGEN_X64_STACK_PROBE_PAGE + (frame_size % CODEGEN_X64_STACK_PROBE_PAGE != 0)
                                     : frame_size / A64_SP_ADJUST_CHUNK + (frame_size % A64_SP_ADJUST_CHUNK != 0);
        u32 stack_action_capacity = windows_aarch64 ? (frame_size > A64_SP_ADJUST_CHUNK ? 14u : stack_action_count * 2) : stack_action_count;
        // x86_64 holds the frame-pointer pair, up to five machine-path
        // callee-saved pushes, and the stack allocation. The AArch64
        // machine path sizes its own frame after this allocation, so the
        // allocator modes reserve room for its worst-case chunk count.
        unwind_action_capacity = (target.cpu_arch == CPU_ARCH_X86_64 ? 8u : windows_aarch64 ? 6u : 5u) + stack_action_capacity +
                                 (target.cpu_arch == CPU_ARCH_AARCH64 && options.register_allocator != CODEGEN_REGISTER_ALLOCATOR_NONE ? 20u : 0u);
        // The x86-64 machine prologue allocates a page per action whatever the
        // ABI, while the canonical Windows prologue takes the whole frame in
        // one; it also pushes up to seven callee-saved registers under Win64
        // beside the frame-pointer pair. Both counts are the machine path's
        // own, so they are taken as a floor rather than replacing the
        // canonical sizing that the fallback below still needs.
        if (target.cpu_arch == CPU_ARCH_X86_64 && options.register_allocator != CODEGEN_REGISTER_ALLOCATOR_NONE)
        {
            u32 machine_unwind_action_capacity = 9u + frame_size / CODEGEN_X64_STACK_PROBE_PAGE + (frame_size % CODEGEN_X64_STACK_PROBE_PAGE != 0);
            unwind_action_capacity = BUSTER_MAX(unwind_action_capacity, machine_unwind_action_capacity);
        }
        descriptor->unwind_actions = arena_allocate(arena, CodegenUnwindAction, unwind_action_capacity);
        descriptor->epilog_offsets = target.cpu_arch == CPU_ARCH_AARCH64 ? arena_allocate(arena, u32, function->instruction_count) : 0;
        descriptor->unwind_action_count = 0;
        descriptor->epilog_count = 0;
        result.statistics.stack_value_bytes += value_bytes;
        result.statistics.stack_frame_bytes += frame_size;
        result.statistics.maximum_stack_frame_bytes = BUSTER_MAX(result.statistics.maximum_stack_frame_bytes, frame_size);
        goto canonical_emit;

    machine_attempt:
        // The descriptor is a shell until this path knows its unwind shape;
        // canonical fallback fills the same shell after its sizing pass.
        // MIR_STACK routes eligible functions through machine selection,
        // stack placement, and the machine encoder; everything else falls
        // back to the canonical path below and is counted. The machine
        // prologue byte-for-byte matches the canonical plain prologue of
        // its architecture, so the descriptor's unwind actions keep their
        // exact meaning. PE-unwind AArch64 targets stay canonical: their
        // unwind data wants the packed-epilogue and probe-NOP shapes the machine
        // wiring does not model yet.
        if ((options.register_allocator == CODEGEN_REGISTER_ALLOCATOR_MIR_STACK || options.register_allocator == CODEGEN_REGISTER_ALLOCATOR_FAST ||
             options.register_allocator == CODEGEN_REGISTER_ALLOCATOR_QUALITY) &&
            (target.cpu_arch == CPU_ARCH_X86_64 || (target.cpu_arch == CPU_ARCH_AARCH64 && !target_uses_pe_unwind(target))))
        {
            bool label_address_target = false;
            for (u32 side_index = 0; side_index < label_address_relocation_count; side_index += 1)
            {
                label_address_target |= result.relocations[label_address_relocation_indices[side_index]].symbol.value == function->symbol.value;
            }
            TemporalArena machine_scratch = scratch_begin(&arena, 1);
            MachineSelectResult selected = {0};
            if (!label_address_target)
            {
                selected = machine_select_validated_canonical_function(machine_scratch.arena, program, function, target);
                machine_simd_operation_count = selected.simd_operation_count;
            }
            if (!selected.supported)
            {
                u32 reason = selected.failed_opcode <= IR_OPCODE_COUNT ? (u32)selected.failed_opcode : (u32)IR_OPCODE_COUNT;
                result.statistics.fallback_opcode_counts[reason] += 1;
            }
            // The target selectors publish a complete machine function only
            // after their typed builder streams and side tables are closed.
            // Keep the verifier as the authority for replayed/manual machine
            // IR, but do not reread every freshly selected row before its
            // immediate allocator consumer.
            MachineVerifyError verify_error = selected.supported && !selected.selector_certified ? machine_verify_function(&selected.function).error
                                                                                                  : MACHINE_VERIFY_NONE;
            if (selected.supported && verify_error != MACHINE_VERIFY_NONE)
            {
                result.statistics.fallback_verify_count += 1;
            }
            if (selected.supported && verify_error == MACHINE_VERIFY_NONE)
            {
                MachineStackPlacement placement;

                switch (options.register_allocator)
                {
                    break; case CODEGEN_REGISTER_ALLOCATOR_FAST: placement = machine_fast_placement_build(machine_scratch.arena, &selected.function);
                    break; case CODEGEN_REGISTER_ALLOCATOR_QUALITY: placement = machine_quality_placement_build(machine_scratch.arena, &selected.function);
                    break; default: placement = machine_stack_placement_build(machine_scratch.arena, &selected.function);
                }

                // Stage-9 scheduling, QUALITY only: reorder rows within
                // over-pressured blocks to sink definitions toward their
                // first use, then keep whichever form places cheaper. The
                // currency is the stage-7 acceptance metric — memory
                // traffic plus a push/pop pair per callee-saved register
                // the placement binds — so a schedule that trades reloads
                // for prologue saves cannot sneak through, and an unmoved
                // schedule costs nothing. FAST measured the same absolute
                // win but pays the acceptance's second placement out of the
                // budget that makes it the default -O allocator, so it
                // stays byte-identical to the unscheduled path
                // (2026-08-10n). The traffic gate bounds the pass's cost:
                // a placement that evicted nothing has nothing to save.
                if (placement.valid && placement.reload_count + placement.spill_count > 0 &&
                    options.register_allocator == CODEGEN_REGISTER_ALLOCATOR_QUALITY)
                {
                    MachineScheduleResult scheduled = machine_schedule_function(machine_scratch.arena, &selected.function);
                    if (scheduled.moved)
                    {
                        result.statistics.allocator_scheduled_function_count += 1;
                        MachineStackPlacement scheduled_placement = machine_quality_placement_build(machine_scratch.arena, &scheduled.function);
                        u32 placement_saved_registers = 0;
                        u32 scheduled_saved_registers = 0;
                        for (u32 physical_register = 0; physical_register < MACHINE_TARGET_REGISTER_LIMIT; physical_register += 1)
                        {
                            placement_saved_registers += (placement.callee_saved_mask >> physical_register) & 1u;
                            scheduled_saved_registers += (scheduled_placement.callee_saved_mask >> physical_register) & 1u;
                        }
                        if (scheduled_placement.valid &&
                            scheduled_placement.reload_count + scheduled_placement.spill_count + 2 * scheduled_saved_registers <
                                placement.reload_count + placement.spill_count + 2 * placement_saved_registers)
                        {
                            result.statistics.allocator_schedule_kept_count += 1;
                            selected.function = scheduled.function;
                            placement = scheduled_placement;
                        }
                    }
                }
                if (!placement.valid)
                {
                    result.statistics.fallback_placement_count += 1;
                }
                if (placement.valid)
                {
                    MachineEncodeResult encoded;

                    switch (target.cpu_arch)
                    {
                        break; case CPU_ARCH_AARCH64: encoded = machine_encode_aarch64(machine_scratch.arena, &selected.function, &placement);
                        break; case CPU_ARCH_X86_64: encoded = machine_encode_x86_64(machine_scratch.arena, &selected.function, &placement);
                        break; default: BUSTER_TODO();
                    }

                    // Keep exact-form telemetry even when the encoder fails;
                    // the function may still fall back to the canonical path.
                    result.statistics.exact_attempts += encoded.exact_attempts;
                    result.statistics.exact_successes += encoded.exact_successes;
                    result.statistics.exact_failures += encoded.exact_failures;
                    bool encoded_fits = encoded.valid && buffer.count <= buffer.capacity &&
                                        encoded.byte_count <= buffer.capacity - buffer.count;
                    if (encoded.valid && !encoded_fits)
                    {
                        codegen_buffer_report_exhausted(&buffer);
                    }
                    if (!encoded.valid)
                    {
                        result.statistics.fallback_encode_count += 1;
                    }
                    if (encoded_fits)
                    {
                        u32 machine_unwind_capacity = 0;
                        if (target.cpu_arch == CPU_ARCH_X86_64)
                        {
                            machine_unwind_capacity = 9u + placement.frame_size / CODEGEN_X64_STACK_PROBE_PAGE +
                                                      (placement.frame_size % CODEGEN_X64_STACK_PROBE_PAGE != 0);
                        }
                        else
                        {
                            u32 machine_saved_register_count = 0;
                            for (u32 saved_register = 0; saved_register < 32u; saved_register += 1)
                            {
                                machine_saved_register_count += (placement.callee_saved_mask >> saved_register) & 1u;
                            }
                            u32 machine_frame_total = placement.frame_size + 16u + 8u * machine_saved_register_count;
                            u32 machine_frame_chunks = machine_frame_total / A64_SP_ADJUST_CHUNK +
                                                       (machine_frame_total % A64_SP_ADJUST_CHUNK != 0);
                            machine_unwind_capacity = 6u + machine_frame_chunks + machine_saved_register_count + function->instruction_count;
                        }
                        unwind_action_capacity = machine_unwind_capacity;
                        descriptor->unwind_actions = arena_allocate(arena, CodegenUnwindAction, machine_unwind_capacity);
                        descriptor->epilog_offsets = target.cpu_arch == CPU_ARCH_AARCH64 ? arena_allocate(arena, u32, function->instruction_count) : 0;
                        descriptor->unwind_action_count = 0;
                        descriptor->epilog_count = 0;
                    }
                    if (encoded_fits && target.cpu_arch == CPU_ARCH_AARCH64)
                    {
                        // The machine prologue mirrors the canonical
                        // AArch64 shape exactly: stp x29/x30, establish
                        // x29, probed sub chunks, the callee-saved saves
                        // at the top of the frame area, x28 saved above
                        // them, x28 repointed. Every prologue instruction
                        // is one word, so the action offsets are exact.
                        u32 machine_push_count = 0;
                        for (u32 saved_register = 0; saved_register < 32u; saved_register += 1)
                        {
                            machine_push_count += (placement.callee_saved_mask >> saved_register) & 1u;
                        }
                        u32 machine_frame_area = placement.frame_size + 8 * machine_push_count;
                        u32 machine_frame_total = machine_frame_area + 16;
                        bool machine_unwind_valid =
                            codegen_unwind_action_append(descriptor, unwind_action_capacity, 4, CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, 16);
                        machine_unwind_valid =
                            codegen_unwind_action_append(descriptor, unwind_action_capacity, 4, CODEGEN_UNWIND_ACTION_SAVE_REGISTER, 29, 0) &&
                            machine_unwind_valid;
                        machine_unwind_valid =
                            codegen_unwind_action_append(descriptor, unwind_action_capacity, 4, CODEGEN_UNWIND_ACTION_SAVE_REGISTER, 30, 8) &&
                            machine_unwind_valid;
                        machine_unwind_valid =
                            codegen_unwind_action_append(descriptor, unwind_action_capacity, 8, CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, 29, 0) &&
                            machine_unwind_valid;
                        u32 machine_prologue_cursor = 8;
                        u32 machine_frame_remaining = machine_frame_total;
                        while (machine_frame_remaining)
                        {
                            u32 machine_frame_chunk = BUSTER_MIN(machine_frame_remaining, A64_SP_ADJUST_CHUNK);
                            machine_prologue_cursor += 4;
                            machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, machine_frame_chunk) &&
                                                   machine_unwind_valid;
                            machine_prologue_cursor += 4;
                            machine_frame_remaining -= machine_frame_chunk;
                        }
                        u32 machine_save_slot = 0;
                        for (u32 saved_register = 0; saved_register < 32u; saved_register += 1)
                        {
                            if (!((placement.callee_saved_mask >> saved_register) & 1u))
                            {
                                continue;
                            }
                            machine_save_slot += 1;
                            machine_prologue_cursor += 4;
                            machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                CODEGEN_UNWIND_ACTION_SAVE_REGISTER, (u8)saved_register,
                                                                                machine_frame_area - 8 * machine_save_slot) &&
                                                   machine_unwind_valid;
                        }
                        machine_prologue_cursor += 4;
                        machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                            CODEGEN_UNWIND_ACTION_SAVE_REGISTER, 28, machine_frame_area) &&
                                               machine_unwind_valid;
                        machine_prologue_cursor += 4;
                        for (u32 epilog_index = 0; epilog_index < encoded.epilog_count; epilog_index += 1)
                        {
                            machine_unwind_valid =
                                codegen_epilog_offset_append(descriptor, function->instruction_count, encoded.epilog_offsets[epilog_index]) &&
                                machine_unwind_valid;
                        }
                        if (machine_unwind_valid)
                        {
                            memcpy(buffer.bytes + buffer.count, encoded.bytes, encoded.byte_count);
                            for (u32 mark_index = 0; mark_index < selected.function.line_mark_count; mark_index += 1)
                            {
                                MachineLineMark* mark = selected.function.line_marks + mark_index;
                                if (result.line_entries && mark->row < selected.function.instruction_count)
                                {
                                    IrSourcePosition position = ir_source_position(program, (IrSourceRange){
                                                                                                .source = {.value = mark->source},
                                                                                                .offset = mark->offset,
                                                                                            });
                                    codegen_record_line_hot(result.line_entries, &result.line_entry_count, line_entry_capacity,
                                                            (u32)buffer.count + encoded.row_offsets[mark->row], mark->source, position.line,
                                                            position.column);
                                }
                            }
                            for (u32 site_index = 0; site_index < encoded.call_site_count; site_index += 1)
                            {
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = selected.function.call_targets[encoded.call_sites[site_index].target],
                                    .offset = (u32)buffer.count + encoded.call_sites[site_index].code_offset,
                                    .kind = (u8)(encoded.call_sites[site_index].absolute ? CODEGEN_MODULE_RELOCATION_ABSOLUTE64
                                                                                         : CODEGEN_MODULE_RELOCATION_AARCH64_CALL26),
                                    .aarch64 = encoded.call_sites[site_index].absolute == 0,
                                    .absolute = encoded.call_sites[site_index].absolute != 0,
                                };
                            }
                            buffer.count += encoded.byte_count;
                            descriptor->prolog_size = machine_prologue_cursor;
                            descriptor->code_size = (u32)buffer.count - descriptor->code_offset;
                            machine_function_emitted = true;
                            machine_stack_frame_size = placement.frame_size;
                            result.statistics.allocator_reload_count += placement.reload_count;
                            result.statistics.allocator_spill_count += placement.spill_count;
                            result.statistics.allocator_copy_count += placement.copy_count;
                            result.statistics.allocator_boundary_spill_count += placement.boundary_spill_count;
                            result.statistics.allocator_rematerialize_count += placement.rematerialize_count;
                            result.statistics.allocator_pinned_register_count += placement.pinned_register_count;
                            result.statistics.allocator_split_register_count += placement.split_register_count;
                        }
                    }
                    else if (encoded_fits)
                    {
                        // The encoder pushes the placement's callee-saved
                        // registers in ascending order — RBX, R12-R15 under
                        // System V, gaining RSI and RDI under Win64 — and
                        // orders them against the frame-pointer establishment
                        // the way the target description asks. Win64 puts them
                        // first: its unwind codes restore a pushed register
                        // off the stack pointer they are recovered with, which
                        // only holds while the pushes precede UWOP_SET_FPREG,
                        // and this path's calls move RSP in the body. The
                        // legacy eight push in one byte, the extended file in
                        // two, and the frame-pointer move is three, which is
                        // what makes each action's offset exact.
                        bool machine_saves_first = result.abi == CODEGEN_ABI_X86_64_WINDOWS;
                        bool machine_unwind_valid =
                            codegen_unwind_action_append(descriptor, unwind_action_capacity, 1, CODEGEN_UNWIND_ACTION_PUSH_REGISTER, X64_REGISTER_RBP, 0);
                        u32 machine_prologue_cursor = 1;
                        if (!machine_saves_first)
                        {
                            machine_prologue_cursor += 3;
                            machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, X64_REGISTER_RBP, 0) &&
                                                   machine_unwind_valid;
                        }
                        for (u32 machine_saved_register = 0; machine_saved_register < 16u; machine_saved_register += 1)
                        {
                            if (!(placement.callee_saved_mask & (1ull << machine_saved_register)))
                            {
                                continue;
                            }
                            machine_prologue_cursor += machine_saved_register < 8u ? 1u : 2u;
                            machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                CODEGEN_UNWIND_ACTION_PUSH_REGISTER, (u8)machine_saved_register, 0) &&
                                                   machine_unwind_valid;
                        }
                        if (machine_saves_first)
                        {
                            machine_prologue_cursor += 3;
                            machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, X64_REGISTER_RBP, 0) &&
                                                   machine_unwind_valid;
                        }
                        // One allocation action per emitted chunk, at the
                        // exact end offset of its subtract; the probe bytes
                        // follow each action.
                        u32 machine_frame_remaining = placement.frame_size;
                        while (machine_frame_remaining)
                        {
                            u32 machine_frame_chunk = BUSTER_MIN(machine_frame_remaining, CODEGEN_X64_STACK_PROBE_PAGE);
                            machine_prologue_cursor += machine_frame_chunk <= INT8_MAX ? 4u : 7u;
                            machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, machine_frame_chunk) &&
                                                   machine_unwind_valid;
                            machine_prologue_cursor += 4;
                            machine_frame_remaining -= machine_frame_chunk;
                        }
                        if (machine_unwind_valid)
                        {
                            memcpy(buffer.bytes + buffer.count, encoded.bytes, encoded.byte_count);
                            for (u32 mark_index = 0; mark_index < selected.function.line_mark_count; mark_index += 1)
                            {
                                MachineLineMark* mark = selected.function.line_marks + mark_index;
                                if (result.line_entries && mark->row < selected.function.instruction_count)
                                {
                                    // Positions are recovered here rather than
                                    // carried through selection, so a row that
                                    // never reaches the line table costs
                                    // nothing to resolve.
                                    IrSourcePosition position = ir_source_position(program, (IrSourceRange){
                                                                                                .source = {.value = mark->source},
                                                                                                .offset = mark->offset,
                                                                                            });
                                    codegen_record_line_hot(result.line_entries, &result.line_entry_count, line_entry_capacity,
                                                            (u32)buffer.count + encoded.row_offsets[mark->row], mark->source, position.line,
                                                            position.column);
                                }
                            }
                            for (u32 site_index = 0; site_index < encoded.call_site_count; site_index += 1)
                            {
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = selected.function.call_targets[encoded.call_sites[site_index].target],
                                    .offset = (u32)buffer.count + encoded.call_sites[site_index].code_offset,
                                    .kind = (u8)(encoded.call_sites[site_index].is_thread_local
                                                     ? target.os == OPERATING_SYSTEM_WINDOWS
                                                           ? CODEGEN_MODULE_RELOCATION_PE_TLS_OFFSET32
                                                           : (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS)
                                                                 ? CODEGEN_MODULE_RELOCATION_X86_64_MACH_TLV_PC32
                                                                 : CODEGEN_MODULE_RELOCATION_X86_64_TPOFF32
                                                     : CODEGEN_MODULE_RELOCATION_X86_64_PC32),
                                    .is_thread_local = encoded.call_sites[site_index].is_thread_local != 0,
                                };
                            }
                            buffer.count += encoded.byte_count;
                            descriptor->prolog_size = machine_prologue_cursor;
                            descriptor->code_size = (u32)buffer.count - descriptor->code_offset;
                            machine_function_emitted = true;
                            machine_stack_frame_size = placement.frame_size;
                            result.statistics.allocator_reload_count += placement.reload_count;
                            result.statistics.allocator_spill_count += placement.spill_count;
                            result.statistics.allocator_copy_count += placement.copy_count;
                            result.statistics.allocator_boundary_spill_count += placement.boundary_spill_count;
                            result.statistics.allocator_boundary_reload_count += placement.boundary_reload_count;
                            result.statistics.allocator_boundary_copy_count += placement.boundary_copy_count;
                            result.statistics.allocator_rematerialize_count += placement.rematerialize_count;
                            result.statistics.allocator_pinned_register_count += placement.pinned_register_count;
                            result.statistics.allocator_split_register_count += placement.split_register_count;
                        }
                    }
                }
            }
            scratch_end(machine_scratch);
        }
        if (machine_function_emitted)
        {
            // Canonical emission accounts for SIMD operations while lowering
            // each row. The machine path bypasses that code, so preserve the
            // same source-IR statistic once its encoded function is kept.
            result.statistics.simd_operation_count += machine_simd_operation_count;
            // Machine placement is the only frame information available on
            // this path; it is the actual frame size and preserves the
            // diagnostic statistics without rebuilding canonical value slots.
            result.statistics.stack_value_bytes += machine_stack_frame_size;
            result.statistics.stack_frame_bytes += machine_stack_frame_size;
            result.statistics.maximum_stack_frame_bytes = BUSTER_MAX(result.statistics.maximum_stack_frame_bytes, machine_stack_frame_size);
            continue;
        }
        result.statistics.fallback_function_count += options.register_allocator != CODEGEN_REGISTER_ALLOCATOR_NONE;
        goto canonical_prep;

    canonical_emit:
        // A machine attempt that bailed after describing part of its prologue
        // leaves those actions behind; the canonical prologue below describes
        // the frame it actually emits, so the fallback starts from an empty
        // description rather than appending to a foreign one.
        descriptor->unwind_action_count = 0;
        descriptor->epilog_count = 0;
        if (target.cpu_arch == CPU_ARCH_X86_64)
        {
            BusterX86MetadataPhysicalOperand push_rbp = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RBP, 64);
            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("PUSH"), &push_rbp, 1);
            bool unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity,
                                                             (u32)buffer.count - descriptor->code_offset,
                                                             CODEGEN_UNWIND_ACTION_PUSH_REGISTER, X64_REGISTER_RBP, 0);
            if (windows_dynamic_stack)
            {
                codegen_canonical_x64_adjust_stack_described(&buffer, frame_size, true, descriptor, unwind_action_capacity,
                                                             target_uses_pe_unwind(target));
                BusterX86MetadataPhysicalOperand frame_pointer_operands[2] = {
                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RBP, 64),
                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSP, 64),
                };
                (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), frame_pointer_operands,
                                                           BUSTER_ARRAY_LENGTH(frame_pointer_operands));
                unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity,
                                                            (u32)buffer.count - descriptor->code_offset,
                                                            CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, X64_REGISTER_RBP, 0) &&
                               unwind_valid;
            }
            else
            {
                BusterX86MetadataPhysicalOperand frame_pointer_operands[2] = {
                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RBP, 64),
                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSP, 64),
                };
                (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), frame_pointer_operands,
                                                           BUSTER_ARRAY_LENGTH(frame_pointer_operands));
                unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity,
                                                            (u32)buffer.count - descriptor->code_offset,
                                                            CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, X64_REGISTER_RBP, 0) &&
                               unwind_valid;
                codegen_canonical_x64_adjust_stack_described(&buffer, frame_size, true, descriptor, unwind_action_capacity,
                                                             target_uses_pe_unwind(target));
            }
            if (x64_save_rbx)
            {
                BusterX86MetadataPhysicalOperand save_rbx_operands[2] = {
                    codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64,
                                                           codegen_canonical_x64_rebase_frame_displacement(&buffer, -(s64)x64_rbx_save_offset,
                                                                                                            canonical_x64_frame_base_offset)),
                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RBX, 64),
                };
                (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), save_rbx_operands,
                                                           BUSTER_ARRAY_LENGTH(save_rbx_operands));
                unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, (u32)buffer.count - descriptor->code_offset,
                                                            CODEGEN_UNWIND_ACTION_SAVE_REGISTER, X64_REGISTER_RBX,
                                                            frame_size - x64_rbx_save_offset) &&
                               unwind_valid;
            }
            descriptor->prolog_size = (u32)buffer.count - descriptor->code_offset;
            if (!unwind_valid || buffer.error != CODEGEN_ERROR_NONE)
            {
                result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_CAPACITY;
                return result;
            }
            if (canonical_va_save_displacement)
            {
                static u8 const gp_registers[] = {
                    7, 6, 2, 1, 8, 9,
                };
                for (u32 register_index = 0; register_index < BUSTER_ARRAY_LENGTH(gp_registers); register_index += 1)
                {
                    u8 reg = gp_registers[register_index];
                    BusterX86MetadataPhysicalOperand save_gp_operands[2] = {
                        codegen_canonical_x64_metadata_memory(
                            X64_REGISTER_RBP, 64,
                            codegen_canonical_x64_rebase_frame_displacement(&buffer,
                                                                              (s64)canonical_va_save_displacement + (s64)(register_index * 8),
                                                                              canonical_x64_frame_base_offset)),
                        codegen_canonical_x64_metadata_gpr((X64Register)reg, 64),
                    };
                    (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), save_gp_operands,
                                                               BUSTER_ARRAY_LENGTH(save_gp_operands));
                }
                for (u32 register_index = 0; register_index < 8; register_index += 1)
                {
                    BusterX86MetadataPhysicalOperand save_xmm_operands[2] = {
                        codegen_canonical_x64_metadata_memory(
                            X64_REGISTER_RBP, 128,
                            codegen_canonical_x64_rebase_frame_displacement(&buffer,
                                                                              (s64)canonical_va_save_displacement + 48 + (s64)(register_index * 16),
                                                                              canonical_x64_frame_base_offset)),
                        codegen_canonical_x64_metadata_vector(register_index, 128),
                    };
                    String8 feature_names[] = {S8("sse2")};
                    (void)codegen_canonical_x64_metadata_emit_features(
                        &buffer, S8("MOVDQU"), save_xmm_operands, BUSTER_ARRAY_LENGTH(save_xmm_operands),
                        (BusterX86MetadataFeatureInput){.names = feature_names, .count = BUSTER_ARRAY_LENGTH(feature_names)});
                }
            }
            else if (canonical_variadic && result.abi == CODEGEN_ABI_X86_64_WINDOWS)
            {
                static u8 const gp_registers[] = {
                    1,
                    2,
                    8,
                    9,
                };
                for (u32 register_index = 0; register_index < BUSTER_ARRAY_LENGTH(gp_registers); register_index += 1)
                {
                    u8 reg = gp_registers[register_index];
                    BusterX86MetadataPhysicalOperand save_gp_operands[2] = {
                        codegen_canonical_x64_metadata_memory(
                            X64_REGISTER_RBP, 64,
                            codegen_canonical_x64_rebase_frame_displacement(&buffer, 16 + (s64)register_index * 8,
                                                                              canonical_x64_frame_base_offset)),
                        codegen_canonical_x64_metadata_gpr((X64Register)reg, 64),
                    };
                    (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), save_gp_operands,
                                                               BUSTER_ARRAY_LENGTH(save_gp_operands));
                }
            }
            if (x64_indirect_return)
            {
                BusterX86MetadataPhysicalOperand indirect_result_operands[2] = {
                    codegen_canonical_x64_metadata_memory(
                        X64_REGISTER_RBP,
                        64,
                        codegen_canonical_x64_rebase_frame_displacement(&buffer, hidden_result_displacement,
                                                                          canonical_x64_frame_base_offset)),
                    codegen_canonical_x64_metadata_gpr(windows_indirect_return ? X64_REGISTER_RCX : X64_REGISTER_RDI, 64),
                };
                (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), indirect_result_operands,
                                                           BUSTER_ARRAY_LENGTH(indirect_result_operands));
            }
        }
        else
        {
            codegen_emit_u32(&buffer, 0xa9bf7bfd);
            bool unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity,
                                                             (u32)buffer.count - descriptor->code_offset,
                                                             CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, 16);
            unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, (u32)buffer.count - descriptor->code_offset,
                                                        CODEGEN_UNWIND_ACTION_SAVE_REGISTER, 29, 0) &&
                           unwind_valid;
            unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, (u32)buffer.count - descriptor->code_offset,
                                                        CODEGEN_UNWIND_ACTION_SAVE_REGISTER, 30, 8) &&
                           unwind_valid;
            codegen_emit_u32(&buffer, 0x910003fd);
            unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, (u32)buffer.count - descriptor->code_offset,
                                                        CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, 29, 0) &&
                           unwind_valid;
            if (frame_size)
            {
                codegen_canonical_a64_adjust_stack_described(&buffer, frame_size, true, descriptor, unwind_action_capacity, windows_aarch64);
            }
            if (!codegen_canonical_a64_memory_operation(&buffer, 28, aarch64_frame_base_save_offset, 8, true, false))
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
            unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, (u32)buffer.count - descriptor->code_offset,
                                                        CODEGEN_UNWIND_ACTION_SAVE_REGISTER, 28, aarch64_frame_base_save_offset) &&
                           unwind_valid;
            codegen_emit_u32(&buffer, 0x910003fc);
            if (windows_aarch64)
            {
                unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, (u32)buffer.count - descriptor->code_offset,
                                                            CODEGEN_UNWIND_ACTION_NOP, 0, 0) &&
                               unwind_valid;
            }
            descriptor->prolog_size = (u32)buffer.count - descriptor->code_offset;
            if (!unwind_valid || buffer.error != CODEGEN_ERROR_NONE)
            {
                result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_CAPACITY;
                return result;
            }
            if (aarch64_indirect_return)
            {
                if (!codegen_canonical_a64_frame_memory_operation(&buffer, 8, aarch64_hidden_result_offset, 8, true, false))
                {
                    result.error = CODEGEN_ERROR_CAPACITY;
                    return result;
                }
            }
            if (canonical_variadic && !aarch64_darwin_variadic)
            {
                for (u32 register_index = 0; register_index < 8; register_index += 1)
                {
                    if (!codegen_canonical_a64_frame_memory_operation(&buffer, register_index, aarch64_va_save_offset + register_index * 8, 8, true, false))
                    {
                        result.error = CODEGEN_ERROR_CAPACITY;
                        return result;
                    }
                }
            }
        }
        if (function->instruction_count > UINT32_MAX / 2)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
        u32 branch_patch_capacity = function->instruction_count * 2;
        u32* block_offsets = arena_allocate(arena, u32, function->block_count);
        CCanonicalBranchPatch* branch_patches = arena_allocate(arena, CCanonicalBranchPatch, branch_patch_capacity);
        CCanonicalEmitter emitter = {
            .buffer = &buffer,
            .value_offsets = value_offsets,
            .frame_base_offset = canonical_x64_frame_base_offset,
            .save_rbx = x64_save_rbx,
            .rbx_save_offset = x64_rbx_save_offset,
            .forwarded_store_end = UINT64_MAX,
            .branch_patches = branch_patches,
            .branch_patch_capacity = branch_patch_capacity,
        };
        bool x64_upper_vector_dirty = false;
        IrValueId x64_last_wide_vector_result = IR_VALUE_ID_INVALID;
        u32 x64_last_wide_vector_size = 0;
        // Canonical f80 support uses only bounded fldt/fstpt transactions and
        // one live ST0 for a direct f80 return.  Keep the depth explicit so a
        // future path cannot silently leak an x87 stack entry across a call.
        u32 x87_stack_depth = 0;
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            IrBlock* emitted_block = function->blocks + block_index;
            block_offsets[block_index] = (u32)buffer.count;
            // Every branch in this emitter targets either a block start or an
            // offset inside its own instruction expansion, so dropping the
            // forwarded-store record here is all that is needed to keep it
            // sound across control flow.
            emitter.forwarded_store_end = UINT64_MAX;
            // Validation's ownership proof covers this walk: every chain is a
            // simple path of in-range ids ending at last_instruction. The
            // counter that used to stand in for that proof re-walked the whole
            // function before it could notice a cycle, and the emitter already
            // trusts validation for everything it indexes below.
            IrInstructionId instruction_id = emitted_block->first_instruction;
            while (instruction_id.value != IR_ID_UNDERLYING_INVALID)
            {
                IrInstruction* instruction = function->instructions + instruction_id.value;
                result.failed_instruction = instruction_id;
                result.failed_opcode = instruction->opcode;
                if (instruction->opcode == IR_OPCODE_LOCAL && function->values[instruction->result.value].alignment <= 16)
                {
                    instruction_id = instruction->next;
                    continue;
                }
                IrSourceRange canonical_source = ir_instruction_canonical_source(function, instruction_id);
                // The line table is one of the four consumers that pay for a
                // line and a column, and the only one that asks per
                // instruction. Consecutive instructions overwhelmingly carry
                // the same range — every instruction of one expression comes
                // from one token — so the repeat is rejected on the offset the
                // range already holds, and a position is recovered only for an
                // offset that can still produce a row.
                if (result.line_entries && canonical_source.source.value != IR_ID_UNDERLYING_INVALID &&
                    (canonical_source.offset != recorded_source.offset || canonical_source.source.value != recorded_source.source.value))
                {
                    recorded_source = canonical_source;
                    IrSourcePosition position = ir_source_position(program, canonical_source);
                    codegen_record_line_hot(result.line_entries, &result.line_entry_count, line_entry_capacity, (u32)buffer.count,
                                            canonical_source.source.value, position.line, position.column);
                }
                if (x64_upper_vector_dirty && !codegen_canonical_x64_instruction_preserves_wide_vector(program, instruction) &&
                    !codegen_canonical_x64_instruction_uses_wide_vector(program, function, instruction, target))
                {
                    String8 vzeroupper_features[] = {S8("avx")};
                    if (!codegen_canonical_x64_metadata_emit_features(
                            &buffer, S8("VZEROUPPER"), 0, 0,
                            (BusterX86MetadataFeatureInput){.names = vzeroupper_features,
                                                             .count = BUSTER_ARRAY_LENGTH(vzeroupper_features)}))
                    {
                        result.error = buffer.error;
                        return result;
                    }
                    x64_upper_vector_dirty = false;
                    x64_last_wide_vector_result = IR_VALUE_ID_INVALID;
                    x64_last_wide_vector_size = 0;
                    result.statistics.vzeroupper_count += 1;
                }
                if (target.cpu_arch == CPU_ARCH_X86_64)
                {
                        s32 result_displacement = instruction->result.value == IR_ID_UNDERLYING_INVALID
                                                  ? 0
                                                  : codegen_canonical_x64_rebase_frame_displacement(&buffer,
                                                                                                     -(s64)value_offsets[instruction->result.value],
                                                                                                     canonical_x64_frame_base_offset);
                    if (instruction->opcode == IR_OPCODE_LOCAL)
                    {
                        IrValue* local = function->values + instruction->result.value;
                        u32 local_alignment = local->alignment;
                        if (!local_alignment || local_alignment > INT32_MAX)
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        BusterX86MetadataPhysicalOperand local_address_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            codegen_canonical_x64_metadata_memory(
                                X64_REGISTER_RBP, 64, c_x64_frame_displacement(&emitter, aligned_local_offsets[instruction->result.value])),
                        };
                        BusterX86MetadataPhysicalOperand local_add_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            codegen_canonical_x64_metadata_immediate((s64)local_alignment - 1, 32),
                        };
                        BusterX86MetadataPhysicalOperand local_align_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            codegen_canonical_x64_metadata_immediate(-(s64)local_alignment, 32),
                        };
                        (void)codegen_canonical_x64_metadata_emit(&buffer, S8("LEA"), local_address_operands,
                                                                  BUSTER_ARRAY_LENGTH(local_address_operands));
                        (void)codegen_canonical_x64_metadata_emit(&buffer, S8("ADD"), local_add_operands,
                                                                  BUSTER_ARRAY_LENGTH(local_add_operands));
                        (void)codegen_canonical_x64_metadata_emit(&buffer, S8("AND"), local_align_operands,
                                                                  BUSTER_ARRAY_LENGTH(local_align_operands));
                        c_x64_store_result(&emitter, result_displacement);
                    }
                    else if (instruction->opcode == IR_OPCODE_STACK_ALLOCATE)
                    {
                        u32 stack_alignment = (u32)instruction->immediates[0];
                        stack_alignment = BUSTER_MAX(stack_alignment, 16);
                        c_x64_load(&emitter, 0x85, instruction->operands[0]);
                        if (stack_alignment > INT32_MAX)
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        BusterX86MetadataPhysicalOperand stack_add_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            codegen_canonical_x64_metadata_immediate((s64)stack_alignment - 1, 32),
                        };
                        BusterX86MetadataPhysicalOperand stack_align_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            codegen_canonical_x64_metadata_immediate(-(s64)stack_alignment, 32),
                        };
                        BusterX86MetadataPhysicalOperand stack_compare_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            codegen_canonical_x64_metadata_immediate(CODEGEN_X64_STACK_PROBE_PAGE, 32),
                        };
                        BusterX86MetadataPhysicalOperand stack_sub_rsp_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSP, 64),
                            codegen_canonical_x64_metadata_immediate(CODEGEN_X64_STACK_PROBE_PAGE, 32),
                        };
                        BusterX86MetadataPhysicalOperand stack_test_operands[2] = {
                            codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RSP, 8, 0),
                            codegen_canonical_x64_metadata_immediate(0, 8),
                        };
                        BusterX86MetadataPhysicalOperand stack_sub_rax_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            codegen_canonical_x64_metadata_immediate(CODEGEN_X64_STACK_PROBE_PAGE, 32),
                        };
                        BusterX86MetadataPhysicalOperand stack_sub_rsp_rax_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSP, 64),
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                        };
                        BusterX86MetadataPhysicalOperand stack_move_rax_rsp_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSP, 64),
                        };
                        (void)codegen_canonical_x64_metadata_emit(&buffer, S8("ADD"), stack_add_operands,
                                                                  BUSTER_ARRAY_LENGTH(stack_add_operands));
                        (void)codegen_canonical_x64_metadata_emit(&buffer, S8("AND"), stack_align_operands,
                                                                  BUSTER_ARRAY_LENGTH(stack_align_operands));
                        u64 stack_probe_compare_offset = buffer.count;
                        (void)codegen_canonical_x64_metadata_emit(&buffer, S8("CMP"), stack_compare_operands,
                                                                  BUSTER_ARRAY_LENGTH(stack_compare_operands));
                        u64 stack_probe_final_patch = buffer.count;
                        BusterX86MetadataPhysicalOperand stack_probe_final_branch = codegen_canonical_x64_metadata_relative(0, 8);
                        (void)codegen_canonical_x64_metadata_emit(&buffer, S8("JB"), &stack_probe_final_branch, 1);
                        (void)codegen_canonical_x64_metadata_emit(&buffer, S8("SUB"), stack_sub_rsp_operands,
                                                                  BUSTER_ARRAY_LENGTH(stack_sub_rsp_operands));
                        (void)codegen_canonical_x64_metadata_emit(&buffer, S8("TEST"), stack_test_operands,
                                                                  BUSTER_ARRAY_LENGTH(stack_test_operands));
                        (void)codegen_canonical_x64_metadata_emit(&buffer, S8("SUB"), stack_sub_rax_operands,
                                                                  BUSTER_ARRAY_LENGTH(stack_sub_rax_operands));
                        u64 stack_probe_loop_patch = buffer.count;
                        BusterX86MetadataPhysicalOperand stack_probe_loop_branch = codegen_canonical_x64_metadata_relative(0, 8);
                        (void)codegen_canonical_x64_metadata_emit(&buffer, S8("JMP"), &stack_probe_loop_branch, 1);
                        u64 stack_probe_final_offset = buffer.count;
                        (void)codegen_canonical_x64_metadata_emit(&buffer, S8("SUB"), stack_sub_rsp_rax_operands,
                                                                  BUSTER_ARRAY_LENGTH(stack_sub_rsp_rax_operands));
                        (void)codegen_canonical_x64_metadata_emit(&buffer, S8("TEST"), stack_test_operands,
                                                                  BUSTER_ARRAY_LENGTH(stack_test_operands));
                        (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), stack_move_rax_rsp_operands,
                                                                  BUSTER_ARRAY_LENGTH(stack_move_rax_rsp_operands));
                        s64 stack_probe_final_delta = (s64)stack_probe_final_offset - (s64)(stack_probe_final_patch + 2);
                        // Recheck the remaining count before every page touch.
                        // Branching to the first SUB skips CMP after the first
                        // page, so any allocation larger than one page keeps
                        // subtracting through the stack guard until SIGSEGV.
                        s64 stack_probe_loop_delta = (s64)stack_probe_compare_offset - (s64)(stack_probe_loop_patch + 2);
                        if (buffer.error == CODEGEN_ERROR_NONE && stack_probe_final_delta >= INT8_MIN && stack_probe_final_delta <= INT8_MAX &&
                            stack_probe_loop_delta >= INT8_MIN && stack_probe_loop_delta <= INT8_MAX && stack_probe_final_patch + 1 < buffer.count &&
                            stack_probe_loop_patch + 1 < buffer.count)
                        {
                            buffer.bytes[stack_probe_final_patch + 1] = (u8)(s8)stack_probe_final_delta;
                            buffer.bytes[stack_probe_loop_patch + 1] = (u8)(s8)stack_probe_loop_delta;
                        }
                        else if (buffer.error == CODEGEN_ERROR_NONE)
                        {
                            buffer.error = CODEGEN_ERROR_CAPACITY;
                        }
                        c_x64_store_result(&emitter, result_displacement);
                    }
                    else if (instruction->opcode == IR_OPCODE_STACK_SAVE)
                    {
                        BusterX86MetadataPhysicalOperand stack_save_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSP, 64),
                        };
                        (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), stack_save_operands,
                                                                  BUSTER_ARRAY_LENGTH(stack_save_operands));
                        c_x64_store_result(&emitter, result_displacement);
                    }
                    else if (instruction->opcode == IR_OPCODE_STACK_RESTORE)
                    {
                        c_x64_load(&emitter, 0x85, instruction->operands[0]);
                        BusterX86MetadataPhysicalOperand stack_restore_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSP, 64),
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                        };
                        (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), stack_restore_operands,
                                                                  BUSTER_ARRAY_LENGTH(stack_restore_operands));
                    }
                    else if (instruction->opcode == IR_OPCODE_ARGUMENT)
                    {
                        static u8 const system_v[] = {
                            7, 6, 2, 1, 8, 9,
                        };
                        static u8 const windows[] = {
                            1,
                            2,
                            8,
                            9,
                        };
                        u32 argument_index = (u32)instruction->immediates[0];
                        IrType* function_type = ir_type_from_id(&program->types, function->canonical_type);
                        if (!function_type || function_type->kind != IR_TYPE_FUNCTION || argument_index >= function_type->parameter_count)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        u8 const* registers = result.abi == CODEGEN_ABI_X86_64_WINDOWS ? windows : system_v;
                        u32 register_count = result.abi == CODEGEN_ABI_X86_64_WINDOWS ? BUSTER_ARRAY_LENGTH(windows) : BUSTER_ARRAY_LENGTH(system_v);
                        u32 register_index = x64_indirect_return ? 1 : 0;
                        u32 float_register_index = 0;
                        // Bytes, not eightbytes: a parameter the caller had to
                        // align sits past a gap, and reading it back means
                        // walking the incoming area by the same rule the
                        // outgoing one was filled by.
                        u64 prior_stack_bytes = 0;
                        for (u32 prior_index = 0; prior_index < argument_index; prior_index += 1)
                        {
                            u32 prior_parts = 1;
                            bool prior_aggregate =
                                codegen_canonical_integer_aggregate_parts(program, function_type->parameter_types[prior_index], &prior_parts);
                            IrType* prior_type = ir_type_from_id(&program->types, function_type->parameter_types[prior_index]);
                            if (codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, function_type->parameter_types[prior_index]))
                            {
                                if (result.abi != CODEGEN_ABI_X86_64_SYSTEM_V || !prior_type || prior_type->layout.size != 16 ||
                                    !codegen_canonical_x64_type_is_f80_x87_shape_cached(f80_cache, program, function_type->parameter_types[prior_index]))
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                prior_stack_bytes = codegen_canonical_x64_stack_argument_offset(prior_stack_bytes, 16) + 16;
                                continue;
                            }
                            CodegenCanonicalAbiValue prior_aggregate_abi =
                                codegen_canonical_aggregate_abi(program, function_type->parameter_types[prior_index], result.abi, false, false);
                            bool prior_in_registers = codegen_canonical_x64_abi_value_in_registers(&prior_aggregate_abi, &target);
                            if (prior_aggregate_abi.part_count && !prior_aggregate_abi.memory && !prior_aggregate_abi.indirect)
                            {
                                prior_aggregate = true;
                                if (prior_in_registers)
                                {
                                    prior_parts = prior_aggregate_abi.part_count;
                                }
                            }
                            if (result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && prior_aggregate_abi.part_count && !prior_aggregate_abi.memory && prior_in_registers)
                            {
                                u32 integer_count = 0;
                                u32 float_count = 0;
                                for (u32 part = 0; part < prior_aggregate_abi.part_count; part += 1)
                                {
                                    if (codegen_canonical_abi_part_is_float(prior_aggregate_abi.parts[part].abi_class))
                                    {
                                        float_count += 1;
                                    }
                                    else
                                    {
                                        integer_count += 1;
                                    }
                                }
                                if (register_index + integer_count <= register_count && float_register_index + float_count <= 8)
                                {
                                    register_index += integer_count;
                                    float_register_index += float_count;
                                }
                                else
                                {
                                    prior_stack_bytes =
                                        codegen_canonical_x64_stack_argument_offset(prior_stack_bytes,
                                                                                    codegen_canonical_x64_stack_argument_alignment(prior_type)) +
                                        ((prior_type->layout.size + 7) & ~(u64)7);
                                }
                                continue;
                            }
                            if (result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && prior_type && prior_type->kind == IR_TYPE_FLOAT)
                            {
                                if (float_register_index < 8)
                                {
                                    float_register_index += 1;
                                }
                                else
                                {
                                    prior_stack_bytes =
                                        codegen_canonical_x64_stack_argument_offset(prior_stack_bytes,
                                                                                    codegen_canonical_x64_stack_argument_alignment(prior_type)) +
                                        8;
                                }
                                continue;
                            }
                            bool prior_memory = prior_aggregate && prior_type && prior_type->layout.size > 16 &&
                                                result.abi == CODEGEN_ABI_X86_64_SYSTEM_V;
                            if (prior_aggregate && result.abi == CODEGEN_ABI_X86_64_WINDOWS)
                            {
                                prior_parts = 1;
                            }
                            if (!prior_memory && register_index + prior_parts <= register_count)
                            {
                                register_index += prior_parts;
                            }
                            else
                            {
                                u32 prior_alignment =
                                    result.abi == CODEGEN_ABI_X86_64_SYSTEM_V ? codegen_canonical_x64_stack_argument_alignment(prior_type) : 8;
                                prior_stack_bytes = codegen_canonical_x64_stack_argument_offset(prior_stack_bytes, prior_alignment) + (u64)prior_parts * 8;
                            }
                        }
                        u32 part_count = 1;
                        bool aggregate = codegen_canonical_integer_aggregate_parts(program, instruction->canonical_type, &part_count);
                        CodegenCanonicalAbiValue argument_aggregate_abi =
                            codegen_canonical_aggregate_abi(program, instruction->canonical_type, result.abi, false, false);
                        // How many eightbytes the argument occupies if it came
                        // in on the stack, which is its size and not the number
                        // of registers it would have taken: one zmm holds a
                        // 64-byte vector, but its stack image is still eight.
                        IrType* argument_stack_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        u32 stack_part_count =
                            argument_stack_type && argument_stack_type->layout.resolved ? (u32)((argument_stack_type->layout.size + 7) / 8) : part_count;
                        bool argument_in_registers = codegen_canonical_x64_abi_value_in_registers(&argument_aggregate_abi, &target);
                        if (argument_aggregate_abi.part_count && !argument_aggregate_abi.memory && !argument_aggregate_abi.indirect)
                        {
                            aggregate = true;
                            if (argument_in_registers)
                            {
                                part_count = argument_aggregate_abi.part_count;
                            }
                        }
                        bool windows_indirect = result.abi == CODEGEN_ABI_X86_64_WINDOWS && argument_aggregate_abi.indirect;
                        IrType* argument_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        bool argument_contains_f80 = codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, instruction->canonical_type);
                        if (argument_contains_f80)
                        {
                            if (result.abi != CODEGEN_ABI_X86_64_SYSTEM_V || !argument_type || argument_type->layout.size != 16 ||
                                !codegen_canonical_x64_type_is_f80_x87_shape_cached(f80_cache, program, instruction->canonical_type) ||
                                !argument_aggregate_abi.memory)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                            u64 argument_stack_offset = codegen_canonical_x64_stack_argument_offset(prior_stack_bytes, 16);
                            s64 source_displacement = (s64)16 + (s64)argument_stack_offset;
                            if (source_displacement > INT32_MAX ||
                                !codegen_canonical_x64_emit_f80_copy(&buffer, X64_REGISTER_RBP,
                                                                      codegen_canonical_x64_rebase_frame_displacement(
                                                                          &buffer, source_displacement, canonical_x64_frame_base_offset),
                                                                      X64_REGISTER_RBP, result_displacement, &x87_stack_depth))
                            {
                                result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && argument_type && argument_type->kind == IR_TYPE_FLOAT)
                        {
                            if (argument_type->bit_width != 32 && argument_type->bit_width != 64)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                            if (float_register_index < 8)
                            {
                                BusterX86MetadataPhysicalOperand float_store_operands[2] = {
                                    codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, (u16)argument_type->bit_width,
                                                                          result_displacement),
                                    codegen_canonical_x64_metadata_vector(float_register_index, (u16)argument_type->bit_width),
                                };
                                String8 float_features[] = {S8("sse"), S8("sse2")};
                                if (!codegen_canonical_x64_metadata_emit_features(
                                        &buffer, argument_type->bit_width == 32 ? S8("MOVSS") : S8("MOVSD"), float_store_operands,
                                        BUSTER_ARRAY_LENGTH(float_store_operands),
                                        (BusterX86MetadataFeatureInput){.names = float_features,
                                                                         .count = BUSTER_ARRAY_LENGTH(float_features)}))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            else
                            {
                                BusterX86MetadataPhysicalOperand stack_argument_load_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_memory(
                                        X64_REGISTER_RBP, 64,
                                        codegen_canonical_x64_rebase_frame_displacement(
                                            &buffer, (s64)16 + (s64)codegen_canonical_x64_stack_argument_offset(prior_stack_bytes, 8),
                                            canonical_x64_frame_base_offset)),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), stack_argument_load_operands,
                                                                           BUSTER_ARRAY_LENGTH(stack_argument_load_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                c_x64_store_result(&emitter, result_displacement);
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (result.abi == CODEGEN_ABI_X86_64_WINDOWS && argument_type && argument_type->kind == IR_TYPE_FLOAT &&
                            register_index < register_count)
                        {
                            if (argument_type->bit_width != 32 && argument_type->bit_width != 64)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                            BusterX86MetadataPhysicalOperand float_store_operands[2] = {
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, (u16)argument_type->bit_width,
                                                                      result_displacement),
                                codegen_canonical_x64_metadata_vector(register_index, (u16)argument_type->bit_width),
                            };
                            String8 float_features[] = {S8("sse"), S8("sse2")};
                            if (!codegen_canonical_x64_metadata_emit_features(
                                    &buffer, argument_type->bit_width == 32 ? S8("MOVSS") : S8("MOVSD"), float_store_operands,
                                    BUSTER_ARRAY_LENGTH(float_store_operands),
                                    (BusterX86MetadataFeatureInput){.names = float_features,
                                                                     .count = BUSTER_ARRAY_LENGTH(float_features)}))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        u32 system_v_integer_parts = 0;
                        u32 system_v_float_parts = 0;
                        bool system_v_register_aggregate =
                            result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && argument_aggregate_abi.part_count && !argument_aggregate_abi.memory && argument_in_registers;
                        // "Aggregates over two eightbytes are MEMORY" is a rule
                        // about aggregates; a 32- or 64-byte vector arrives in
                        // a vector register on a target that has one that wide.
                        // The IR ABI and the target between them have already
                        // said which of the two this is, so the size heuristic
                        // only speaks when they did not.
                        bool system_v_memory = aggregate && argument_type && argument_type->layout.size > 16 &&
                                               result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && !system_v_register_aggregate;
                        if (system_v_register_aggregate)
                        {
                            for (u32 part = 0; part < argument_aggregate_abi.part_count; part += 1)
                            {
                                if (codegen_canonical_abi_part_is_float(argument_aggregate_abi.parts[part].abi_class))
                                {
                                    system_v_float_parts += 1;
                                }
                                else
                                {
                                    system_v_integer_parts += 1;
                                }
                            }
                        }
                        u32 register_parts = windows_indirect ? 1 : part_count;
                        if (!aggregate)
                        {
                            IrType* parameter_type = ir_type_from_id(&program->types, instruction->canonical_type);
                            if (parameter_type && (parameter_type->kind == IR_TYPE_STRUCT || parameter_type->kind == IR_TYPE_UNION))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                        }
                        if (system_v_memory || (system_v_register_aggregate ? register_index + system_v_integer_parts > register_count ||
                                                                                  float_register_index + system_v_float_parts > 8
                                                                            : register_index + register_parts > register_count))
                        {
                            u32 first_stack_offset = result.abi == CODEGEN_ABI_X86_64_WINDOWS ? 48 : 16;
                            // The caller placed this one at its own alignment,
                            // so skip the padding it left behind.
                            u64 argument_stack_offset = codegen_canonical_x64_stack_argument_offset(
                                prior_stack_bytes,
                                result.abi == CODEGEN_ABI_X86_64_SYSTEM_V ? codegen_canonical_x64_stack_argument_alignment(argument_stack_type) : 8);
                            if (windows_indirect)
                            {
                                BusterX86MetadataPhysicalOperand indirect_stack_load_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_memory(
                                        X64_REGISTER_RBP, 64,
                                        codegen_canonical_x64_rebase_frame_displacement(
                                            &buffer, first_stack_offset + (s64)argument_stack_offset, canonical_x64_frame_base_offset)),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), indirect_stack_load_operands,
                                                                           BUSTER_ARRAY_LENGTH(indirect_stack_load_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                for (u32 part_index = 0; part_index < part_count; part_index += 1)
                                {
                                    u32 part_offset = part_index * 8;
                                    BusterX86MetadataPhysicalOperand indirect_part_load_operands[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                        codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RAX, 64, part_offset),
                                    };
                                    BusterX86MetadataPhysicalOperand indirect_part_store_operands[2] = {
                                        codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64,
                                                                              result_displacement + (s32)(part_index * 8)),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                    };
                                    if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), indirect_part_load_operands,
                                                                               BUSTER_ARRAY_LENGTH(indirect_part_load_operands)) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), indirect_part_store_operands,
                                                                               BUSTER_ARRAY_LENGTH(indirect_part_store_operands)))
                                    {
                                        result.error = buffer.error;
                                        return result;
                                    }
                                }
                                instruction_id = instruction->next;
                                continue;
                            }
                            for (u32 part_index = 0; part_index < stack_part_count; part_index += 1)
                            {
                                BusterX86MetadataPhysicalOperand stack_load_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_memory(
                                        X64_REGISTER_RBP, 64,
                                        codegen_canonical_x64_rebase_frame_displacement(
                                            &buffer, first_stack_offset + (s64)argument_stack_offset + (s64)part_index * 8,
                                            canonical_x64_frame_base_offset)),
                                };
                                BusterX86MetadataPhysicalOperand stack_store_operands[2] = {
                                    codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64,
                                                                          result_displacement + (s32)(part_index * 8)),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), stack_load_operands,
                                                                           BUSTER_ARRAY_LENGTH(stack_load_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), stack_store_operands,
                                                                           BUSTER_ARRAY_LENGTH(stack_store_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (windows_indirect)
                        {
                            u8 source_reg = registers[register_index];
                            for (u32 part_index = 0; part_index < part_count; part_index += 1)
                            {
                                u32 part_offset = part_index * 8;
                                BusterX86MetadataPhysicalOperand indirect_register_load_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_memory_relaxed((X64Register)source_reg, 64, part_offset),
                                };
                                BusterX86MetadataPhysicalOperand indirect_register_store_operands[2] = {
                                    codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64,
                                                                          result_displacement + (s32)(part_index * 8)),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), indirect_register_load_operands,
                                                                           BUSTER_ARRAY_LENGTH(indirect_register_load_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), indirect_register_store_operands,
                                                                           BUSTER_ARRAY_LENGTH(indirect_register_store_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        for (u32 part_index = 0; part_index < part_count; part_index += 1)
                        {
                            if (system_v_register_aggregate && codegen_canonical_abi_part_is_float(argument_aggregate_abi.parts[part_index].abi_class))
                            {
                                u32 part_offset = argument_aggregate_abi.parts[part_index].value_offset;
                                u32 part_size = argument_aggregate_abi.parts[part_index].size;
                                if (!codegen_canonical_x64_float_memory(&buffer, target, float_register_index, result_displacement + (s32)part_offset, part_size, true))
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                float_register_index += 1;
                                continue;
                            }
                            u8 reg = registers[register_index++];
                            BusterX86MetadataPhysicalOperand register_store_operands[2] = {
                                codegen_canonical_x64_metadata_memory(
                                    X64_REGISTER_RBP, 64,
                                    result_displacement + (s32)(system_v_register_aggregate ? argument_aggregate_abi.parts[part_index].value_offset
                                                                                             : part_index * 8)),
                                codegen_canonical_x64_metadata_gpr((X64Register)reg, 64),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), register_store_operands,
                                                                       BUSTER_ARRAY_LENGTH(register_store_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_GLOBAL || instruction->opcode == IR_OPCODE_FUNCTION)
                    {
                        if (instruction->opcode == IR_OPCODE_FUNCTION && direct_call_uses[instruction->result.value] == 1)
                        {
                            BusterX86MetadataPhysicalOperand zero_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), zero_operands,
                                                                       BUSTER_ARRAY_LENGTH(zero_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            c_x64_store_result(&emitter, result_displacement);
                            instruction_id = instruction->next;
                            continue;
                        }
                        IrSymbol* symbol = ir_symbol_from_id(&program->symbols, instruction->symbol);
                        bool is_thread_local = instruction->opcode == IR_OPCODE_GLOBAL && symbol && symbol->is_thread_local;
                        if (is_thread_local)
                        {
                            if (target.os == OPERATING_SYSTEM_WINDOWS)
                            {
                                BusterX86MetadataPhysicalOperand index_load_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                                    codegen_canonical_x64_metadata_rip_relative(32, 0),
                                };
                                u32 index_relocation_offset = 0;
                                if (!codegen_canonical_x64_metadata_emit_relocation(&buffer, S8("MOV"), index_load_operands,
                                                                                     BUSTER_ARRAY_LENGTH(index_load_operands),
                                                                                     &index_relocation_offset))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                BusterX86MetadataPhysicalOperand tls_index_memory =
                                    codegen_canonical_x64_metadata_segment_memory(BUSTER_X86_METADATA_SEGMENT_GS, 64, 0x58);
                                BusterX86MetadataPhysicalOperand tls_index_load_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                    tls_index_memory,
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), tls_index_load_operands,
                                                                           BUSTER_ARRAY_LENGTH(tls_index_load_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                BusterX86MetadataPhysicalOperand tls_slot_memory =
                                    codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RDX, 64, 0);
                                tls_slot_memory.memory.has_index = true;
                                tls_slot_memory.memory.index = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64).reg;
                                tls_slot_memory.memory.scale = 8;
                                BusterX86MetadataPhysicalOperand tls_slot_load_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    tls_slot_memory,
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), tls_slot_load_operands,
                                                                           BUSTER_ARRAY_LENGTH(tls_slot_load_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                BusterX86MetadataPhysicalOperand tls_value_address_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_memory(X64_REGISTER_RAX, 64, 0),
                                };
                                u32 value_relocation_offset = 0;
                                if (!codegen_canonical_x64_metadata_emit_relocation(&buffer, S8("LEA"), tls_value_address_operands,
                                                                                     BUSTER_ARRAY_LENGTH(tls_value_address_operands),
                                                                                     &value_relocation_offset))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = index_relocation_offset,
                                    .kind = CODEGEN_MODULE_RELOCATION_X86_64_PE_TLS_INDEX_PC32,
                                    .is_thread_local = true,
                                    .thread_local_index = true,
                                };
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = value_relocation_offset,
                                    .kind = CODEGEN_MODULE_RELOCATION_PE_TLS_OFFSET32,
                                    .is_thread_local = true,
                                };
                            }
                            else if (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS)
                            {
                                BusterX86MetadataPhysicalOperand descriptor_load_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDI, 64),
                                    codegen_canonical_x64_metadata_rip_relative(64, 0),
                                };
                                u32 descriptor_relocation_offset = 0;
                                if (!codegen_canonical_x64_metadata_emit_relocation(&buffer, S8("MOV"), descriptor_load_operands,
                                                                                     BUSTER_ARRAY_LENGTH(descriptor_load_operands),
                                                                                     &descriptor_relocation_offset))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                BusterX86MetadataPhysicalOperand descriptor_call_operand =
                                    codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RDI, 64, 0);
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("CALL"), &descriptor_call_operand, 1))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = descriptor_relocation_offset,
                                    .kind = CODEGEN_MODULE_RELOCATION_X86_64_MACH_TLV_PC32,
                                    .is_thread_local = true,
                                };
                            }
                            else if (target.os != OPERATING_SYSTEM_LINUX && target.os != OPERATING_SYSTEM_ANDROID)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                            else
                            {
                                BusterX86MetadataPhysicalOperand thread_pointer_load_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_segment_memory(BUSTER_X86_METADATA_SEGMENT_FS, 64, 0),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), thread_pointer_load_operands,
                                                                           BUSTER_ARRAY_LENGTH(thread_pointer_load_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                BusterX86MetadataPhysicalOperand tls_value_address_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_memory(X64_REGISTER_RAX, 64, 0),
                                };
                                u32 tls_relocation_offset = 0;
                                if (!codegen_canonical_x64_metadata_emit_relocation(&buffer, S8("LEA"), tls_value_address_operands,
                                                                                     BUSTER_ARRAY_LENGTH(tls_value_address_operands),
                                                                                     &tls_relocation_offset))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = tls_relocation_offset,
                                    .kind = CODEGEN_MODULE_RELOCATION_X86_64_TPOFF32,
                                    .is_thread_local = true,
                                };
                            }
                        }
                        else
                        {
                            BusterX86MetadataPhysicalOperand address_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                codegen_canonical_x64_metadata_rip_relative(64, 0),
                            };
                            u32 address_relocation_offset = 0;
                            if (!codegen_canonical_x64_metadata_emit_relocation(&buffer, S8("LEA"), address_operands,
                                                                                 BUSTER_ARRAY_LENGTH(address_operands),
                                                                                 &address_relocation_offset))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                .symbol = instruction->symbol,
                                .offset = address_relocation_offset,
                                .kind = CODEGEN_MODULE_RELOCATION_X86_64_PC32,
                            };
                        }
                        BusterX86MetadataPhysicalOperand global_store_operands[2] = {
                            codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, result_displacement),
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                        };
                        if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), global_store_operands,
                                                                   BUSTER_ARRAY_LENGTH(global_store_operands)))
                        {
                            result.error = buffer.error;
                            return result;
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_LOAD || instruction->opcode == IR_OPCODE_ATOMIC_LOAD)
                    {
                        IrValue* place = function->values + instruction->operands[0].value;
                        IrInstruction* definition = function->instructions + place->definition.value;
                        u32 aggregate_parts = 0;
                        bool aggregate = codegen_canonical_integer_aggregate_parts(program, instruction->canonical_type, &aggregate_parts);
                        IrType* aggregate_type = aggregate ? ir_type_from_id(&program->types, instruction->canonical_type) : 0;
                        bool indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                        definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE ||
                                        (definition->opcode == IR_OPCODE_LOCAL && place->alignment > 16);
                        IrType* loaded_value_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        if (codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, instruction->canonical_type))
                        {
                            if (instruction->opcode == IR_OPCODE_ATOMIC_LOAD || !loaded_value_type || loaded_value_type->layout.size != 16 ||
                                result.abi != CODEGEN_ABI_X86_64_SYSTEM_V ||
                                !codegen_canonical_x64_type_is_f80_x87_shape_cached(f80_cache, program, instruction->canonical_type))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            if (indirect)
                            {
                                c_x64_load(&emitter, 0x85, instruction->operands[0]);
                                if (!codegen_canonical_x64_emit_f80_copy(&buffer, X64_REGISTER_RAX, 0, X64_REGISTER_RBP, result_displacement,
                                                                          &x87_stack_depth))
                                {
                                    result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                            }
                            else if (!codegen_canonical_x64_emit_f80_copy(
                                         &buffer, X64_REGISTER_RBP, c_x64_frame_displacement(&emitter, value_offsets[instruction->operands[0].value]),
                                         X64_REGISTER_RBP, result_displacement, &x87_stack_depth))
                            {
                                result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (instruction->opcode == IR_OPCODE_ATOMIC_LOAD && aggregate)
                        {
                            if (!aggregate_type || aggregate_type->kind != IR_TYPE_INTEGER || aggregate_type->layout.size != 16 ||
                                !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_CX16))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            c_x64_atomic_address(&emitter, instruction->operands[0], indirect);
                            BusterX86MetadataPhysicalOperand zero_rax_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                            };
                            BusterX86MetadataPhysicalOperand zero_rdx_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 32),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 32),
                            };
                            if (buffer.error || !codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), zero_rax_operands,
                                                                                       BUSTER_ARRAY_LENGTH(zero_rax_operands)) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), zero_rdx_operands,
                                                                     BUSTER_ARRAY_LENGTH(zero_rdx_operands)))
                            {
                                result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            u32 retry = (u32)buffer.count;
                            BusterX86MetadataPhysicalOperand move_rax_to_rbx_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RBX, 64),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            };
                            BusterX86MetadataPhysicalOperand move_rdx_to_rcx_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), move_rax_to_rbx_operands,
                                                                       BUSTER_ARRAY_LENGTH(move_rax_to_rbx_operands)) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), move_rdx_to_rcx_operands,
                                                                     BUSTER_ARRAY_LENGTH(move_rdx_to_rcx_operands)))
                            {
                                result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            BusterX86MetadataPhysicalOperand atomic_memory =
                                codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_R10, 128, 0);
                            String8 cx16_features_names[] = {S8("cx16")};
                            if (!codegen_canonical_x64_metadata_emit_attributes(
                                    &buffer, S8("CMPXCHG16B"), &atomic_memory, 1,
                                    (BusterX86MetadataFeatureInput){.names = cx16_features_names,
                                                                     .count = BUSTER_ARRAY_LENGTH(cx16_features_names)},
                                    (BusterX86MetadataPhysicalAttributes){.lock = true}))
                            {
                                result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            BusterX86MetadataPhysicalOperand retry_operand = codegen_canonical_x64_metadata_relative(0, 32);
                            // The branch is emitted with a neutral rel32 and patched after the metadata encoder reports its
                            // exact length.  Keep the retry displacement as the only hand-written field in this sequence.
                            u32 retry_branch_offset = (u32)buffer.count;
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JNZ"), &retry_operand, 1))
                            {
                                result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            s64 retry_delta = (s64)retry - ((s64)retry_branch_offset + 6);
                            if (retry_delta < INT32_MIN || retry_delta > INT32_MAX || !buffer.bytes || retry_branch_offset + 6 > buffer.count)
                            {
                                result.error = retry_delta < INT32_MIN || retry_delta > INT32_MAX ? CODEGEN_ERROR_CAPACITY
                                                                                                    : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            s32 retry_displacement = (s32)retry_delta;
                            memcpy(buffer.bytes + retry_branch_offset + 2, &retry_displacement, sizeof(retry_displacement));
                            c_x64_store_result(&emitter, result_displacement);
                            c_x64_store_high_rdx(&emitter, result_displacement);
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (aggregate)
                        {
                            if (!aggregate_type || !aggregate_type->layout.resolved)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            if (indirect)
                            {
                                c_x64_load(&emitter, 0x95, instruction->operands[0]);
                                if (buffer.error)
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            for (u32 part_index = 0; part_index < aggregate_parts; part_index += 1)
                            {
                                u64 part_offset = (u64)part_index * 8;
                                u64 part_size = BUSTER_MIN((u64)8, aggregate_type->layout.size - part_offset);
                                u64 part_copied = 0;
                                while (part_copied < part_size)
                                {
                                    u64 remaining = part_size - part_copied;
                                    u32 chunk = remaining >= 8 ? 8 : remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;
                                    u64 copy_offset = part_offset + part_copied;
                                    s32 source_displacement =
                                        c_x64_frame_displacement(&emitter, value_offsets[instruction->operands[0].value]);
                                    if (copy_offset > INT32_MAX ||
                                        (!indirect && ((s64)source_displacement + (s64)copy_offset > INT32_MAX ||
                                                       (s64)source_displacement + (s64)copy_offset < INT32_MIN)) ||
                                        (indirect && (s64)copy_offset > INT32_MAX))
                                    {
                                        result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                        return result;
                                    }
                                    u16 chunk_width = (u16)(chunk * 8);
                                    u16 load_register_width = chunk <= 4 ? 32 : 64;
                                    String8 load_mnemonic = chunk == 1 || chunk == 2 ? S8("MOVZX") : S8("MOV");
                                    BusterX86MetadataPhysicalOperand load_operands[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, load_register_width),
                                        indirect ? codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RDX, chunk_width,
                                                                                                  (s64)copy_offset)
                                                 : codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, chunk_width,
                                                                                          (s64)source_displacement + (s64)copy_offset),
                                    };
                                    if (!codegen_canonical_x64_metadata_emit(&buffer, load_mnemonic, load_operands,
                                                                               BUSTER_ARRAY_LENGTH(load_operands)))
                                    {
                                        result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                        return result;
                                    }
                                    if ((s64)result_displacement + (s64)copy_offset > INT32_MAX ||
                                        (s64)result_displacement + (s64)copy_offset < INT32_MIN)
                                    {
                                        result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                        return result;
                                    }
                                    BusterX86MetadataPhysicalOperand store_operands[2] = {
                                        codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, chunk_width,
                                                                                (s64)result_displacement + (s64)copy_offset),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, chunk_width),
                                    };
                                    if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), store_operands,
                                                                               BUSTER_ARRAY_LENGTH(store_operands)))
                                    {
                                        result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                        return result;
                                    }
                                    part_copied += chunk;
                                }
                            }
                        }
                        else if (indirect)
                        {
                            IrType* loaded_type = ir_type_from_id(&program->types, instruction->canonical_type);
                            if (!loaded_type || !loaded_type->layout.resolved ||
                                (loaded_type->layout.size != 1 && loaded_type->layout.size != 2 && loaded_type->layout.size != 4 &&
                                 loaded_type->layout.size != 8))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            c_x64_load(&emitter, 0x85, instruction->operands[0]);
                            u16 load_width = (u16)(loaded_type->layout.size * 8);
                            BusterX86MetadataPhysicalOperand load_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, loaded_type->layout.size <= 2 ? 32 : load_width),
                                codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RAX, load_width, 0),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, loaded_type->layout.size <= 2 ? S8("MOVZX") : S8("MOV"),
                                                                       load_operands, BUSTER_ARRAY_LENGTH(load_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                        else
                        {
                            c_x64_load(&emitter, 0x85, instruction->operands[0]);
                        }
                        if (!aggregate && instruction->result.value != IR_ID_UNDERLYING_INVALID)
                        {
                            c_x64_store_result(&emitter, result_displacement);
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_INDEX)
                    {
                        IrValueId base = instruction->operands[0];
                        IrInstruction* base_definition = function->instructions + function->values[base.value].definition.value;
                        IrType* base_type = ir_type_from_id(&program->types, function->values[base.value].canonical_type);
                        if (base_definition->opcode == IR_OPCODE_LOCAL || (function->values[base.value].category == IR_VALUE_VALUE && base_type &&
                                                                           (base_type->kind == IR_TYPE_ARRAY || base_type->kind == IR_TYPE_VECTOR)))
                        {
                            if (base_definition->opcode == IR_OPCODE_LOCAL && function->values[base.value].alignment > 16)
                            {
                                c_x64_load(&emitter, 0x85, base);
                            }
                            else
                            {
                                BusterX86MetadataPhysicalOperand base_address_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_memory(
                                        X64_REGISTER_RBP, 64, c_x64_frame_displacement(&emitter, value_offsets[base.value])),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("LEA"), base_address_operands,
                                                                           BUSTER_ARRAY_LENGTH(base_address_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                        }
                        else
                        {
                            c_x64_load(&emitter, 0x85, base);
                        }
                        c_x64_load(&emitter, 0x8d, instruction->operands[1]);
                        IrType* index_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
                        IrType* element = ir_type_from_id(&program->types, instruction->canonical_type);
                        if (!index_type || index_type->kind != IR_TYPE_INTEGER || !element || element->layout.size > UINT32_MAX)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        if (index_type->is_signed && index_type->bit_width < 64)
                        {
                            String8 extend_mnemonic = index_type->bit_width == 32 ? S8("MOVSXD") : S8("MOVSX");
                            if (index_type->bit_width != 8 && index_type->bit_width != 16 && index_type->bit_width != 32)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            BusterX86MetadataPhysicalOperand extend_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, (u16)index_type->bit_width),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, extend_mnemonic, extend_operands,
                                                                       BUSTER_ARRAY_LENGTH(extend_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                        // Byte-element indexing scales by one, so the multiply
                        // is the identity. The following add overwrites flags
                        // without reading them.
                        if (element->layout.size != 1)
                        {
                            BusterX86MetadataPhysicalOperand scale_load_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 32),
                                codegen_canonical_x64_metadata_unsigned_immediate((u64)element->layout.size, 32),
                            };
                            BusterX86MetadataPhysicalOperand scale_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), scale_load_operands,
                                                                       BUSTER_ARRAY_LENGTH(scale_load_operands)) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("IMUL"), scale_operands,
                                                                       BUSTER_ARRAY_LENGTH(scale_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                        BusterX86MetadataPhysicalOperand add_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                        };
                        if (!codegen_canonical_x64_metadata_emit(&buffer, S8("ADD"), add_operands,
                                                                   BUSTER_ARRAY_LENGTH(add_operands)))
                        {
                            result.error = buffer.error;
                            return result;
                        }
                        c_x64_store_result(&emitter, result_displacement);
                    }
                    else if (instruction->opcode == IR_OPCODE_ADDRESS_OF)
                    {
                        IrValueId object = instruction->operands[0];
                        IrInstruction* definition = function->instructions + function->values[object.value].definition.value;
                        if (definition->opcode == IR_OPCODE_LOCAL)
                        {
                            if (function->values[object.value].alignment > 16)
                            {
                                c_x64_load(&emitter, 0x85, object);
                            }
                            else
                            {
                                BusterX86MetadataPhysicalOperand address_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_memory(
                                        X64_REGISTER_RBP, 64, c_x64_frame_displacement(&emitter, value_offsets[object.value])),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("LEA"), address_operands,
                                                                           BUSTER_ARRAY_LENGTH(address_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                        }
                        else
                        {
                            c_x64_load(&emitter, 0x85, object);
                        }
                        c_x64_store_result(&emitter, result_displacement);
                    }
                    else if (instruction->opcode == IR_OPCODE_DEREFERENCE)
                    {
                        c_x64_load(&emitter, 0x85, instruction->operands[0]);
                        c_x64_store_result(&emitter, result_displacement);
                    }
                    else if (instruction->opcode == IR_OPCODE_FIELD)
                    {
                        IrValueId base = instruction->operands[0];
                        IrInstruction* definition = function->instructions + function->values[base.value].definition.value;
                        if (definition->opcode == IR_OPCODE_LOCAL)
                        {
                            if (function->values[base.value].alignment > 16)
                            {
                                c_x64_load(&emitter, 0x85, base);
                            }
                            else
                            {
                                BusterX86MetadataPhysicalOperand field_address_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_memory(
                                        X64_REGISTER_RBP, 64, c_x64_frame_displacement(&emitter, value_offsets[base.value])),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("LEA"), field_address_operands,
                                                                           BUSTER_ARRAY_LENGTH(field_address_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                        }
                        else
                        {
                            c_x64_load(&emitter, 0x85, base);
                        }
                        IrType* aggregate = ir_type_from_id(&program->types, function->values[base.value].canonical_type);
                        u64 field_index = instruction->immediates[0];
                        if (!aggregate || field_index >= aggregate->field_count || aggregate->fields[field_index].offset > INT32_MAX)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        // The first field of an aggregate sits at offset zero,
                        // so the address arithmetic is the identity. Only the
                        // result store follows, and it does not read flags.
                        if (aggregate->fields[field_index].offset)
                        {
                            BusterX86MetadataPhysicalOperand field_add_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                codegen_canonical_x64_metadata_immediate((s64)aggregate->fields[field_index].offset, 32),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("ADD"), field_add_operands,
                                                                       BUSTER_ARRAY_LENGTH(field_add_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                        c_x64_store_result(&emitter, result_displacement);
                    }
                    else if (instruction->opcode == IR_OPCODE_CAST)
                    {
                        IrType* source_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
                        IrType* target_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        IrConversionOperation conversion = instruction->conversion_operation;
                        if (!target_type || !source_type)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        bool source_contains_f80 = codegen_canonical_x64_type_contains_f80_cached(
                            f80_cache, program, function->values[instruction->operands[0].value].canonical_type);
                        bool target_contains_f80 = codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, instruction->canonical_type);
                        if (source_contains_f80 || target_contains_f80)
                        {
                            if (conversion != IR_CONVERSION_IDENTITY || !source_contains_f80 || !target_contains_f80 ||
                                result.abi != CODEGEN_ABI_X86_64_SYSTEM_V || !source_type->layout.resolved || !target_type->layout.resolved ||
                                source_type->layout.size != 16 || target_type->layout.size != 16 ||
                                !codegen_canonical_x64_type_is_f80_x87_shape_cached(
                                    f80_cache, program, function->values[instruction->operands[0].value].canonical_type) ||
                                !codegen_canonical_x64_type_is_f80_x87_shape_cached(f80_cache, program, instruction->canonical_type) ||
                                !codegen_canonical_x64_emit_f80_copy(
                                    &buffer, X64_REGISTER_RBP,
                                    c_x64_frame_displacement(&emitter, value_offsets[instruction->operands[0].value]), X64_REGISTER_RBP,
                                    result_displacement, &x87_stack_depth))
                            {
                                result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        bool source_integer128 = source_type->kind == IR_TYPE_INTEGER && source_type->bit_width == 128;
                        bool target_integer128 = target_type->kind == IR_TYPE_INTEGER && target_type->bit_width == 128;
                        if (source_integer128 || target_integer128)
                        {
                            if (source_type->kind != IR_TYPE_INTEGER || target_type->kind != IR_TYPE_INTEGER)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            c_x64_load(&emitter, 0x85, instruction->operands[0]);
                            if (target_integer128)
                            {
                                if (source_integer128)
                                {
                                    c_x64_load_high(&emitter, 0x95, instruction->operands[0]);
                                }
                                else if (conversion == IR_CONVERSION_INTEGER_SIGN_EXTEND)
                                {
                                    String8 extend_mnemonic = {0};
                                    BusterX86MetadataPhysicalOperand extend_operands[2];
                                    u32 extend_operand_count = 0;
                                    if (source_type->bit_width == 8 || source_type->bit_width == 16)
                                    {
                                        extend_mnemonic = S8("MOVSX");
                                        extend_operands[0] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64);
                                        extend_operands[1] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, (u16)source_type->bit_width);
                                        extend_operand_count = 2;
                                    }
                                    else if (source_type->bit_width == 32)
                                    {
                                        extend_mnemonic = S8("MOVSXD");
                                        extend_operands[0] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64);
                                        extend_operands[1] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32);
                                        extend_operand_count = 2;
                                    }
                                    if ((extend_operand_count && !codegen_canonical_x64_metadata_emit(&buffer, extend_mnemonic, extend_operands, extend_operand_count)) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("CQO"), 0, 0))
                                    {
                                        result.error = buffer.error;
                                        return result;
                                    }
                                }
                                else
                                {
                                    BusterX86MetadataPhysicalOperand zero_extend_operands[2];
                                    u32 zero_extend_operand_count = 0;
                                    if (source_type->bit_width <= 32)
                                    {
                                        if (source_type->bit_width < 32)
                                        {
                                            zero_extend_operands[0] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32);
                                            zero_extend_operands[1] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, (u16)source_type->bit_width);
                                            zero_extend_operand_count = 2;
                                        }
                                        else
                                        {
                                            zero_extend_operands[0] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32);
                                            zero_extend_operands[1] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32);
                                            zero_extend_operand_count = 2;
                                        }
                                    }
                                    BusterX86MetadataPhysicalOperand clear_high_operands[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                    };
                                    String8 zero_extend_mnemonic = source_type->bit_width < 32 ? S8("MOVZX") : S8("MOV");
                                    if ((zero_extend_operand_count && !codegen_canonical_x64_metadata_emit(&buffer, zero_extend_mnemonic, zero_extend_operands,
                                                                                                             zero_extend_operand_count)) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), clear_high_operands, 2))
                                    {
                                        result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                        return result;
                                    }
                                }
                                c_x64_store_result(&emitter, result_displacement);
                                c_x64_store_high_rdx(&emitter, result_displacement);
                            }
                            else
                            {
                                if (target_type->bit_width != 8 && target_type->bit_width != 16 && target_type->bit_width != 32 &&
                                    target_type->bit_width != 64)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                                BusterX86MetadataPhysicalOperand truncate_operands[2];
                                u32 truncate_operand_count = 0;
                                if (target_type->bit_width == 8 || target_type->bit_width == 16)
                                {
                                    truncate_operands[0] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32);
                                    truncate_operands[1] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, (u16)target_type->bit_width);
                                    truncate_operand_count = 2;
                                }
                                else if (target_type->bit_width == 32)
                                {
                                    truncate_operands[0] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32);
                                    truncate_operands[1] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32);
                                    truncate_operand_count = 2;
                                }
                                if (truncate_operand_count && !codegen_canonical_x64_metadata_emit(&buffer, S8("MOVZX"), truncate_operands,
                                                                                                     truncate_operand_count))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                c_x64_store_result(&emitter, result_displacement);
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        c_x64_load(&emitter, 0x85, instruction->operands[0]);
                        if (buffer.error)
                        {
                            result.error = buffer.error;
                            return result;
                        }
                        bool source_integer = source_type->kind == IR_TYPE_INTEGER || source_type->kind == IR_TYPE_BOOLEAN ||
                                              source_type->kind == IR_TYPE_POINTER;
                        bool target_integer = target_type->kind == IR_TYPE_INTEGER || target_type->kind == IR_TYPE_BOOLEAN ||
                                              target_type->kind == IR_TYPE_POINTER;
                        if (source_integer && target_integer &&
                            (conversion == IR_CONVERSION_INTEGER_SIGN_EXTEND || conversion == IR_CONVERSION_INTEGER_ZERO_EXTEND ||
                             conversion == IR_CONVERSION_INTEGER_TRUNCATE || conversion == IR_CONVERSION_INTEGER_REINTERPRET ||
                             conversion == IR_CONVERSION_POINTER_TO_INTEGER || conversion == IR_CONVERSION_INTEGER_TO_POINTER ||
                             conversion == IR_CONVERSION_POINTER_REINTERPRET || conversion == IR_CONVERSION_IDENTITY))
                        {
                            u32 source_bit_width = source_type->kind == IR_TYPE_BOOLEAN ? 8
                                                 : source_type->kind == IR_TYPE_POINTER ? 64
                                                                                         : source_type->bit_width;
                            u32 target_bit_width = target_type->kind == IR_TYPE_BOOLEAN ? 8
                                                 : target_type->kind == IR_TYPE_POINTER ? 64
                                                                                         : target_type->bit_width;
                            if ((source_bit_width != 8 && source_bit_width != 16 && source_bit_width != 32 && source_bit_width != 64) ||
                                (target_bit_width != 8 && target_bit_width != 16 && target_bit_width != 32 && target_bit_width != 64))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            String8 mnemonic = {0};
                            BusterX86MetadataPhysicalOperand cast_operands[2];
                            u32 cast_operand_count = 2;
                            if (conversion == IR_CONVERSION_INTEGER_SIGN_EXTEND)
                            {
                                if (source_bit_width == 64)
                                {
                                    // A 64-bit source is already in the canonical
                                    // register width; no instruction is required.
                                    cast_operand_count = 0;
                                }
                                else
                                {
                                    mnemonic = source_bit_width == 32 ? S8("MOVSXD") : S8("MOVSX");
                                    cast_operands[0] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64);
                                    cast_operands[1] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, (u16)source_bit_width);
                                }
                            }
                            else if (conversion == IR_CONVERSION_INTEGER_ZERO_EXTEND)
                            {
                                if (source_bit_width == 64)
                                {
                                    cast_operand_count = 0;
                                }
                                else if (source_bit_width == 32)
                                {
                                    mnemonic = S8("MOV");
                                    cast_operands[0] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32);
                                    cast_operands[1] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32);
                                }
                                else
                                {
                                    mnemonic = S8("MOVZX");
                                    cast_operands[0] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32);
                                    cast_operands[1] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, (u16)source_bit_width);
                                }
                            }
                            else if (conversion == IR_CONVERSION_INTEGER_TRUNCATE)
                            {
                                if (target_bit_width == 64)
                                {
                                    cast_operand_count = 0;
                                }
                                else if (target_bit_width == 32)
                                {
                                    mnemonic = S8("MOV");
                                    cast_operands[0] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32);
                                    cast_operands[1] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32);
                                }
                                else
                                {
                                    mnemonic = S8("MOVZX");
                                    cast_operands[0] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32);
                                    cast_operands[1] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, (u16)target_bit_width);
                                }
                            }
                            else if (conversion == IR_CONVERSION_INTEGER_REINTERPRET || conversion == IR_CONVERSION_POINTER_REINTERPRET ||
                                     conversion == IR_CONVERSION_POINTER_TO_INTEGER || conversion == IR_CONVERSION_INTEGER_TO_POINTER ||
                                     conversion == IR_CONVERSION_IDENTITY)
                            {
                                u32 effective_bit_width = conversion == IR_CONVERSION_INTEGER_REINTERPRET
                                                               ? BUSTER_MIN(source_bit_width, target_bit_width)
                                                               : target_bit_width;
                                if (effective_bit_width >= 64)
                                {
                                    cast_operand_count = 0;
                                }
                                else if (effective_bit_width == 32)
                                {
                                    mnemonic = S8("MOV");
                                    cast_operands[0] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32);
                                    cast_operands[1] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32);
                                }
                                else
                                {
                                    mnemonic = S8("MOVZX");
                                    cast_operands[0] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32);
                                    cast_operands[1] = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, (u16)effective_bit_width);
                                }
                            }
                            else
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            if (cast_operand_count && !codegen_canonical_x64_metadata_emit(&buffer, mnemonic, cast_operands, cast_operand_count))
                            {
                                result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                        }
                        else if ((conversion == IR_CONVERSION_FLOAT_EXTEND || conversion == IR_CONVERSION_FLOAT_TRUNCATE) &&
                                 source_type->kind == IR_TYPE_FLOAT && target_type->kind == IR_TYPE_FLOAT)
                        {
                            if ((source_type->bit_width != 32 && source_type->bit_width != 64) ||
                                (target_type->bit_width != 32 && target_type->bit_width != 64) ||
                                (conversion == IR_CONVERSION_FLOAT_EXTEND && (source_type->bit_width != 32 || target_type->bit_width != 64)) ||
                                (conversion == IR_CONVERSION_FLOAT_TRUNCATE && (source_type->bit_width != 64 || target_type->bit_width != 32)))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            String8 float_features[] = {S8("sse"), S8("sse2")};
                            BusterX86MetadataFeatureInput float_feature_input = {
                                .names = float_features,
                                .count = BUSTER_ARRAY_LENGTH(float_features),
                            };
                            BusterX86MetadataPhysicalOperand load_operands[2] = {
                                codegen_canonical_x64_metadata_vector(0, (u16)source_type->bit_width),
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, (u16)source_type->bit_width,
                                                                       c_x64_frame_displacement(&emitter, value_offsets[instruction->operands[0].value])),
                            };
                            String8 load_mnemonic = source_type->bit_width == 32 ? S8("MOVSS") : S8("MOVSD");
                            String8 convert_mnemonic = conversion == IR_CONVERSION_FLOAT_EXTEND ? S8("CVTSS2SD") : S8("CVTSD2SS");
                            BusterX86MetadataPhysicalOperand convert_operands[2] = {
                                codegen_canonical_x64_metadata_vector(0, (u16)target_type->bit_width),
                                codegen_canonical_x64_metadata_vector(0, (u16)source_type->bit_width),
                            };
                            BusterX86MetadataPhysicalOperand store_operands[2] = {
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, (u16)target_type->bit_width, result_displacement),
                                codegen_canonical_x64_metadata_vector(0, (u16)target_type->bit_width),
                            };
                            String8 store_mnemonic = target_type->bit_width == 32 ? S8("MOVSS") : S8("MOVSD");
                            if (!codegen_canonical_x64_metadata_emit_features(&buffer, load_mnemonic, load_operands,
                                                                              BUSTER_ARRAY_LENGTH(load_operands), float_feature_input) ||
                                !codegen_canonical_x64_metadata_emit_features(&buffer, convert_mnemonic, convert_operands,
                                                                              BUSTER_ARRAY_LENGTH(convert_operands), float_feature_input) ||
                                !codegen_canonical_x64_metadata_emit_features(&buffer, store_mnemonic, store_operands,
                                                                              BUSTER_ARRAY_LENGTH(store_operands), float_feature_input))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        else if (target_type->kind == IR_TYPE_FLOAT && source_type->kind == IR_TYPE_INTEGER &&
                                 (conversion == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT || conversion == IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT))
                        {
                            u32 source_bit_width = source_type->bit_width;
                            u32 target_bit_width = target_type->bit_width;
                            if ((source_bit_width != 8 && source_bit_width != 16 && source_bit_width != 32 && source_bit_width != 64) ||
                                (target_bit_width != 32 && target_bit_width != 64))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            String8 float_features[] = {S8("sse"), S8("sse2")};
                            BusterX86MetadataFeatureInput float_feature_input = {
                                .names = float_features,
                                .count = BUSTER_ARRAY_LENGTH(float_features),
                            };
                            if (conversion == IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT && source_bit_width == 64)
                            {
                                BusterX86MetadataPhysicalOperand test_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                };
                                BusterX86MetadataPhysicalOperand nonnegative_branch = codegen_canonical_x64_metadata_relative(0, 8);
                                u32 nonnegative_branch_offset = 0;
                                BusterX86MetadataPhysicalOperand correction_move[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                };
                                BusterX86MetadataPhysicalOperand correction_shift[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_immediate(1, 8),
                                };
                                BusterX86MetadataPhysicalOperand correction_mask[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                    codegen_canonical_x64_metadata_immediate(1, 8),
                                };
                                BusterX86MetadataPhysicalOperand correction_or[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                };
                                BusterX86MetadataPhysicalOperand convert_operands[2] = {
                                    codegen_canonical_x64_metadata_vector(0, (u16)target_bit_width),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                };
                                String8 convert_mnemonic = target_bit_width == 32 ? S8("CVTSI2SS") : S8("CVTSI2SD");
                                BusterX86MetadataPhysicalOperand add_operands[2] = {
                                    codegen_canonical_x64_metadata_vector(0, (u16)target_bit_width),
                                    codegen_canonical_x64_metadata_vector(0, (u16)target_bit_width),
                                };
                                String8 add_mnemonic = target_bit_width == 32 ? S8("ADDSS") : S8("ADDSD");
                                BusterX86MetadataPhysicalOperand skip_branch = codegen_canonical_x64_metadata_relative(0, 8);
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("TEST"), test_operands, 2))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                nonnegative_branch_offset = (u32)buffer.count;
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JNS"), &nonnegative_branch, 1) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), correction_move, 2) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("SHR"), correction_shift, 2) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("AND"), correction_mask, 2) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("OR"), correction_or, 2) ||
                                    !codegen_canonical_x64_metadata_emit_features(&buffer, convert_mnemonic, convert_operands, 2, float_feature_input) ||
                                    !codegen_canonical_x64_metadata_emit_features(&buffer, add_mnemonic, add_operands, 2, float_feature_input))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                u32 skip_branch_offset = (u32)buffer.count;
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JMP"), &skip_branch, 1))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                u32 direct_conversion_offset = (u32)buffer.count;
                                if (!codegen_canonical_x64_metadata_emit_features(&buffer, convert_mnemonic, convert_operands, 2, float_feature_input))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                u32 store_offset = (u32)buffer.count;
                                BusterX86MetadataPhysicalOperand store_operands[2] = {
                                    codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, (u16)target_bit_width, result_displacement),
                                    codegen_canonical_x64_metadata_vector(0, (u16)target_bit_width),
                                };
                                String8 store_mnemonic = target_bit_width == 32 ? S8("MOVSS") : S8("MOVSD");
                                if (!codegen_canonical_x64_metadata_emit_features(&buffer, store_mnemonic, store_operands, 2, float_feature_input))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                if (buffer.error == CODEGEN_ERROR_NONE && nonnegative_branch_offset + 2 <= buffer.count && skip_branch_offset + 2 <= buffer.count)
                                {
                                    s8 nonnegative_delta = (s8)((s32)direct_conversion_offset - (s32)(nonnegative_branch_offset + 2));
                                    s8 skip_delta = (s8)((s32)store_offset - (s32)(skip_branch_offset + 2));
                                    memcpy(buffer.bytes + nonnegative_branch_offset + 1, &nonnegative_delta, sizeof(nonnegative_delta));
                                    memcpy(buffer.bytes + skip_branch_offset + 1, &skip_delta, sizeof(skip_delta));
                                }
                                else if (buffer.error == CODEGEN_ERROR_NONE)
                                {
                                    buffer.error = CODEGEN_ERROR_CAPACITY;
                                }
                                if (buffer.error != CODEGEN_ERROR_NONE)
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                instruction_id = instruction->next;
                                continue;
                            }
                            if (conversion == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT && source_bit_width != 64)
                            {
                                String8 extend_mnemonic = source_bit_width == 32 ? S8("MOVSXD") : S8("MOVSX");
                                BusterX86MetadataPhysicalOperand extend_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, (u16)source_bit_width),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, extend_mnemonic, extend_operands,
                                                                           BUSTER_ARRAY_LENGTH(extend_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            else if (conversion == IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT && source_bit_width < 32)
                            {
                                BusterX86MetadataPhysicalOperand zero_extend_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, (u16)source_bit_width),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOVZX"), zero_extend_operands,
                                                                           BUSTER_ARRAY_LENGTH(zero_extend_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            else if (conversion == IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT && source_bit_width == 32)
                            {
                                // C integer arguments occupy an eight-byte canonical
                                // slot, while an unsigned 32-bit parameter is
                                // defined by its low word.  Clear the incoming
                                // high bits before the 64-bit conversion so a
                                // caller that supplies a sign-extended register
                                // value still observes unsigned semantics.
                                BusterX86MetadataPhysicalOperand zero_extend_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), zero_extend_operands,
                                                                           BUSTER_ARRAY_LENGTH(zero_extend_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            BusterX86MetadataPhysicalOperand convert_operands[2] = {
                                codegen_canonical_x64_metadata_vector(0, (u16)target_bit_width),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            };
                            String8 convert_mnemonic = target_bit_width == 32 ? S8("CVTSI2SS") : S8("CVTSI2SD");
                            BusterX86MetadataPhysicalOperand store_operands[2] = {
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, (u16)target_bit_width, result_displacement),
                                codegen_canonical_x64_metadata_vector(0, (u16)target_bit_width),
                            };
                            String8 store_mnemonic = target_bit_width == 32 ? S8("MOVSS") : S8("MOVSD");
                            if (!codegen_canonical_x64_metadata_emit_features(&buffer, convert_mnemonic, convert_operands,
                                                                              BUSTER_ARRAY_LENGTH(convert_operands), float_feature_input) ||
                                !codegen_canonical_x64_metadata_emit_features(&buffer, store_mnemonic, store_operands,
                                                                              BUSTER_ARRAY_LENGTH(store_operands), float_feature_input))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        else if (source_type->kind == IR_TYPE_FLOAT && target_type->kind == IR_TYPE_INTEGER &&
                                 (conversion == IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER ||
                                  (conversion == IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER && target_type->bit_width != 64)))
                        {
                            u32 source_bit_width = source_type->bit_width;
                            u32 target_bit_width = target_type->bit_width;
                            // Keep the conversion in the 64-bit signed range
                            // even for narrower C destinations.  This is
                            // required for unsigned 32-bit values above
                            // INT32_MAX (for example 4000000000.0), after
                            // which the canonical frame store narrows as
                            // usual.
                            u32 conversion_bit_width = 64;
                            if ((source_bit_width != 32 && source_bit_width != 64) ||
                                (target_bit_width != 8 && target_bit_width != 16 && target_bit_width != 32 && target_bit_width != 64) ||
                                (conversion == IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER && target_bit_width == 64))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            String8 float_features[] = {S8("sse"), S8("sse2")};
                            BusterX86MetadataFeatureInput float_feature_input = {
                                .names = float_features,
                                .count = BUSTER_ARRAY_LENGTH(float_features),
                            };
                            BusterX86MetadataPhysicalOperand load_operands[2] = {
                                codegen_canonical_x64_metadata_vector(0, (u16)source_bit_width),
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, (u16)source_bit_width,
                                                                       c_x64_frame_displacement(&emitter, value_offsets[instruction->operands[0].value])),
                            };
                            String8 load_mnemonic = source_bit_width == 32 ? S8("MOVSS") : S8("MOVSD");
                            BusterX86MetadataPhysicalOperand convert_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, (u16)conversion_bit_width),
                                codegen_canonical_x64_metadata_vector(0, (u16)source_bit_width),
                            };
                            String8 convert_mnemonic = source_bit_width == 32 ? S8("CVTTSS2SI") : S8("CVTTSD2SI");
                            if (!codegen_canonical_x64_metadata_emit_features(&buffer, load_mnemonic, load_operands,
                                                                              BUSTER_ARRAY_LENGTH(load_operands), float_feature_input) ||
                                !codegen_canonical_x64_metadata_emit_features(&buffer, convert_mnemonic, convert_operands,
                                                                              BUSTER_ARRAY_LENGTH(convert_operands), float_feature_input))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            c_x64_store_result(&emitter, result_displacement);
                            instruction_id = instruction->next;
                            continue;
                        }
                        else if (source_type->kind == IR_TYPE_FLOAT && target_type->kind == IR_TYPE_INTEGER &&
                                 conversion == IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER && target_type->bit_width == 64)
                        {
                            if (source_type->bit_width != 32 && source_type->bit_width != 64)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            String8 float_features[] = {S8("sse"), S8("sse2")};
                            BusterX86MetadataFeatureInput float_feature_input = {
                                .names = float_features,
                                .count = BUSTER_ARRAY_LENGTH(float_features),
                            };
                            u16 float_width = (u16)source_type->bit_width;
                            String8 load_mnemonic = source_type->bit_width == 32 ? S8("MOVSS") : S8("MOVSD");
                            String8 compare_mnemonic = source_type->bit_width == 32 ? S8("UCOMISS") : S8("UCOMISD");
                            String8 subtract_mnemonic = source_type->bit_width == 32 ? S8("SUBSS") : S8("SUBSD");
                            String8 convert_mnemonic = source_type->bit_width == 32 ? S8("CVTTSS2SI") : S8("CVTTSD2SI");
                            BusterX86MetadataPhysicalOperand load_operands[2] = {
                                codegen_canonical_x64_metadata_vector(0, float_width),
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, float_width,
                                                                       c_x64_frame_displacement(&emitter, value_offsets[instruction->operands[0].value])),
                            };
                            BusterX86MetadataPhysicalOperand threshold_load[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, source_type->bit_width == 32 ? 32 : 64),
                                codegen_canonical_x64_metadata_unsigned_immediate(
                                    source_type->bit_width == 32 ? UINT64_C(0x5f000000) : UINT64_C(0x43e0000000000000),
                                    source_type->bit_width == 32 ? 32 : 64),
                            };
                            BusterX86MetadataPhysicalOperand threshold_vector[2] = {
                                codegen_canonical_x64_metadata_vector(1, float_width),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, source_type->bit_width == 32 ? 32 : 64),
                            };
                            BusterX86MetadataPhysicalOperand compare_operands[2] = {
                                codegen_canonical_x64_metadata_vector(0, float_width),
                                codegen_canonical_x64_metadata_vector(1, float_width),
                            };
                            BusterX86MetadataPhysicalOperand subtract_operands[2] = {
                                codegen_canonical_x64_metadata_vector(0, float_width),
                                codegen_canonical_x64_metadata_vector(1, float_width),
                            };
                            BusterX86MetadataPhysicalOperand convert_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                codegen_canonical_x64_metadata_vector(0, float_width),
                            };
                            BusterX86MetadataPhysicalOperand high_bit_load[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                codegen_canonical_x64_metadata_unsigned_immediate(UINT64_C(0x8000000000000000), 64),
                            };
                            BusterX86MetadataPhysicalOperand high_bit_or[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                            };
                            if (!codegen_canonical_x64_metadata_emit_features(&buffer, load_mnemonic, load_operands, 2, float_feature_input) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), threshold_load, 2) ||
                                !codegen_canonical_x64_metadata_emit_features(&buffer, source_type->bit_width == 32 ? S8("MOVD") : S8("MOVQ"),
                                                                                threshold_vector, 2, float_feature_input) ||
                                !codegen_canonical_x64_metadata_emit_features(&buffer, compare_mnemonic, compare_operands, 2, float_feature_input))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            u32 below_threshold_branch_offset = (u32)buffer.count;
                            BusterX86MetadataPhysicalOperand below_threshold_branch = codegen_canonical_x64_metadata_relative(0, 8);
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JB"), &below_threshold_branch, 1) ||
                                !codegen_canonical_x64_metadata_emit_features(&buffer, subtract_mnemonic, subtract_operands, 2, float_feature_input) ||
                                !codegen_canonical_x64_metadata_emit_features(&buffer, convert_mnemonic, convert_operands, 2, float_feature_input) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), high_bit_load, 2) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("OR"), high_bit_or, 2))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            u32 skip_direct_branch_offset = (u32)buffer.count;
                            BusterX86MetadataPhysicalOperand skip_direct_branch = codegen_canonical_x64_metadata_relative(0, 8);
                            u32 direct_conversion_offset = skip_direct_branch_offset + 2;
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JMP"), &skip_direct_branch, 1) ||
                                !codegen_canonical_x64_metadata_emit_features(&buffer, convert_mnemonic, convert_operands, 2, float_feature_input))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            s64 below_threshold_delta = (s64)direct_conversion_offset - ((s64)below_threshold_branch_offset + 2);
                            s64 skip_direct_delta = (s64)buffer.count - ((s64)skip_direct_branch_offset + 2);
                            if (below_threshold_delta < INT8_MIN || below_threshold_delta > INT8_MAX || skip_direct_delta < INT8_MIN ||
                                skip_direct_delta > INT8_MAX || below_threshold_branch_offset + 1 >= buffer.count || skip_direct_branch_offset + 1 >= buffer.count)
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                            buffer.bytes[below_threshold_branch_offset + 1] = (u8)(s8)below_threshold_delta;
                            buffer.bytes[skip_direct_branch_offset + 1] = (u8)(s8)skip_direct_delta;
                            c_x64_store_result(&emitter, result_displacement);
                            instruction_id = instruction->next;
                            continue;
                        }
                        else
                        {
                            result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        c_x64_store_result(&emitter, result_displacement);
                    }
                    else if (instruction->opcode == IR_OPCODE_STORE || instruction->opcode == IR_OPCODE_ATOMIC_STORE)
                    {
                        IrValue* place = function->values + instruction->operands[0].value;
                        IrInstruction* definition = function->instructions + place->definition.value;
                        u32 aggregate_parts = 0;
                        bool aggregate = codegen_canonical_integer_aggregate_parts(program, function->values[instruction->operands[1].value].canonical_type,
                                                                                   &aggregate_parts);
                        IrType* stored_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
                        if (!aggregate && stored_type && stored_type->layout.resolved && stored_type->layout.size > 8 &&
                            stored_type->layout.size <= (u64)UINT32_MAX * 8)
                        {
                            aggregate = true;
                            aggregate_parts = (u32)((stored_type->layout.size + 7) / 8);
                        }
                        bool indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                        definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE ||
                                        (definition->opcode == IR_OPCODE_LOCAL && place->alignment > 16);
                        if (codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, function->values[instruction->operands[1].value].canonical_type))
                        {
                            if (instruction->opcode == IR_OPCODE_ATOMIC_STORE || !stored_type || stored_type->layout.size != 16 ||
                                result.abi != CODEGEN_ABI_X86_64_SYSTEM_V ||
                                !codegen_canonical_x64_type_is_f80_x87_shape_cached(f80_cache, program,
                                                                                     function->values[instruction->operands[1].value].canonical_type))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            if (indirect)
                            {
                                c_x64_load(&emitter, 0x85, instruction->operands[0]);
                                if (!codegen_canonical_x64_emit_f80_copy(
                                         &buffer, X64_REGISTER_RBP, c_x64_frame_displacement(&emitter, value_offsets[instruction->operands[1].value]),
                                         X64_REGISTER_RAX, 0, &x87_stack_depth))
                                {
                                    result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                            }
                            else if (!codegen_canonical_x64_emit_f80_copy(
                                         &buffer, X64_REGISTER_RBP, c_x64_frame_displacement(&emitter, value_offsets[instruction->operands[1].value]),
                                         X64_REGISTER_RBP, c_x64_frame_displacement(&emitter, value_offsets[instruction->operands[0].value]), &x87_stack_depth))
                            {
                                result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (instruction->opcode == IR_OPCODE_ATOMIC_STORE && aggregate)
                        {
                            if (!stored_type || stored_type->kind != IR_TYPE_INTEGER || stored_type->layout.size != 16 ||
                                !target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_CX16))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            c_x64_atomic_address(&emitter, instruction->operands[0], indirect);
                            c_x64_load(&emitter, 0x9d, instruction->operands[1]);
                            c_x64_load_high(&emitter, 0x8d, instruction->operands[1]);
                            BusterX86MetadataPhysicalOperand zero_rax_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                            };
                            BusterX86MetadataPhysicalOperand zero_rdx_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 32),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 32),
                            };
                            BusterX86MetadataPhysicalOperand atomic_memory = codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_R10, 128, 0);
                            String8 cx16_features_names[] = {S8("cx16")};
                            if (buffer.error || !codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), zero_rax_operands,
                                                                                       BUSTER_ARRAY_LENGTH(zero_rax_operands)) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), zero_rdx_operands,
                                                                     BUSTER_ARRAY_LENGTH(zero_rdx_operands)))
                            {
                                result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            u32 retry = (u32)buffer.count;
                            BusterX86MetadataPhysicalOperand retry_operand = codegen_canonical_x64_metadata_relative(0, 32);
                            if (!codegen_canonical_x64_metadata_emit_attributes(
                                    &buffer, S8("CMPXCHG16B"), &atomic_memory, 1,
                                    (BusterX86MetadataFeatureInput){.names = cx16_features_names, .count = BUSTER_ARRAY_LENGTH(cx16_features_names)},
                                    (BusterX86MetadataPhysicalAttributes){.lock = true}))
                            {
                                result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            u32 retry_branch_offset = (u32)buffer.count;
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JNZ"), &retry_operand, 1))
                            {
                                result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            s64 retry_delta = (s64)retry - ((s64)retry_branch_offset + 6);
                            if (retry_delta < INT32_MIN || retry_delta > INT32_MAX || !buffer.bytes || retry_branch_offset + 6 > buffer.count)
                            {
                                result.error = retry_delta < INT32_MIN || retry_delta > INT32_MAX ? CODEGEN_ERROR_CAPACITY
                                                                                                    : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            s32 retry_displacement = (s32)retry_delta;
                            memcpy(buffer.bytes + retry_branch_offset + 2, &retry_displacement, sizeof(retry_displacement));
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (aggregate)
                        {
                            if (!stored_type || !stored_type->layout.resolved)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            if (indirect)
                            {
                                c_x64_load(&emitter, 0x95, instruction->operands[0]);
                            }
                            for (u32 part_index = 0; part_index < aggregate_parts; part_index += 1)
                            {
                                u64 part_offset = (u64)part_index * 8;
                                u64 part_size = BUSTER_MIN((u64)8, stored_type->layout.size - part_offset);
                                u64 part_copied = 0;
                                while (part_copied < part_size)
                                {
                                    u64 remaining = part_size - part_copied;
                                    u32 chunk = remaining >= 8 ? 8 : remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;
                                    u64 copy_offset = part_offset + part_copied;
                                    s32 source_displacement = c_x64_frame_displacement(&emitter, value_offsets[instruction->operands[1].value]);
                                    s32 destination_displacement = indirect ? 0 : c_x64_frame_displacement(&emitter, value_offsets[instruction->operands[0].value]);
                                    if (copy_offset > INT32_MAX || (s64)source_displacement + (s64)copy_offset > INT32_MAX ||
                                        (s64)source_displacement + (s64)copy_offset < INT32_MIN ||
                                        (!indirect && ((s64)destination_displacement + (s64)copy_offset > INT32_MAX ||
                                                       (s64)destination_displacement + (s64)copy_offset < INT32_MIN)))
                                    {
                                        result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                        return result;
                                    }
                                    u16 chunk_width = (u16)(chunk * 8);
                                    String8 load_mnemonic = chunk == 1 || chunk == 2 ? S8("MOVZX") : S8("MOV");
                                    u16 load_register_width = chunk <= 4 ? 32 : 64;
                                    BusterX86MetadataPhysicalOperand load_operands[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, load_register_width),
                                        codegen_canonical_x64_metadata_memory(
                                            X64_REGISTER_RBP, chunk_width, (s64)source_displacement + (s64)copy_offset),
                                    };
                                    if (!codegen_canonical_x64_metadata_emit(&buffer, load_mnemonic, load_operands, BUSTER_ARRAY_LENGTH(load_operands)))
                                    {
                                        result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                        return result;
                                    }
                                    BusterX86MetadataPhysicalOperand store_operands[2] = {
                                        indirect ? codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RDX, chunk_width, (s64)copy_offset)
                                                  : codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, chunk_width,
                                                                                           (s64)destination_displacement + (s64)copy_offset),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, chunk_width),
                                    };
                                    if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), store_operands,
                                                                              BUSTER_ARRAY_LENGTH(store_operands)))
                                    {
                                        result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                        return result;
                                    }
                                    part_copied += chunk;
                                }
                            }
                        }
                        else
                        {
                            c_x64_load(&emitter, 0x85, instruction->operands[1]);
                            if (instruction->opcode == IR_OPCODE_ATOMIC_STORE && instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL)
                            {
                                if (!stored_type || !stored_type->layout.resolved ||
                                    (stored_type->layout.size != 1 && stored_type->layout.size != 2 && stored_type->layout.size != 4 &&
                                     stored_type->layout.size != 8))
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                                if (indirect)
                                {
                                    c_x64_load(&emitter, 0x95, instruction->operands[0]);
                                }
                                u16 atomic_store_width = (u16)(stored_type->layout.size * 8);
                                BusterX86MetadataPhysicalOperand atomic_store_operands[2] = {
                                    indirect ? codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RDX, atomic_store_width, 0)
                                              : codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, atomic_store_width,
                                                                                       c_x64_frame_displacement(&emitter, value_offsets[instruction->operands[0].value])),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, atomic_store_width),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("XCHG"), atomic_store_operands,
                                                                           BUSTER_ARRAY_LENGTH(atomic_store_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                instruction_id = instruction->next;
                                continue;
                            }
                            if (indirect)
                            {
                                c_x64_load(&emitter, 0x95, instruction->operands[0]);
                                if (!stored_type || !stored_type->layout.resolved ||
                                    (stored_type->layout.size != 1 && stored_type->layout.size != 2 && stored_type->layout.size != 4 &&
                                     stored_type->layout.size != 8))
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                                u16 store_width = (u16)(stored_type->layout.size * 8);
                                BusterX86MetadataPhysicalOperand store_operands[2] = {
                                    codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RDX, store_width, 0),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, store_width),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), store_operands,
                                                                           BUSTER_ARRAY_LENGTH(store_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                instruction_id = instruction->next;
                                continue;
                            }
                            BusterX86MetadataPhysicalOperand store_operands[2] = {
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64,
                                                                        c_x64_frame_displacement(&emitter, value_offsets[instruction->operands[0].value])),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), store_operands,
                                                                       BUSTER_ARRAY_LENGTH(store_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_ATOMIC_READ_MODIFY_WRITE)
                    {
                        IrValue* place = function->values + instruction->operands[0].value;
                        IrInstruction* definition = function->instructions + place->definition.value;
                        IrType* value_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        bool indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                        definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE ||
                                        (definition->opcode == IR_OPCODE_LOCAL && place->alignment > 16);
                        bool pointer_arithmetic = value_type && value_type->kind == IR_TYPE_POINTER &&
                                                  (instruction->atomic_operation == IR_ATOMIC_ADD || instruction->atomic_operation == IR_ATOMIC_SUBTRACT);
                        if (value_type && value_type->kind == IR_TYPE_INTEGER && value_type->layout.resolved && value_type->layout.size == 16 &&
                            instruction->atomic_operation < IR_ATOMIC_OPERATION_COUNT)
                        {
                            if (!target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_CX16))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            c_x64_atomic_address(&emitter, instruction->operands[0], indirect);
                            s32 atomic_value_displacement = c_x64_frame_displacement(&emitter, value_offsets[instruction->operands[1].value]);
                            codegen_canonical_x64_asm_load(&buffer, X64_REGISTER_R8, X64_REGISTER_RBP, (u32)atomic_value_displacement, 8);
                            codegen_canonical_x64_asm_load(&buffer, X64_REGISTER_R9, X64_REGISTER_RBP, (u32)(atomic_value_displacement + 8), 8);
                            BusterX86MetadataPhysicalOperand zero_low_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                            };
                            BusterX86MetadataPhysicalOperand zero_high_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 32),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 32),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), zero_low_operands,
                                                                      BUSTER_ARRAY_LENGTH(zero_low_operands)) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), zero_high_operands,
                                                                      BUSTER_ARRAY_LENGTH(zero_high_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            u32 retry = (u32)buffer.count;
                            BusterX86MetadataPhysicalOperand copy_low_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RBX, 64),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            };
                            BusterX86MetadataPhysicalOperand copy_high_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), copy_low_operands,
                                                                      BUSTER_ARRAY_LENGTH(copy_low_operands)) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), copy_high_operands,
                                                                      BUSTER_ARRAY_LENGTH(copy_high_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            String8 low_mnemonic = {0};
                            String8 high_mnemonic = {0};
                            switch (instruction->atomic_operation)
                            {
                            case IR_ATOMIC_ADD:
                                low_mnemonic = S8("ADD");
                                high_mnemonic = S8("ADC");
                                break;
                            case IR_ATOMIC_SUBTRACT:
                                low_mnemonic = S8("SUB");
                                high_mnemonic = S8("SBB");
                                break;
                            case IR_ATOMIC_BITWISE_AND:
                                low_mnemonic = S8("AND");
                                high_mnemonic = S8("AND");
                                break;
                            case IR_ATOMIC_BITWISE_OR:
                                low_mnemonic = S8("OR");
                                high_mnemonic = S8("OR");
                                break;
                            case IR_ATOMIC_BITWISE_XOR:
                                low_mnemonic = S8("XOR");
                                high_mnemonic = S8("XOR");
                                break;
                            case IR_ATOMIC_EXCHANGE:
                                low_mnemonic = S8("MOV");
                                high_mnemonic = S8("MOV");
                                break;
                            case IR_ATOMIC_OPERATION_COUNT:
                                break;
                            }
                            BusterX86MetadataPhysicalOperand low_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RBX, 64),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                            };
                            BusterX86MetadataPhysicalOperand high_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 64),
                            };
                            String8 cx16_features_names[] = {S8("cx16")};
                            BusterX86MetadataPhysicalOperand cx16_memory = codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_R10, 128, 0);
                            if (!low_mnemonic.length || !codegen_canonical_x64_metadata_emit(&buffer, low_mnemonic, low_operands,
                                                                                                  BUSTER_ARRAY_LENGTH(low_operands)) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, high_mnemonic, high_operands,
                                                                      BUSTER_ARRAY_LENGTH(high_operands)) ||
                                !codegen_canonical_x64_metadata_emit_attributes(
                                    &buffer, S8("CMPXCHG16B"), &cx16_memory, 1,
                                    (BusterX86MetadataFeatureInput){.names = cx16_features_names, .count = BUSTER_ARRAY_LENGTH(cx16_features_names)},
                                    (BusterX86MetadataPhysicalAttributes){.lock = true}))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            BusterX86MetadataPhysicalOperand retry_operand = codegen_canonical_x64_metadata_relative(0, 32);
                            u32 retry_branch_offset = (u32)buffer.count;
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JNZ"), &retry_operand, 1))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            s64 retry_displacement = (s64)retry - (s64)buffer.count;
                            if (retry_displacement < INT32_MIN || retry_displacement > INT32_MAX || !buffer.bytes)
                            {
                                result.error = retry_displacement < INT32_MIN || retry_displacement > INT32_MAX ? CODEGEN_ERROR_CAPACITY
                                                                                                                 : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            s32 retry_delta = (s32)retry_displacement;
                            memcpy(buffer.bytes + retry_branch_offset + 2, &retry_delta, sizeof(retry_delta));
                            c_x64_store_result(&emitter, result_displacement);
                            c_x64_store_high_rdx(&emitter, result_displacement);
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (!value_type ||
                            (!pointer_arithmetic && value_type->kind != IR_TYPE_INTEGER &&
                             (instruction->atomic_operation != IR_ATOMIC_EXCHANGE ||
                              (value_type->kind != IR_TYPE_BOOLEAN && value_type->kind != IR_TYPE_POINTER))) ||
                            !value_type->layout.resolved ||
                            (value_type->layout.size != 1 && value_type->layout.size != 2 && value_type->layout.size != 4 && value_type->layout.size != 8) ||
                            instruction->atomic_operation >= IR_ATOMIC_OPERATION_COUNT)
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        if (indirect)
                        {
                            c_x64_load(&emitter, 0x95, instruction->operands[0]);
                        }
                        else
                        {
                            BusterX86MetadataPhysicalOperand address_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                codegen_canonical_x64_metadata_memory(
                                    X64_REGISTER_RBP, 64, c_x64_frame_displacement(&emitter, value_offsets[instruction->operands[0].value])),
                            };
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("LEA"), address_operands,
                                                                       BUSTER_ARRAY_LENGTH(address_operands));
                        }
                        c_x64_load(&emitter, 0x8d, instruction->operands[1]);
                        if (buffer.error)
                        {
                            result.error = buffer.error;
                            return result;
                        }
                        codegen_canonical_x64_asm_load(&buffer, X64_REGISTER_RAX, X64_REGISTER_RDX, 0, (u32)value_type->layout.size);
                        if (buffer.error)
                        {
                            result.error = buffer.error;
                            return result;
                        }
                        u16 atomic_width = (u16)(value_type->layout.size * 8);
                        u32 retry_offset = (u32)buffer.count;
                        BusterX86MetadataPhysicalOperand move_old_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, atomic_width),
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, atomic_width),
                        };
                        if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), move_old_operands,
                                                                  BUSTER_ARRAY_LENGTH(move_old_operands)))
                        {
                            result.error = buffer.error;
                            return result;
                        }
                        String8 operation_mnemonic = {0};
                        if (instruction->atomic_operation == IR_ATOMIC_EXCHANGE)
                        {
                            operation_mnemonic = S8("MOV");
                        }
                        else
                        {
                            switch (instruction->atomic_operation)
                            {
                            case IR_ATOMIC_ADD:
                                operation_mnemonic = S8("ADD");
                                break;
                            case IR_ATOMIC_SUBTRACT:
                                operation_mnemonic = S8("SUB");
                                break;
                            case IR_ATOMIC_BITWISE_AND:
                                operation_mnemonic = S8("AND");
                                break;
                            case IR_ATOMIC_BITWISE_OR:
                                operation_mnemonic = S8("OR");
                                break;
                            case IR_ATOMIC_BITWISE_XOR:
                                operation_mnemonic = S8("XOR");
                                break;
                            case IR_ATOMIC_EXCHANGE:
                            case IR_ATOMIC_OPERATION_COUNT:
                                break;
                            }
                        }
                        BusterX86MetadataPhysicalOperand operation_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, atomic_width),
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, atomic_width),
                        };
                        if (!operation_mnemonic.length ||
                            !codegen_canonical_x64_metadata_emit(&buffer, operation_mnemonic, operation_operands,
                                                                  BUSTER_ARRAY_LENGTH(operation_operands)) ||
                            !codegen_canonical_x64_metadata_atomic_register_memory(&buffer, S8("CMPXCHG"), X64_REGISTER_RDX,
                                                                                      X64_REGISTER_R8, atomic_width))
                        {
                            result.error = buffer.error;
                            return result;
                        }
                        BusterX86MetadataPhysicalOperand retry_operand = codegen_canonical_x64_metadata_relative(0, 32);
                        u32 retry_branch_offset = (u32)buffer.count;
                        if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JNZ"), &retry_operand, 1))
                        {
                            result.error = buffer.error;
                            return result;
                        }
                        s64 retry_displacement = (s64)retry_offset - (s64)buffer.count;
                        if (retry_displacement < INT32_MIN || retry_displacement > INT32_MAX || !buffer.bytes)
                        {
                            result.error = retry_displacement < INT32_MIN || retry_displacement > INT32_MAX ? CODEGEN_ERROR_CAPACITY
                                                                                                             : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        s32 retry_delta = (s32)retry_displacement;
                        memcpy(buffer.bytes + retry_branch_offset + 2, &retry_delta, sizeof(retry_delta));
                        c_x64_store_result(&emitter, result_displacement);
                    }
                    else if (instruction->opcode == IR_OPCODE_ATOMIC_COMPARE_EXCHANGE)
                    {
                        IrValue* place = function->values + instruction->operands[0].value;
                        IrInstruction* definition = function->instructions + place->definition.value;
                        IrType* value_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        bool indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                        definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE ||
                                        (definition->opcode == IR_OPCODE_LOCAL && place->alignment > 16);
                        if (value_type && value_type->kind == IR_TYPE_INTEGER && value_type->layout.resolved && value_type->layout.size == 16)
                        {
                            if (!target_cpu_feature_has(target, TARGET_CPU_FEATURE_X86_CX16))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            c_x64_atomic_address(&emitter, instruction->operands[0], indirect);
                            c_x64_load(&emitter, 0x85, instruction->operands[1]);
                            c_x64_load_high(&emitter, 0x95, instruction->operands[1]);
                            c_x64_load(&emitter, 0x9d, instruction->operands[2]);
                            c_x64_load_high(&emitter, 0x8d, instruction->operands[2]);
                            String8 cx16_features_names[] = {S8("cx16")};
                            BusterX86MetadataPhysicalOperand cx16_memory = codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_R10, 128, 0);
                            if (buffer.error ||
                                !codegen_canonical_x64_metadata_emit_attributes(
                                    &buffer, S8("CMPXCHG16B"), &cx16_memory, 1,
                                    (BusterX86MetadataFeatureInput){.names = cx16_features_names, .count = BUSTER_ARRAY_LENGTH(cx16_features_names)},
                                    (BusterX86MetadataPhysicalAttributes){.lock = true}))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            c_x64_store_result(&emitter, result_displacement);
                            c_x64_store_high_rdx(&emitter, result_displacement);
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (!value_type || (value_type->kind != IR_TYPE_INTEGER && value_type->kind != IR_TYPE_POINTER) || !value_type->layout.resolved ||
                            (value_type->layout.size != 1 && value_type->layout.size != 2 && value_type->layout.size != 4 && value_type->layout.size != 8))
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        if (indirect)
                        {
                            c_x64_load(&emitter, 0x95, instruction->operands[0]);
                        }
                        else
                        {
                            BusterX86MetadataPhysicalOperand address_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                codegen_canonical_x64_metadata_memory(
                                    X64_REGISTER_RBP, 64, c_x64_frame_displacement(&emitter, value_offsets[instruction->operands[0].value])),
                            };
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("LEA"), address_operands,
                                                                       BUSTER_ARRAY_LENGTH(address_operands));
                        }
                        c_x64_load(&emitter, 0x85, instruction->operands[1]);
                        c_x64_load(&emitter, 0x8d, instruction->operands[2]);
                        if (buffer.error)
                        {
                            result.error = buffer.error;
                            return result;
                        }
                        u16 atomic_width = (u16)(value_type->layout.size * 8);
                        if (!codegen_canonical_x64_metadata_atomic_register_memory(&buffer, S8("CMPXCHG"), X64_REGISTER_RDX,
                                                                                     X64_REGISTER_RCX, atomic_width))
                        {
                            result.error = buffer.error;
                            return result;
                        }
                        if (value_type->layout.size == 1 || value_type->layout.size == 2)
                        {
                            BusterX86MetadataPhysicalOperand widen_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, atomic_width),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOVZX"), widen_operands,
                                                                      BUSTER_ARRAY_LENGTH(widen_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                        c_x64_store_result(&emitter, result_displacement);
                    }
                    else if (instruction->opcode == IR_OPCODE_ATOMIC_FENCE)
                    {
                        if (!instruction->atomic_signal_fence && instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL)
                        {
                            String8 fence_features[] = {S8("sse2")};
                            if (!codegen_canonical_x64_metadata_emit_features(
                                    &buffer, S8("MFENCE"), 0, 0,
                                    (BusterX86MetadataFeatureInput){.names = fence_features, .count = BUSTER_ARRAY_LENGTH(fence_features)}))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_CLEAR_INSTRUCTION_CACHE)
                    {
                    }
                    else if (instruction->opcode == IR_OPCODE_CONSTANT_INTEGER || instruction->opcode == IR_OPCODE_CONSTANT_FLOAT)
                    {
                        IrType* constant_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        if (instruction->opcode == IR_OPCODE_CONSTANT_FLOAT && constant_type && constant_type->kind == IR_TYPE_FLOAT && constant_type->bit_width == 80)
                        {
                            if (!codegen_canonical_x64_type_is_f80(constant_type) || result.abi != CODEGEN_ABI_X86_64_SYSTEM_V ||
                                instruction->immediate_count != 2 ||
                                (instruction->immediates[1] & ~UINT64_C(0xffff)) ||
                                !codegen_canonical_x64_store_f80_constant(&buffer, result_displacement, instruction->immediates[0],
                                                                           (u16)instruction->immediates[1]))
                            {
                                result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        u64 immediate = instruction->immediates[0];
                        if (instruction->opcode == IR_OPCODE_CONSTANT_INTEGER && instruction->immediate_is_negative)
                        {
                            immediate = 0 - immediate;
                        }
                        u16 constant_width = codegen_canonical_register_is_64_bit(program, instruction->canonical_type) ? 64 : 32;
                        BusterX86MetadataPhysicalOperand constant_value =
                            constant_width == 32 && immediate > UINT32_MAX
                                ? codegen_canonical_x64_metadata_immediate((s64)(s32)(u32)immediate, constant_width)
                                : codegen_canonical_x64_metadata_unsigned_immediate(immediate, constant_width);
                        BusterX86MetadataPhysicalOperand constant_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, constant_width),
                            constant_value,
                        };
                        if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), constant_operands,
                                                                   BUSTER_ARRAY_LENGTH(constant_operands)))
                        {
                            result.error = buffer.error;
                            return result;
                        }
                        c_x64_store_result(&emitter, result_displacement);
                        if (instruction->opcode == IR_OPCODE_CONSTANT_INTEGER && constant_type && constant_type->kind == IR_TYPE_INTEGER &&
                            constant_type->bit_width == 128)
                        {
                            BusterX86MetadataPhysicalOperand high_constant_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                codegen_canonical_x64_metadata_unsigned_immediate(
                                    instruction->immediate_count > 1 ? instruction->immediates[1]
                                                                      : instruction->immediate_is_negative ? UINT64_MAX : 0,
                                    64),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), high_constant_operands,
                                                                       BUSTER_ARRAY_LENGTH(high_constant_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            c_x64_store_high_rdx(&emitter, result_displacement);
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_VA_START)
                    {
                        if (!canonical_variadic)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        u32 va_list_component_count = codegen_canonical_va_list_component_count(program, instruction->canonical_type);
                        if (!va_list_component_count)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        u32 gp_count = 0;
                        u32 fp_count = 0;
                        u32 stack_parts = 0;
                        for (u32 parameter_index = 0; parameter_index < canonical_function_type->parameter_count; parameter_index += 1)
                        {
                            IrTypeId parameter_type_id = canonical_function_type->parameter_types[parameter_index];
                            IrType* parameter_type = ir_type_from_id(&program->types, parameter_type_id);
                            if (codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, parameter_type_id))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            u32 parts = 1;
                            bool aggregate = codegen_canonical_integer_aggregate_parts(program, parameter_type_id, &parts);
                            if (parameter_type && parameter_type->kind == IR_TYPE_FLOAT)
                            {
                                if (fp_count < 8)
                                {
                                    fp_count += 1;
                                }
                                else
                                {
                                    stack_parts += 1;
                                }
                            }
                            else if (aggregate ? gp_count + parts <= 6 : gp_count < 6)
                            {
                                gp_count += parts;
                            }
                            else
                            {
                                stack_parts += parts;
                            }
                        }
                        if (result.abi == CODEGEN_ABI_X86_64_SYSTEM_V)
                        {
                            u64 offsets = (u64)(gp_count * 8) | ((u64)(48 + fp_count * 16) << 32);
                            BusterX86MetadataPhysicalOperand va_offsets_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                codegen_canonical_x64_metadata_unsigned_immediate(offsets, 64),
                            };
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), va_offsets_operands,
                                                                      BUSTER_ARRAY_LENGTH(va_offsets_operands));
                            c_x64_store_result(&emitter, result_displacement);
                            BusterX86MetadataPhysicalOperand va_overflow_address_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, 16 + (s64)stack_parts * 8),
                            };
                            BusterX86MetadataPhysicalOperand va_overflow_store_operands[2] = {
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, (s64)result_displacement + 8),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            };
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("LEA"), va_overflow_address_operands,
                                                                      BUSTER_ARRAY_LENGTH(va_overflow_address_operands));
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), va_overflow_store_operands,
                                                                      BUSTER_ARRAY_LENGTH(va_overflow_store_operands));
                            BusterX86MetadataPhysicalOperand va_register_save_address_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                codegen_canonical_x64_metadata_memory(
                                    X64_REGISTER_RBP, 64,
                                    codegen_canonical_x64_rebase_frame_displacement(&buffer, canonical_va_save_displacement,
                                                                                     canonical_x64_frame_base_offset)),
                            };
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("LEA"), va_register_save_address_operands,
                                                                      BUSTER_ARRAY_LENGTH(va_register_save_address_operands));
                        }
                        else
                        {
                            BusterX86MetadataPhysicalOperand va_windows_address_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                codegen_canonical_x64_metadata_memory(
                                    X64_REGISTER_RBP, 64,
                                    codegen_canonical_x64_rebase_frame_displacement(
                                        &buffer, 16 + (s64)(canonical_function_type->parameter_count + (windows_indirect_return ? 1 : 0)) * 8,
                                        canonical_x64_frame_base_offset)),
                            };
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("LEA"), va_windows_address_operands,
                                                                      BUSTER_ARRAY_LENGTH(va_windows_address_operands));
                        }
                        BusterX86MetadataPhysicalOperand va_save_area_operands[2] = {
                            codegen_canonical_x64_metadata_memory(
                                X64_REGISTER_RBP, 64, (s64)result_displacement + (result.abi == CODEGEN_ABI_X86_64_SYSTEM_V ? 16 : 0)),
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                        };
                        BusterX86MetadataPhysicalOperand va_zero_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                        };
                        (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), va_save_area_operands,
                                                                  BUSTER_ARRAY_LENGTH(va_save_area_operands));
                        (void)codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), va_zero_operands,
                                                                  BUSTER_ARRAY_LENGTH(va_zero_operands));
                        u32 zero_start = result.abi == CODEGEN_ABI_X86_64_SYSTEM_V ? 24 : 8;
                        for (u32 offset = zero_start; offset < va_list_component_count * 8; offset += 8)
                        {
                            BusterX86MetadataPhysicalOperand va_zero_store_operands[2] = {
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, (s64)result_displacement + offset),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            };
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), va_zero_store_operands,
                                                                      BUSTER_ARRAY_LENGTH(va_zero_store_operands));
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_VA_COPY)
                    {
                        u32 va_list_component_count = codegen_canonical_va_list_component_count(program, instruction->canonical_type);
                        if (!va_list_component_count)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        c_x64_load(&emitter, 0x85, instruction->operands[0]);
                        for (u32 component = 0; component < va_list_component_count; component += 1)
                        {
                            BusterX86MetadataPhysicalOperand va_copy_load_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RAX, 64, (s64)component * 8),
                            };
                            BusterX86MetadataPhysicalOperand va_copy_store_operands[2] = {
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, (s64)result_displacement + component * 8),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                            };
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), va_copy_load_operands,
                                                                      BUSTER_ARRAY_LENGTH(va_copy_load_operands));
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), va_copy_store_operands,
                                                                      BUSTER_ARRAY_LENGTH(va_copy_store_operands));
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_VA_END)
                    {
                        // va_end is a semantic lifetime marker.  The native
                        // representations used here do not require a
                        // destructive operation, and writing a fixed fourth
                        // word would exceed pointer-sized Windows va_list
                        // objects.
                    }
                    else if (instruction->opcode == IR_OPCODE_VA_ARG)
                    {
                        IrType* value_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        if (codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, instruction->canonical_type))
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        u32 integer_parts = 0;
                        bool aggregate = codegen_canonical_integer_aggregate_parts(program, instruction->canonical_type, &integer_parts);
                        bool floating = value_type && value_type->kind == IR_TYPE_FLOAT;
                        CodegenCanonicalAbiValue aggregate_abi = codegen_canonical_aggregate_abi(program, instruction->canonical_type, result.abi, false, true);
                        if (!value_type || !value_type->layout.size || value_type->layout.size > 16 ||
                            (!aggregate && !floating && value_type->kind != IR_TYPE_INTEGER && value_type->kind != IR_TYPE_BOOLEAN &&
                             value_type->kind != IR_TYPE_POINTER))
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        c_x64_load(&emitter, 0x85, instruction->operands[0]);
                        bool split_system_v_aggregate =
                            result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && aggregate_abi.part_count && !aggregate_abi.memory && !aggregate_abi.indirect;
                        u32 split_integer_count = 0;
                        u32 split_float_count = 0;
                        if (split_system_v_aggregate)
                        {
                            for (u32 part = 0; part < aggregate_abi.part_count; part += 1)
                            {
                                if (codegen_canonical_abi_part_is_float(aggregate_abi.parts[part].abi_class))
                                {
                                    split_float_count += 1;
                                }
                                else
                                {
                                    split_integer_count += 1;
                                }
                            }
                        }
                        if (split_system_v_aggregate && split_float_count)
                        {
                            u32 overflow_branch_offsets[2] = {0};
                            u32 overflow_branch_sizes[2] = {0};
                            u32 overflow_branch_count = 0;
                            if (split_integer_count)
                            {
                                BusterX86MetadataPhysicalOperand gp_offset_load_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 32),
                                    codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RAX, 32, 0),
                                };
                                BusterX86MetadataPhysicalOperand gp_offset_compare_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 32),
                                    codegen_canonical_x64_metadata_immediate(48 - split_integer_count * 8, 32),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), gp_offset_load_operands,
                                                                           BUSTER_ARRAY_LENGTH(gp_offset_load_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("CMP"), gp_offset_compare_operands,
                                                                           BUSTER_ARRAY_LENGTH(gp_offset_compare_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                u32 branch_offset = (u32)buffer.count;
                                BusterX86MetadataPhysicalOperand overflow_branch_operand = codegen_canonical_x64_metadata_relative(0, 32);
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JNBE"), &overflow_branch_operand, 1))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                u32 branch_size = (u32)buffer.count - branch_offset;
                                if (branch_size < 4 || overflow_branch_count >= BUSTER_ARRAY_LENGTH(overflow_branch_offsets))
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                                overflow_branch_offsets[overflow_branch_count] = branch_offset;
                                overflow_branch_sizes[overflow_branch_count] = branch_size;
                                overflow_branch_count += 1;
                            }
                            BusterX86MetadataPhysicalOperand fp_offset_load_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 32),
                                codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RAX, 32, 4),
                            };
                            BusterX86MetadataPhysicalOperand fp_offset_compare_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 32),
                                codegen_canonical_x64_metadata_immediate(176 - split_float_count * 16, 32),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), fp_offset_load_operands,
                                                                       BUSTER_ARRAY_LENGTH(fp_offset_load_operands)) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("CMP"), fp_offset_compare_operands,
                                                                       BUSTER_ARRAY_LENGTH(fp_offset_compare_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            u32 fp_branch_offset = (u32)buffer.count;
                            BusterX86MetadataPhysicalOperand fp_overflow_branch_operand = codegen_canonical_x64_metadata_relative(0, 32);
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JNBE"), &fp_overflow_branch_operand, 1))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            u32 fp_branch_size = (u32)buffer.count - fp_branch_offset;
                            if (fp_branch_size < 4 || overflow_branch_count >= BUSTER_ARRAY_LENGTH(overflow_branch_offsets))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            overflow_branch_offsets[overflow_branch_count] = fp_branch_offset;
                            overflow_branch_sizes[overflow_branch_count] = fp_branch_size;
                            overflow_branch_count += 1;

                            BusterX86MetadataPhysicalOperand register_save_area_load_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RAX, 64, 16),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), register_save_area_load_operands,
                                                                       BUSTER_ARRAY_LENGTH(register_save_area_load_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            u32 integer_part = 0;
                            u32 float_part = 0;
                            for (u32 part = 0; part < aggregate_abi.part_count; part += 1)
                            {
                                bool part_float = codegen_canonical_abi_part_is_float(aggregate_abi.parts[part].abi_class);
                                u32 part_offset = part_float ? float_part++ * 16 : integer_part++ * 8;
                                X64Register index_register = part_float ? X64_REGISTER_R8 : X64_REGISTER_RCX;
                                BusterX86MetadataPhysicalOperand part_memory =
                                    codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RDX, 64, part_offset);
                                part_memory.memory.has_index = true;
                                part_memory.memory.index = codegen_canonical_x64_metadata_gpr(index_register, 64).reg;
                                part_memory.memory.scale = 1;
                                BusterX86MetadataPhysicalOperand part_load_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 64),
                                    part_memory,
                                };
                                BusterX86MetadataPhysicalOperand part_store_operands[2] = {
                                    codegen_canonical_x64_metadata_memory(
                                        X64_REGISTER_RBP, 64,
                                        (s64)result_displacement + (s64)aggregate_abi.parts[part].value_offset),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 64),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), part_load_operands,
                                                                           BUSTER_ARRAY_LENGTH(part_load_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), part_store_operands,
                                                                           BUSTER_ARRAY_LENGTH(part_store_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            if (split_integer_count)
                            {
                                BusterX86MetadataPhysicalOperand gp_offset_advance_operands[2] = {
                                    codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RAX, 32, 0),
                                    codegen_canonical_x64_metadata_immediate(split_integer_count * 8, 8),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("ADD"), gp_offset_advance_operands,
                                                                           BUSTER_ARRAY_LENGTH(gp_offset_advance_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            BusterX86MetadataPhysicalOperand fp_offset_advance_operands[2] = {
                                codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RAX, 32, 4),
                                codegen_canonical_x64_metadata_immediate(split_float_count * 16, 8),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("ADD"), fp_offset_advance_operands,
                                                                       BUSTER_ARRAY_LENGTH(fp_offset_advance_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            u32 end_branch_offset = (u32)buffer.count;
                            BusterX86MetadataPhysicalOperand end_branch_operand = codegen_canonical_x64_metadata_relative(0, 32);
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JMP"), &end_branch_operand, 1))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            u32 end_branch_size = (u32)buffer.count - end_branch_offset;
                            if (end_branch_size < 4)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            u32 overflow_offset = (u32)buffer.count;
                            BusterX86MetadataPhysicalOperand overflow_area_load_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RAX, 64, 8),
                            };
                            u32 stack_size = (u32)((value_type->layout.size + 7) & ~(u64)7);
                            BusterX86MetadataPhysicalOperand overflow_area_advance_operands[2] = {
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RAX, 64, 8),
                                codegen_canonical_x64_metadata_immediate((s64)stack_size, 32),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), overflow_area_load_operands,
                                                                       BUSTER_ARRAY_LENGTH(overflow_area_load_operands)) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("ADD"), overflow_area_advance_operands,
                                                                       BUSTER_ARRAY_LENGTH(overflow_area_advance_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            for (u32 part = 0; part < aggregate_abi.part_count; part += 1)
                            {
                                u32 part_offset = part * 8;
                                BusterX86MetadataPhysicalOperand overflow_part_load_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 64),
                                    codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RDX, 64, part_offset),
                                };
                                BusterX86MetadataPhysicalOperand overflow_part_store_operands[2] = {
                                    codegen_canonical_x64_metadata_memory(
                                        X64_REGISTER_RBP, 64,
                                        (s64)result_displacement + (s64)aggregate_abi.parts[part].value_offset),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 64),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), overflow_part_load_operands,
                                                                           BUSTER_ARRAY_LENGTH(overflow_part_load_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), overflow_part_store_operands,
                                                                           BUSTER_ARRAY_LENGTH(overflow_part_store_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            u32 end_offset = (u32)buffer.count;
                            for (u32 branch = 0; branch < overflow_branch_count; branch += 1)
                            {
                                u32 branch_offset = overflow_branch_offsets[branch];
                                u32 branch_size = overflow_branch_sizes[branch];
                                u32 field_offset = branch_offset + branch_size - 4;
                                s64 delta = (s64)overflow_offset - ((s64)branch_offset + branch_size);
                                if (delta < INT32_MIN || delta > INT32_MAX || field_offset + sizeof(s32) > buffer.count)
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                s32 displacement = (s32)delta;
                                memcpy(buffer.bytes + field_offset, &displacement, sizeof(displacement));
                            }
                            u32 end_field_offset = end_branch_offset + end_branch_size - 4;
                            s64 end_delta = (s64)end_offset - ((s64)end_branch_offset + end_branch_size);
                            if (end_delta < INT32_MIN || end_delta > INT32_MAX || end_field_offset + sizeof(s32) > buffer.count)
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                            s32 end_displacement = (s32)end_delta;
                            memcpy(buffer.bytes + end_field_offset, &end_displacement, sizeof(end_displacement));
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (result.abi == CODEGEN_ABI_X86_64_WINDOWS)
                        {
                            BusterX86MetadataPhysicalOperand windows_va_load_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RAX, 64, 0),
                            };
                            BusterX86MetadataPhysicalOperand windows_va_advance_operands[2] = {
                                codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RAX, 64, 0),
                                codegen_canonical_x64_metadata_immediate(8, 8),
                            };
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), windows_va_load_operands,
                                                                      BUSTER_ARRAY_LENGTH(windows_va_load_operands));
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("ADD"), windows_va_advance_operands,
                                                                      BUSTER_ARRAY_LENGTH(windows_va_advance_operands));
                            if (aggregate_abi.indirect)
                            {
                                BusterX86MetadataPhysicalOperand windows_va_indirect_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                    codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RDX, 64, 0),
                                };
                                (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), windows_va_indirect_operands,
                                                                          BUSTER_ARRAY_LENGTH(windows_va_indirect_operands));
                            }
                            for (u32 part = 0; part < (aggregate ? integer_parts : 1); part += 1)
                            {
                                BusterX86MetadataPhysicalOperand windows_va_part_load_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                    codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RDX, 64, (s64)part * 8),
                                };
                                BusterX86MetadataPhysicalOperand windows_va_part_store_operands[2] = {
                                    codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, (s64)result_displacement + (s64)part * 8),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                };
                                (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), windows_va_part_load_operands,
                                                                          BUSTER_ARRAY_LENGTH(windows_va_part_load_operands));
                                (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), windows_va_part_store_operands,
                                                                          BUSTER_ARRAY_LENGTH(windows_va_part_store_operands));
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        else
                        {
                            u32 descriptor_offset = floating ? 4 : 0;
                            u32 part_count = aggregate ? integer_parts : 1;
                            u32 increment = floating ? 16 : part_count * 8;
                            u32 limit = floating ? 176 - increment : 48 - increment;
                            BusterX86MetadataPhysicalOperand descriptor_load_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 32),
                                codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RAX, 32, descriptor_offset),
                            };
                            BusterX86MetadataPhysicalOperand descriptor_compare_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 32),
                                codegen_canonical_x64_metadata_immediate(limit, 32),
                            };
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), descriptor_load_operands,
                                                                      BUSTER_ARRAY_LENGTH(descriptor_load_operands));
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("CMP"), descriptor_compare_operands,
                                                                      BUSTER_ARRAY_LENGTH(descriptor_compare_operands));
                            u32 overflow_branch_offset = (u32)buffer.count;
                            BusterX86MetadataPhysicalOperand overflow_branch = codegen_canonical_x64_metadata_relative(0, 32);
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("JNBE"), &overflow_branch, 1);
                            BusterX86MetadataPhysicalOperand register_save_load_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RAX, 64, 16),
                            };
                            BusterX86MetadataPhysicalOperand register_save_add_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                            };
                            BusterX86MetadataPhysicalOperand descriptor_advance_operands[2] = {
                                codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RAX, 32, descriptor_offset),
                                codegen_canonical_x64_metadata_immediate(increment, 8),
                            };
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), register_save_load_operands,
                                                                      BUSTER_ARRAY_LENGTH(register_save_load_operands));
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("ADD"), register_save_add_operands,
                                                                      BUSTER_ARRAY_LENGTH(register_save_add_operands));
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("ADD"), descriptor_advance_operands,
                                                                      BUSTER_ARRAY_LENGTH(descriptor_advance_operands));
                            u32 end_branch_offset = (u32)buffer.count;
                            BusterX86MetadataPhysicalOperand end_branch = codegen_canonical_x64_metadata_relative(0, 32);
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("JMP"), &end_branch, 1);
                            u32 overflow_offset = (u32)buffer.count;
                            BusterX86MetadataPhysicalOperand overflow_load_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RAX, 64, 8),
                            };
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), overflow_load_operands,
                                                                      BUSTER_ARRAY_LENGTH(overflow_load_operands));
                            // The caller placed an over-aligned argument at its
                            // own alignment, so the overflow cursor has to skip
                            // the same padding before reading one back. Only
                            // sixteen is reachable: a wider type is refused
                            // above for being larger than two eightbytes.
                            if (codegen_canonical_x64_stack_argument_alignment(value_type) > 8)
                            {
                                BusterX86MetadataPhysicalOperand overflow_align_add_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                    codegen_canonical_x64_metadata_immediate(15, 8),
                                };
                                BusterX86MetadataPhysicalOperand overflow_align_and_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                    codegen_canonical_x64_metadata_immediate(-16, 8),
                                };
                                BusterX86MetadataPhysicalOperand overflow_cursor_store_operands[2] = {
                                    codegen_canonical_x64_metadata_memory(X64_REGISTER_RAX, 64, 8),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                };
                                (void)codegen_canonical_x64_metadata_emit(&buffer, S8("ADD"), overflow_align_add_operands,
                                                                          BUSTER_ARRAY_LENGTH(overflow_align_add_operands));
                                (void)codegen_canonical_x64_metadata_emit(&buffer, S8("AND"), overflow_align_and_operands,
                                                                          BUSTER_ARRAY_LENGTH(overflow_align_and_operands));
                                (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), overflow_cursor_store_operands,
                                                                          BUSTER_ARRAY_LENGTH(overflow_cursor_store_operands));
                            }
                            u32 stack_size = (u32)((value_type->layout.size + 7) & ~(u64)7);
                            BusterX86MetadataPhysicalOperand overflow_advance_operands[2] = {
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RAX, 64, 8),
                                codegen_canonical_x64_metadata_immediate(stack_size, 32),
                            };
                            (void)codegen_canonical_x64_metadata_emit(&buffer, S8("ADD"), overflow_advance_operands,
                                                                      BUSTER_ARRAY_LENGTH(overflow_advance_operands));
                            u32 common_copy_offset = (u32)buffer.count;
                            for (u32 part = 0; part < (aggregate ? integer_parts : 1); part += 1)
                            {
                                BusterX86MetadataPhysicalOperand register_part_load_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                    codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RDX, 64, (s64)part * 8),
                                };
                                BusterX86MetadataPhysicalOperand register_part_store_operands[2] = {
                                    codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, (s64)result_displacement + (s64)part * 8),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                };
                                (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), register_part_load_operands,
                                                                          BUSTER_ARRAY_LENGTH(register_part_load_operands));
                                (void)codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), register_part_store_operands,
                                                                          BUSTER_ARRAY_LENGTH(register_part_store_operands));
                            }
                            if (buffer.error == CODEGEN_ERROR_NONE && overflow_branch_offset + 6 <= buffer.count && end_branch_offset + 5 <= buffer.count)
                            {
                                s32 overflow_delta = (s32)(overflow_offset - (overflow_branch_offset + 6));
                                s32 end_delta = (s32)(common_copy_offset - (end_branch_offset + 5));
                                memcpy(buffer.bytes + overflow_branch_offset + 2, &overflow_delta, sizeof(overflow_delta));
                                memcpy(buffer.bytes + end_branch_offset + 1, &end_delta, sizeof(end_delta));
                            }
                            else if (buffer.error == CODEGEN_ERROR_NONE)
                            {
                                buffer.error = CODEGEN_ERROR_CAPACITY;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        for (u32 part = 0; part < (aggregate ? integer_parts : 1); part += 1)
                        {
                            BusterX86MetadataPhysicalOperand load_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 64),
                                codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RDX, 64, (s64)part * 8),
                            };
                            BusterX86MetadataPhysicalOperand store_operands[2] = {
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64,
                                                                      (s64)result_displacement + (s64)part * 8),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 64),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), load_operands,
                                                                       BUSTER_ARRAY_LENGTH(load_operands)) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), store_operands,
                                                                       BUSTER_ARRAY_LENGTH(store_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_CALL)
                    {
                        CodegenCanonicalCallLayout call_layout = {0};
                        u32 call_instruction_index = (u32)(instruction - function->instructions);
                        if (call_layout_cache && call_layout_cache[call_instruction_index])
                        {
                            call_layout = *call_layout_cache[call_instruction_index];
                        }
                        else
                        {
                            CodegenError call_error = codegen_canonical_x64_call_layout_cached(arena, program, f80_cache, function, instruction,
                                                                                                  result.abi, target, &call_layout);
                            if (call_error != CODEGEN_ERROR_NONE)
                            {
                                result.error = call_error;
                                return result;
                            }
                        }
                        static u8 const system_v[] = {
                            7, 6, 2, 1, 8, 9,
                        };
                        static u8 const windows[] = {
                            1,
                            2,
                            8,
                            9,
                        };
                        u8 const* registers = result.abi == CODEGEN_ABI_X86_64_WINDOWS ? windows : system_v;
                        u32 register_count = result.abi == CODEGEN_ABI_X86_64_WINDOWS ? BUSTER_ARRAY_LENGTH(windows) : BUSTER_ARRAY_LENGTH(system_v);
                        u32 argument_count = call_layout.argument_count;
                        CodegenCanonicalCallArgument* arguments = call_layout.arguments;
                        CodegenCanonicalAbiValue call_return_abi = call_layout.return_abi;
                        bool call_windows_indirect_return = call_layout.windows_indirect_return;
                        bool call_x64_indirect_return = call_layout.indirect_return;
                        u32 simulated_float_registers = call_layout.simulated_float_registers;
                        // An argument wanting more than the sixteen bytes the
                        // stack pointer is already worth cannot be reached by
                        // pushing: where the pushes leave it depends on where
                        // the stack happened to be. Such a call moves the stack
                        // pointer down to the alignment the area needs instead,
                        // writes each argument at its own offset within it, and
                        // puts the stack pointer back from a frame slot after,
                        // which is the one restore an `and` cannot undo and a
                        // dynamically grown stack does not invalidate.
                        bool system_v_aligned_area = result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && call_layout.stack_part_count &&
                                                     call_layout.stack_alignment > CODEGEN_X64_STACK_ALIGNMENT;
                        if (system_v_aligned_area && !x64_stack_save_offset)
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                            return result;
                        }
                        bool stack_padding = call_layout.stack_padding && !system_v_aligned_area;
                        if (stack_padding)
                        {
                            BusterX86MetadataPhysicalOperand stack_padding_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSP, 64),
                                codegen_canonical_x64_metadata_immediate(8, 8),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("SUB"), stack_padding_operands,
                                                                       BUSTER_ARRAY_LENGTH(stack_padding_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                        if (system_v_aligned_area)
                        {
                            BusterX86MetadataPhysicalOperand stack_save_operands[2] = {
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, c_x64_frame_displacement(&emitter, x64_stack_save_offset)),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSP, 64),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), stack_save_operands,
                                                                       BUSTER_ARRAY_LENGTH(stack_save_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            codegen_canonical_x64_adjust_stack(&buffer, call_layout.stack_part_count * 8, true);
                            BusterX86MetadataPhysicalOperand stack_align_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSP, 64),
                                codegen_canonical_x64_metadata_immediate(-(s64)call_layout.stack_alignment, 32),
                            };
                            if (buffer.error != CODEGEN_ERROR_NONE ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("AND"), stack_align_operands,
                                                                       BUSTER_ARRAY_LENGTH(stack_align_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            for (u32 argument_index = 0; argument_index < argument_count; argument_index += 1)
                            {
                                CodegenCanonicalCallArgument* call_argument = arguments + argument_index;
                                if (!call_argument->on_stack)
                                {
                                    continue;
                                }
                                u32 argument_offset = value_offsets[instruction->operands[argument_index + 1].value];
                                // No rounding here: a System V stack argument
                                // is already at an offset respecting its own
                                // alignment inside an area the call aligned to
                                // the widest of them, so the slot address is
                                // whatever the stack pointer already is.
                                if (!codegen_canonical_x64_copy_frame_to_rsp(&buffer, argument_offset, canonical_x64_frame_base_offset,
                                                                             call_argument->stack_offset, call_argument->stack_part_count * 8,
                                                                             CODEGEN_X64_STACK_ALIGNMENT))
                                {
                                    result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                        }
                        bool windows_dynamic_call = windows_dynamic_stack && result.abi == CODEGEN_ABI_X86_64_WINDOWS;
                        if (windows_dynamic_call)
                        {
                            codegen_canonical_x64_adjust_stack(&buffer, call_layout.windows_stack_size, true);
                            if (buffer.error != CODEGEN_ERROR_NONE)
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                        if (result.abi == CODEGEN_ABI_X86_64_WINDOWS)
                        {
                            for (u32 argument_index = 0; argument_index < argument_count; argument_index += 1)
                            {
                                CodegenCanonicalCallArgument* call_argument = arguments + argument_index;
                                if (call_argument->windows_indirect &&
                                    (value_offsets[instruction->operands[argument_index + 1].value] > UINT32_MAX - call_argument->copy_size ||
                                     !codegen_canonical_x64_copy_frame_to_rsp(&buffer, value_offsets[instruction->operands[argument_index + 1].value],
                                                                               canonical_x64_frame_base_offset, call_argument->copy_offset,
                                                                               call_argument->copy_size, call_argument->copy_alignment)))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                        }
                        // The pushes build the area downward, so an argument is
                        // preceded by whatever padding sits above it: emit that
                        // first and the one below it lands on its own offset.
                        u32 stack_cursor = system_v_aligned_area ? 0 : call_layout.stack_part_count * 8;
                        for (u32 argument_reverse_index = argument_count; argument_reverse_index > 0 && !system_v_aligned_area; argument_reverse_index -= 1)
                        {
                            u32 array_index = argument_reverse_index - 1;
                            if (!arguments[array_index].on_stack || result.abi != CODEGEN_ABI_X86_64_SYSTEM_V)
                            {
                                continue;
                            }
                            IrValueId argument = instruction->operands[argument_reverse_index];
                            for (u32 padding = arguments[array_index].stack_offset + arguments[array_index].stack_part_count * 8; padding < stack_cursor;
                                 padding += 8)
                            {
                                BusterX86MetadataPhysicalOperand push_padding_operand =
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64);
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("PUSH"), &push_padding_operand, 1))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            for (u32 part_index = arguments[array_index].stack_part_count; part_index > 0; part_index -= 1)
                            {
                                BusterX86MetadataPhysicalOperand push_argument_operand = codegen_canonical_x64_metadata_memory(
                                    X64_REGISTER_RBP, 64,
                                    c_x64_frame_displacement(&emitter, value_offsets[argument.value]) + (s32)((part_index - 1) * 8));
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("PUSH"), &push_argument_operand, 1))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            stack_cursor = arguments[array_index].stack_offset;
                        }
                        if (result.abi == CODEGEN_ABI_X86_64_WINDOWS)
                        {
                            u32 stack_index = 0;
                            for (u32 argument_index = 0; argument_index < argument_count; argument_index += 1)
                            {
                                if (!arguments[argument_index].on_stack)
                                {
                                    continue;
                                }
                                IrValueId argument = instruction->operands[argument_index + 1];
                                if (arguments[argument_index].windows_indirect)
                                {
                                    if (!codegen_canonical_x64_rsp_address(&buffer, X64_REGISTER_RAX, arguments[argument_index].copy_offset,
                                                                            arguments[argument_index].copy_alignment))
                                    {
                                        result.error = CODEGEN_ERROR_CAPACITY;
                                        return result;
                                    }
                                }
                                else
                                {
                                    c_x64_load(&emitter, 0x85, argument);
                                }
                                BusterX86MetadataPhysicalOperand windows_stack_store_operands[2] = {
                                    codegen_canonical_x64_metadata_memory(X64_REGISTER_RSP, 64, 32 + (s64)stack_index * 8),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), windows_stack_store_operands,
                                                                           BUSTER_ARRAY_LENGTH(windows_stack_store_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                stack_index += arguments[argument_index].part_count;
                            }
                        }
                        if (call_x64_indirect_return)
                        {
                            X64Register indirect_return_register = call_windows_indirect_return ? X64_REGISTER_RCX : X64_REGISTER_RDI;
                            BusterX86MetadataPhysicalOperand indirect_return_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(indirect_return_register, 64),
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, result_displacement),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("LEA"), indirect_return_operands,
                                                                       BUSTER_ARRAY_LENGTH(indirect_return_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                        IrType* call_callee_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
                        if (call_callee_type && call_callee_type->kind == IR_TYPE_POINTER)
                        {
                            call_callee_type = ir_type_from_id(&program->types, call_callee_type->element_type);
                        }
                        bool windows_variadic_call = result.abi == CODEGEN_ABI_X86_64_WINDOWS && call_callee_type &&
                                                     call_callee_type->kind == IR_TYPE_FUNCTION && call_callee_type->is_variadic;
                        u32 register_index = call_x64_indirect_return ? 1 : 0;
                        for (u32 argument_index = 1; argument_index < instruction->operand_count; argument_index += 1)
                        {
                            IrValueId argument = instruction->operands[argument_index];
                            CodegenCanonicalCallArgument* call_argument = arguments + argument_index - 1;
                            IrType* argument_type = call_argument->type;
                            CodegenCanonicalAbiValue argument_abi = call_argument->abi;
                            u32 register_parts = call_argument->windows_indirect ? 1 : call_argument->part_count;
                            if (call_argument->on_stack)
                            {
                                continue;
                            }
                            if (!argument_type || (!call_argument->system_v_aggregate &&
                                                   (register_index > register_count || register_parts > register_count - register_index)))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                            if (result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && argument_type->kind == IR_TYPE_FLOAT)
                            {
                                u8 float_register = call_argument->float_register;
                                if (float_register >= 8 || (argument_type->bit_width != 32 && argument_type->bit_width != 64))
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                BusterX86MetadataPhysicalOperand float_load_operands[2] = {
                                    codegen_canonical_x64_metadata_vector(float_register, (u16)argument_type->bit_width),
                                    codegen_canonical_x64_metadata_memory(
                                        X64_REGISTER_RBP, (u16)argument_type->bit_width,
                                        c_x64_frame_displacement(&emitter, value_offsets[argument.value])),
                                };
                                String8 float_features[] = {S8("sse"), S8("sse2")};
                                if (!codegen_canonical_x64_metadata_emit_features(
                                        &buffer, argument_type->bit_width == 32 ? S8("MOVSS") : S8("MOVSD"), float_load_operands,
                                        BUSTER_ARRAY_LENGTH(float_load_operands),
                                        (BusterX86MetadataFeatureInput){.names = float_features,
                                                                         .count = BUSTER_ARRAY_LENGTH(float_features)}))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                continue;
                            }
                            if (call_argument->system_v_aggregate)
                            {
                                u32 float_register = call_argument->float_register;
                                for (u32 part_index = 0; part_index < argument_abi.part_count; part_index += 1)
                                {
                                    CodegenCanonicalAbiPart* part = argument_abi.parts + part_index;
                                    s32 displacement = c_x64_frame_displacement(&emitter, value_offsets[argument.value]) + (s32)part->value_offset;
                                    if (codegen_canonical_abi_part_is_float(part->abi_class))
                                    {
                                        if (!codegen_canonical_x64_float_memory(&buffer, target, float_register, displacement, part->size, false))
                                        {
                                            result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                            return result;
                                        }
                                        float_register += 1;
                                    }
                                    else
                                    {
                                        if (register_index >= register_count)
                                        {
                                            result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                            return result;
                                        }
                                        u8 reg = registers[register_index++];
                                        BusterX86MetadataPhysicalOperand integer_load_operands[2] = {
                                            codegen_canonical_x64_metadata_gpr((X64Register)reg, 64),
                                            codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, displacement),
                                        };
                                        if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), integer_load_operands,
                                                                                   BUSTER_ARRAY_LENGTH(integer_load_operands)))
                                        {
                                            result.error = buffer.error;
                                            return result;
                                        }
                                    }
                                }
                                continue;
                            }
                            if (result.abi == CODEGEN_ABI_X86_64_WINDOWS && argument_type->kind == IR_TYPE_FLOAT)
                            {
                                if (register_index >= register_count || (argument_type->bit_width != 32 && argument_type->bit_width != 64))
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                BusterX86MetadataPhysicalOperand float_load_operands[2] = {
                                    codegen_canonical_x64_metadata_vector(register_index, (u16)argument_type->bit_width),
                                    codegen_canonical_x64_metadata_memory(
                                        X64_REGISTER_RBP, (u16)argument_type->bit_width,
                                        c_x64_frame_displacement(&emitter, value_offsets[argument.value])),
                                };
                                String8 float_features[] = {S8("sse"), S8("sse2")};
                                if (!codegen_canonical_x64_metadata_emit_features(
                                        &buffer, argument_type->bit_width == 32 ? S8("MOVSS") : S8("MOVSD"), float_load_operands,
                                        BUSTER_ARRAY_LENGTH(float_load_operands),
                                        (BusterX86MetadataFeatureInput){.names = float_features,
                                                                         .count = BUSTER_ARRAY_LENGTH(float_features)}))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                if (windows_variadic_call)
                                {
                                    u8 reg = registers[register_index];
                                    BusterX86MetadataPhysicalOperand integer_load_operands[2] = {
                                        codegen_canonical_x64_metadata_gpr((X64Register)reg, 64),
                                        codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64,
                                                                               c_x64_frame_displacement(&emitter, value_offsets[argument.value])),
                                    };
                                    if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), integer_load_operands,
                                                                               BUSTER_ARRAY_LENGTH(integer_load_operands)))
                                    {
                                        result.error = buffer.error;
                                        return result;
                                    }
                                }
                                register_index += 1;
                                continue;
                            }
                            if (call_argument->windows_indirect)
                            {
                                u8 reg = registers[register_index++];
                                if (!codegen_canonical_x64_rsp_address(&buffer, reg, call_argument->copy_offset, call_argument->copy_alignment))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                continue;
                            }
                            for (u32 part_index = 0; part_index < call_argument->part_count; part_index += 1)
                            {
                                u8 reg = registers[register_index++];
                                BusterX86MetadataPhysicalOperand integer_load_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr((X64Register)reg, 64),
                                    codegen_canonical_x64_metadata_memory(
                                        X64_REGISTER_RBP, 64,
                                        c_x64_frame_displacement(&emitter, value_offsets[argument.value]) + (s32)(part_index * 8)),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), integer_load_operands,
                                                                           BUSTER_ARRAY_LENGTH(integer_load_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                        }
                        IrType* callee_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
                        bool indirect_call = callee_type && callee_type->kind == IR_TYPE_POINTER;
                        if (indirect_call)
                        {
                            callee_type = ir_type_from_id(&program->types, callee_type->element_type);
                        }
                        if (result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && callee_type && callee_type->kind == IR_TYPE_FUNCTION && callee_type->is_variadic)
                        {
                            BusterX86MetadataPhysicalOperand variadic_register_count_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                                codegen_canonical_x64_metadata_unsigned_immediate(simulated_float_registers, 32),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), variadic_register_count_operands,
                                                                       BUSTER_ARRAY_LENGTH(variadic_register_count_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                        if (indirect_call)
                        {
                            c_x64_load(&emitter, 0x85, instruction->operands[0]);
                            BusterX86MetadataPhysicalOperand call_register = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64);
                            if (buffer.error || !codegen_canonical_x64_metadata_emit(&buffer, S8("CALL"), &call_register, 1))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                        else
                        {
                            BusterX86MetadataPhysicalOperand call_target = codegen_canonical_x64_metadata_relative(0, 32);
                            u32 call_offset = (u32)buffer.count;
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("CALL"), &call_target, 1))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                .symbol = instruction->symbol,
                                .offset = call_offset + 1,
                                .kind = CODEGEN_MODULE_RELOCATION_X86_64_PC32,
                            };
                        }
                        if (windows_dynamic_call)
                        {
                            codegen_canonical_x64_adjust_stack(&buffer, call_layout.windows_stack_size, false);
                            if (buffer.error != CODEGEN_ERROR_NONE)
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                        if (system_v_aligned_area)
                        {
                            BusterX86MetadataPhysicalOperand stack_restore_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSP, 64),
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, c_x64_frame_displacement(&emitter, x64_stack_save_offset)),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), stack_restore_operands,
                                                                       BUSTER_ARRAY_LENGTH(stack_restore_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                        else if (result.abi != CODEGEN_ABI_X86_64_WINDOWS && (call_layout.stack_part_count || stack_padding))
                        {
                            u32 cleanup = call_layout.stack_part_count * 8 + (stack_padding ? 8 : 0);
                            codegen_canonical_x64_adjust_stack(&buffer, cleanup, false);
                        }
                        IrType* call_return_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        if (codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, instruction->canonical_type))
                        {
                            if (result.abi != CODEGEN_ABI_X86_64_SYSTEM_V ||
                                !codegen_canonical_x64_type_is_f80_x87_shape_cached(f80_cache, program, instruction->canonical_type) ||
                                !codegen_canonical_x64_abi_is_f80_result(call_return_type, &call_return_abi))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                            // SysV returns f80 in ST0.  Spill it immediately
                            // into the canonical sixteen-byte result slot, or
                            // pop it when the call result is unused.
                            if (x87_stack_depth != 0)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
                            {
                                x87_stack_depth += 1;
                                if (!codegen_canonical_x64_emit_f80_store_top(&buffer, X64_REGISTER_RBP, result_displacement,
                                                                               &x87_stack_depth))
                                {
                                    result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                            }
                            else
                            {
                                BusterX86MetadataPhysicalOperand x87_st0_operand = {
                                    .kind = BUSTER_X86_METADATA_PHYSICAL_OPERAND_REGISTER,
                                    .width = 80,
                                    .reg = {
                                        .index = 0,
                                        .width = 80,
                                        .physical_class = BUSTER_X86_METADATA_PHYSICAL_CLASS_SPECIAL,
                                    },
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("FSTP"), &x87_st0_operand, 1))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
                        {
                            IrType* return_type = call_return_type;
                            if (return_type && return_type->kind == IR_TYPE_FLOAT)
                            {
                                if (return_type->bit_width != 32 && return_type->bit_width != 64)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                BusterX86MetadataPhysicalOperand return_float_store_operands[2] = {
                                    codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, (u16)return_type->bit_width,
                                                                          result_displacement),
                                    codegen_canonical_x64_metadata_vector(0, (u16)return_type->bit_width),
                                };
                                String8 return_float_features[] = {S8("sse"), S8("sse2")};
                                if (!codegen_canonical_x64_metadata_emit_features(
                                        &buffer, return_type->bit_width == 32 ? S8("MOVSS") : S8("MOVSD"), return_float_store_operands,
                                        BUSTER_ARRAY_LENGTH(return_float_store_operands),
                                        (BusterX86MetadataFeatureInput){.names = return_float_features,
                                                                         .count = BUSTER_ARRAY_LENGTH(return_float_features)}))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                instruction_id = instruction->next;
                                continue;
                            }
                            if (result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && call_return_abi.part_count && !call_return_abi.indirect && !call_return_abi.memory)
                            {
                                u32 integer_index = 0;
                                u32 float_index = 0;
                                for (u32 part_index = 0; part_index < call_return_abi.part_count; part_index += 1)
                                {
                                    CodegenCanonicalAbiPart* part = call_return_abi.parts + part_index;
                                    if (codegen_canonical_abi_part_is_float(part->abi_class))
                                    {
                                        u32 register_size = 0;
                                        u32 register_count_used = codegen_canonical_x64_vector_part_registers(&target, part->size, &register_size);
                                        if (!register_count_used || float_index + register_count_used > BUSTER_MAX((u32)2, register_count_used))
                                        {
                                            result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                            return result;
                                        }
                                        for (u32 chunk = 0; chunk < register_count_used; chunk += 1)
                                        {
                                            if (!codegen_canonical_x64_float_memory(&buffer, target, float_index,
                                                                                    result_displacement + (s32)part->value_offset + (s32)(chunk * register_size),
                                                                                    register_size, true))
                                            {
                                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                                return result;
                                            }
                                            float_index += 1;
                                        }
                                    }
                                    else
                                    {
                                        if (integer_index >= 2)
                                        {
                                            result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                            return result;
                                        }
                                        BusterX86MetadataPhysicalOperand integer_return_store_operands[2] = {
                                            codegen_canonical_x64_metadata_memory(
                                                X64_REGISTER_RBP, 64, result_displacement + (s32)part->value_offset),
                                            codegen_canonical_x64_metadata_gpr(integer_index ? X64_REGISTER_RDX : X64_REGISTER_RAX, 64),
                                        };
                                        if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), integer_return_store_operands,
                                                                                   BUSTER_ARRAY_LENGTH(integer_return_store_operands)))
                                        {
                                            result.error = buffer.error;
                                            return result;
                                        }
                                        integer_index += 1;
                                    }
                                }
                                instruction_id = instruction->next;
                                continue;
                            }
                            u32 return_parts = 0;
                            bool aggregate_return = codegen_canonical_integer_aggregate_parts(program, instruction->canonical_type, &return_parts);
                            CodegenCanonicalAbiValue aggregate_return_abi =
                                codegen_canonical_aggregate_abi(program, instruction->canonical_type, result.abi, true, false);
                            if (aggregate_return_abi.part_count && !aggregate_return_abi.indirect)
                            {
                                aggregate_return = true;
                                return_parts = aggregate_return_abi.part_count;
                            }
                            if (aggregate_return_abi.indirect)
                            {
                                if (!call_x64_indirect_return)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                instruction_id = instruction->next;
                                continue;
                            }
                            c_x64_store_result(&emitter, result_displacement);
                            if (aggregate_return && return_parts == 2)
                            {
                                BusterX86MetadataPhysicalOperand aggregate_return_store_operands[2] = {
                                    codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, result_displacement + 8),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), aggregate_return_store_operands,
                                                                           BUSTER_ARRAY_LENGTH(aggregate_return_store_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_ARRAY)
                    {
                        IrType* array = ir_type_from_id(&program->types, instruction->canonical_type);
                        IrType* element = array ? ir_type_from_id(&program->types, array->element_type) : 0;
                        if (!array || !element || (array->kind != IR_TYPE_ARRAY && array->kind != IR_TYPE_VECTOR) ||
                            instruction->operand_count != array->element_count)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        for (u32 element_index = 0; element_index < instruction->operand_count; element_index += 1)
                        {
                            u64 copied = 0;
                            while (copied < element->layout.size)
                            {
                                u64 remaining = element->layout.size - copied;
                                u32 chunk = remaining >= 8 ? 8 : remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;
                                u16 memory_width = (u16)(chunk * 8);
                                u16 register_width = chunk <= 2 ? 32 : memory_width;
                                String8 load_mnemonic = chunk <= 2 ? S8("MOVZX") : S8("MOV");
                                BusterX86MetadataPhysicalOperand load_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, register_width),
                                    codegen_canonical_x64_metadata_memory(
                                        X64_REGISTER_RBP, memory_width,
                                        c_x64_frame_displacement(&emitter, value_offsets[instruction->operands[element_index].value]) + (s32)copied),
                                };
                                BusterX86MetadataPhysicalOperand store_operands[2] = {
                                    codegen_canonical_x64_metadata_memory(
                                        X64_REGISTER_RBP, memory_width,
                                        result_displacement + (s32)(element_index * element->layout.size + copied)),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, memory_width),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, load_mnemonic, load_operands,
                                                                           BUSTER_ARRAY_LENGTH(load_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), store_operands,
                                                                           BUSTER_ARRAY_LENGTH(store_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                copied += chunk;
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_AGGREGATE)
                    {
                        IrType* aggregate = ir_type_from_id(&program->types, instruction->canonical_type);
                        if (!aggregate || instruction->operand_count != instruction->immediate_count)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        u64 aggregate_copied = 0;
                        while (aggregate_copied < aggregate->layout.size)
                        {
                            u64 remaining = aggregate->layout.size - aggregate_copied;
                            u32 chunk = remaining >= 8 ? 8 : remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;
                            u16 memory_width = (u16)(chunk * 8);
                            BusterX86MetadataPhysicalOperand zero_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                            };
                            BusterX86MetadataPhysicalOperand zero_store_operands[2] = {
                                codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, memory_width,
                                                                      result_displacement + (s32)aggregate_copied),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, memory_width),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), zero_operands,
                                                                       BUSTER_ARRAY_LENGTH(zero_operands)) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), zero_store_operands,
                                                                       BUSTER_ARRAY_LENGTH(zero_store_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            aggregate_copied += chunk;
                        }
                        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                        {
                            u64 field_index = instruction->immediates[operand_index];
                            if (field_index >= aggregate->field_count)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrField* field = aggregate->fields + field_index;
                            IrType* field_type = ir_type_from_id(&program->types, field->type);
                            if (!field_type || !field_type->layout.resolved)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            s32 field_displacement = result_displacement + (s32)field->offset;
                            if (field->is_bit_field)
                            {
                                if (!field->bit_width)
                                {
                                    continue;
                                }
                                if (field_type->layout.size != 1 && field_type->layout.size != 2 && field_type->layout.size != 4 &&
                                    field_type->layout.size != 8)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                                c_x64_load(&emitter, 0x85, instruction->operands[operand_index]);
                                if (field->bit_width < 64)
                                {
                                    BusterX86MetadataPhysicalOperand mask_load_operands[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                        codegen_canonical_x64_metadata_unsigned_immediate(((u64)1 << field->bit_width) - 1, 64),
                                    };
                                    BusterX86MetadataPhysicalOperand mask_operands[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                    };
                                    if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), mask_load_operands,
                                                                               BUSTER_ARRAY_LENGTH(mask_load_operands)) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("AND"), mask_operands,
                                                                               BUSTER_ARRAY_LENGTH(mask_operands)))
                                    {
                                        result.error = buffer.error;
                                        return result;
                                    }
                                }
                                if (field->bit_offset)
                                {
                                    BusterX86MetadataPhysicalOperand shift_count_load_operands[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 32),
                                        codegen_canonical_x64_metadata_unsigned_immediate(field->bit_offset, 32),
                                    };
                                    BusterX86MetadataPhysicalOperand shift_operands[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 8),
                                    };
                                    if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), shift_count_load_operands,
                                                                               BUSTER_ARRAY_LENGTH(shift_count_load_operands)) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("SHL"), shift_operands,
                                                                               BUSTER_ARRAY_LENGTH(shift_operands)))
                                    {
                                        result.error = buffer.error;
                                        return result;
                                    }
                                }
                                BusterX86MetadataPhysicalOperand bitfield_store_operands[2] = {
                                    codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, (u16)(field_type->layout.size * 8),
                                                                          field_displacement),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, (u16)(field_type->layout.size * 8)),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("OR"), bitfield_store_operands,
                                                                           BUSTER_ARRAY_LENGTH(bitfield_store_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                continue;
                            }
                            u64 field_copied = 0;
                            while (field_copied < field_type->layout.size)
                            {
                                u64 remaining = field_type->layout.size - field_copied;
                                u32 chunk = remaining >= 8 ? 8 : remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;
                                u16 memory_width = (u16)(chunk * 8);
                                u16 register_width = chunk <= 2 ? 32 : memory_width;
                                String8 load_mnemonic = chunk <= 2 ? S8("MOVZX") : S8("MOV");
                                BusterX86MetadataPhysicalOperand load_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, register_width),
                                    codegen_canonical_x64_metadata_memory(
                                        X64_REGISTER_RBP, memory_width,
                                        c_x64_frame_displacement(&emitter, value_offsets[instruction->operands[operand_index].value]) + (s32)field_copied),
                                };
                                BusterX86MetadataPhysicalOperand store_operands[2] = {
                                    codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, memory_width,
                                                                          field_displacement + (s32)field_copied),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, memory_width),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, load_mnemonic, load_operands,
                                                                           BUSTER_ARRAY_LENGTH(load_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), store_operands,
                                                                           BUSTER_ARRAY_LENGTH(store_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                field_copied += chunk;
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_UNARY)
                    {
                        IrType* canonical_unary_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        if (codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, instruction->canonical_type) ||
                            (instruction->operand_count &&
                             codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, function->values[instruction->operands[0].value].canonical_type)))
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        if (canonical_unary_type && canonical_unary_type->kind == IR_TYPE_VECTOR)
                        {
                            if (!codegen_canonical_x64_vector_operation(
                                    &buffer, program, function, instruction, value_offsets, canonical_x64_frame_base_offset, target,
                                    &result.statistics.native_vector_operation_count, &result.statistics.split_vector_operation_count,
                                    &x64_upper_vector_dirty, &x64_last_wide_vector_result, &x64_last_wide_vector_size,
                                    &result.statistics.forwarded_wide_vector_load_count))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        IrType* canonical_unary_operand_type =
                            ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
                        if (canonical_unary_operand_type && canonical_unary_operand_type->kind == IR_TYPE_INTEGER &&
                            canonical_unary_operand_type->bit_width == 128)
                        {
                            if (instruction->unary_operation != IR_UNARY_INTEGER_NEGATE &&
                                instruction->unary_operation != IR_UNARY_INTEGER_BITWISE_NOT)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            c_x64_load(&emitter, 0x85, instruction->operands[0]);
                            c_x64_load_high(&emitter, 0x95, instruction->operands[0]);
                            if (instruction->unary_operation == IR_UNARY_INTEGER_NEGATE)
                            {
                                BusterX86MetadataPhysicalOperand negate_low_operand =
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64);
                                BusterX86MetadataPhysicalOperand carry_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                    codegen_canonical_x64_metadata_immediate(0, 8),
                                };
                                BusterX86MetadataPhysicalOperand negate_high_operand =
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64);
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("NEG"), &negate_low_operand, 1) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("ADC"), carry_operands,
                                                                           BUSTER_ARRAY_LENGTH(carry_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("NEG"), &negate_high_operand, 1))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            else
                            {
                                BusterX86MetadataPhysicalOperand not_low_operand =
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64);
                                BusterX86MetadataPhysicalOperand not_high_operand =
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64);
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("NOT"), &not_low_operand, 1) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("NOT"), &not_high_operand, 1))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            c_x64_store_result(&emitter, result_displacement);
                            c_x64_store_high_rdx(&emitter, result_displacement);
                            instruction_id = instruction->next;
                            continue;
                        }
                        c_x64_load(&emitter, 0x85, instruction->operands[0]);
                        if (instruction->unary_operation == IR_UNARY_BOOLEAN_NOT)
                        {
                            BusterX86MetadataPhysicalOperand test_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            };
                            BusterX86MetadataPhysicalOperand set_operand =
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 8);
                            BusterX86MetadataPhysicalOperand extend_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 8),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("TEST"), test_operands,
                                                                       BUSTER_ARRAY_LENGTH(test_operands)) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("SETZ"), &set_operand, 1) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("MOVZX"), extend_operands,
                                                                       BUSTER_ARRAY_LENGTH(extend_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                        else if (instruction->unary_operation == IR_UNARY_FLOAT_NEGATE)
                        {
                            IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
                            if (!type || type->kind != IR_TYPE_FLOAT || (type->bit_width != 32 && type->bit_width != 64))
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            u64 sign = type->bit_width == 32 ? (u64)1 << 31 : (u64)1 << 63;
                            BusterX86MetadataPhysicalOperand sign_load_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                codegen_canonical_x64_metadata_unsigned_immediate(sign, 64),
                            };
                            BusterX86MetadataPhysicalOperand xor_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                            };
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), sign_load_operands,
                                                                       BUSTER_ARRAY_LENGTH(sign_load_operands)) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), xor_operands,
                                                                       BUSTER_ARRAY_LENGTH(xor_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                        else if (instruction->unary_operation == IR_UNARY_INTEGER_NEGATE || instruction->unary_operation == IR_UNARY_INTEGER_BITWISE_NOT)
                        {
                            IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
                            if (!type || type->kind != IR_TYPE_INTEGER)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            BusterX86MetadataPhysicalOperand unary_operand = codegen_canonical_x64_metadata_gpr(
                                X64_REGISTER_RAX, type->bit_width > 32 ? 64 : 32);
                            if (!codegen_canonical_x64_metadata_emit(
                                    &buffer, instruction->unary_operation == IR_UNARY_INTEGER_NEGATE ? S8("NEG") : S8("NOT"),
                                    &unary_operand, 1))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                        else if (instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS ||
                                 instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS)
                        {
                            IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
                            if (!type || type->kind != IR_TYPE_INTEGER)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            u16 operation_width = type->bit_width > 32 ? 64 : 32;
                            BusterX86MetadataPhysicalOperand count_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, operation_width),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, operation_width),
                            };
                            String8 count_mnemonic = instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS
                                                         ? S8("BSF")
                                                         : S8("BSR");
                            if (!codegen_canonical_x64_metadata_emit(&buffer, count_mnemonic, count_operands,
                                                                       BUSTER_ARRAY_LENGTH(count_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            if (instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS)
                            {
                                BusterX86MetadataPhysicalOperand invert_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, operation_width),
                                    codegen_canonical_x64_metadata_immediate((s64)type->bit_width - 1, 8),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), invert_operands,
                                                                           BUSTER_ARRAY_LENGTH(invert_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                        }
                        else if (instruction->unary_operation == IR_UNARY_INTEGER_POPULATION_COUNT)
                        {
                            IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
                            if (!type || type->kind != IR_TYPE_INTEGER)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            x64_emit_population_count(&buffer, type->bit_width);
                        }
                        else
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        c_x64_store_result(&emitter, result_displacement);
                    }
                    else if (instruction->opcode == IR_OPCODE_BINARY)
                    {
                        IrTypeId operand_type = function->values[instruction->operands[0].value].canonical_type;
                        IrType* operand_type_value = ir_type_from_id(&program->types, operand_type);
                        if (codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, operand_type) ||
                            codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, function->values[instruction->operands[1].value].canonical_type) ||
                            codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, instruction->canonical_type))
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        if (operand_type_value && operand_type_value->kind == IR_TYPE_VECTOR)
                        {
                            if (!codegen_canonical_x64_vector_operation(
                                    &buffer, program, function, instruction, value_offsets, canonical_x64_frame_base_offset, target,
                                    &result.statistics.native_vector_operation_count, &result.statistics.split_vector_operation_count,
                                    &x64_upper_vector_dirty, &x64_last_wide_vector_result, &x64_last_wide_vector_size,
                                    &result.statistics.forwarded_wide_vector_load_count))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (operand_type_value && operand_type_value->kind == IR_TYPE_FLOAT)
                        {
                            u32 width = operand_type_value->bit_width;
                            if (width != 32 && width != 64)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            c_x64_load_float(&emitter, 0, instruction->operands[0], width);
                            c_x64_load_float(&emitter, 1, instruction->operands[1], width);
                            IrBinaryOperation operation = instruction->binary_operation;
                            String8 feature_names[] = {S8("sse"), S8("sse2")};
                            if (operation >= IR_BINARY_FLOAT_ADD && operation <= IR_BINARY_FLOAT_DIVIDE)
                            {
                                String8 mnemonic = operation == IR_BINARY_FLOAT_ADD        ? (width == 32 ? S8("ADDSS") : S8("ADDSD"))
                                                   : operation == IR_BINARY_FLOAT_SUBTRACT   ? (width == 32 ? S8("SUBSS") : S8("SUBSD"))
                                                   : operation == IR_BINARY_FLOAT_MULTIPLY   ? (width == 32 ? S8("MULSS") : S8("MULSD"))
                                                                                             : (width == 32 ? S8("DIVSS") : S8("DIVSD"));
                                BusterX86MetadataPhysicalOperand arithmetic_operands[2] = {
                                    codegen_canonical_x64_metadata_vector(0, (u16)width),
                                    codegen_canonical_x64_metadata_vector(1, (u16)width),
                                };
                                if (!codegen_canonical_x64_metadata_emit_features(
                                        &buffer, mnemonic, arithmetic_operands, BUSTER_ARRAY_LENGTH(arithmetic_operands),
                                        (BusterX86MetadataFeatureInput){.names = feature_names, .count = BUSTER_ARRAY_LENGTH(feature_names)}))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                BusterX86MetadataPhysicalOperand store_operands[2] = {
                                    codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, (u16)width, result_displacement),
                                    codegen_canonical_x64_metadata_vector(0, (u16)width),
                                };
                                if (!codegen_canonical_x64_metadata_emit_features(
                                        &buffer, width == 32 ? S8("MOVSS") : S8("MOVSD"), store_operands,
                                        BUSTER_ARRAY_LENGTH(store_operands),
                                        (BusterX86MetadataFeatureInput){.names = feature_names, .count = BUSTER_ARRAY_LENGTH(feature_names)}))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            else
                            {
                                String8 compare_mnemonic = width == 32 ? S8("UCOMISS") : S8("UCOMISD");
                                BusterX86MetadataPhysicalOperand compare_operands[2] = {
                                    codegen_canonical_x64_metadata_vector(0, (u16)width),
                                    codegen_canonical_x64_metadata_vector(1, (u16)width),
                                };
                                if (!codegen_canonical_x64_metadata_emit_features(
                                        &buffer, compare_mnemonic, compare_operands, BUSTER_ARRAY_LENGTH(compare_operands),
                                        (BusterX86MetadataFeatureInput){.names = feature_names, .count = BUSTER_ARRAY_LENGTH(feature_names)}))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                String8 condition_mnemonic = operation == IR_BINARY_FLOAT_EQUAL           ? S8("SETZ")
                                                             : operation == IR_BINARY_FLOAT_NOT_EQUAL     ? S8("SETNZ")
                                                             : operation == IR_BINARY_FLOAT_LESS          ? S8("SETB")
                                                             : operation == IR_BINARY_FLOAT_LESS_EQUAL    ? S8("SETBE")
                                                             : operation == IR_BINARY_FLOAT_GREATER       ? S8("SETNBE")
                                                             : operation == IR_BINARY_FLOAT_GREATER_EQUAL ? S8("SETNB")
                                                                                                            : (String8){0};
                                if (!condition_mnemonic.length)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                                BusterX86MetadataPhysicalOperand set_operands[] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 8),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, condition_mnemonic, set_operands,
                                                                           BUSTER_ARRAY_LENGTH(set_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                if (operation == IR_BINARY_FLOAT_EQUAL || operation == IR_BINARY_FLOAT_LESS || operation == IR_BINARY_FLOAT_LESS_EQUAL)
                                {
                                    BusterX86MetadataPhysicalOperand ordered_operands[] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 8),
                                    };
                                    if (!codegen_canonical_x64_metadata_emit(&buffer, S8("SETNP"), ordered_operands,
                                                                               BUSTER_ARRAY_LENGTH(ordered_operands)))
                                    {
                                        result.error = buffer.error;
                                        return result;
                                    }
                                    BusterX86MetadataPhysicalOperand and_operands[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 8),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 8),
                                    };
                                    if (!codegen_canonical_x64_metadata_emit(&buffer, S8("AND"), and_operands,
                                                                               BUSTER_ARRAY_LENGTH(and_operands)))
                                    {
                                        result.error = buffer.error;
                                        return result;
                                    }
                                }
                                else if (operation == IR_BINARY_FLOAT_NOT_EQUAL)
                                {
                                    BusterX86MetadataPhysicalOperand unordered_operands[] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 8),
                                    };
                                    if (!codegen_canonical_x64_metadata_emit(&buffer, S8("SETP"), unordered_operands,
                                                                               BUSTER_ARRAY_LENGTH(unordered_operands)))
                                    {
                                        result.error = buffer.error;
                                        return result;
                                    }
                                    BusterX86MetadataPhysicalOperand or_operands[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 8),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 8),
                                    };
                                    if (!codegen_canonical_x64_metadata_emit(&buffer, S8("OR"), or_operands,
                                                                               BUSTER_ARRAY_LENGTH(or_operands)))
                                    {
                                        result.error = buffer.error;
                                        return result;
                                    }
                                }
                                BusterX86MetadataPhysicalOperand widen_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 8),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOVZX"), widen_operands,
                                                                           BUSTER_ARRAY_LENGTH(widen_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                c_x64_store_result(&emitter, result_displacement);
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        bool integer128 = operand_type_value && operand_type_value->kind == IR_TYPE_INTEGER && operand_type_value->bit_width == 128;
                        if (integer128)
                        {
                            c_x64_load(&emitter, 0x85, instruction->operands[0]);
                            c_x64_load_high(&emitter, 0x95, instruction->operands[0]);
                            c_x64_load(&emitter, 0x8d, instruction->operands[1]);
                            c_x64_load_high(&emitter, 0xb5, instruction->operands[1]);
                            IrBinaryOperation operation = instruction->binary_operation;
                            if (operation == IR_BINARY_INTEGER_ADD || operation == IR_BINARY_INTEGER_SUBTRACT)
                            {
                                BusterX86MetadataPhysicalOperand low_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                };
                                BusterX86MetadataPhysicalOperand high_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSI, 64),
                                };
                                String8 mnemonic = operation == IR_BINARY_INTEGER_ADD ? S8("ADD") : S8("SUB");
                                String8 carry_mnemonic = operation == IR_BINARY_INTEGER_ADD ? S8("ADC") : S8("SBB");
                                if (!codegen_canonical_x64_metadata_emit(&buffer, mnemonic, low_operands, BUSTER_ARRAY_LENGTH(low_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, carry_mnemonic, high_operands, BUSTER_ARRAY_LENGTH(high_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            else if (operation == IR_BINARY_INTEGER_BITWISE_AND || operation == IR_BINARY_INTEGER_BITWISE_OR ||
                                     operation == IR_BINARY_INTEGER_BITWISE_XOR)
                            {
                                String8 mnemonic = operation == IR_BINARY_INTEGER_BITWISE_AND ? S8("AND")
                                                   : operation == IR_BINARY_INTEGER_BITWISE_OR ? S8("OR")
                                                                                              : S8("XOR");
                                BusterX86MetadataPhysicalOperand low_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                };
                                BusterX86MetadataPhysicalOperand high_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSI, 64),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, mnemonic, low_operands, BUSTER_ARRAY_LENGTH(low_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, mnemonic, high_operands, BUSTER_ARRAY_LENGTH(high_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            else if (operation == IR_BINARY_INTEGER_MULTIPLY)
                            {
                                BusterX86MetadataPhysicalOperand move_r11_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R11, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                };
                                BusterX86MetadataPhysicalOperand move_r9_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                };
                                BusterX86MetadataPhysicalOperand multiply_low_operands[] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                };
                                BusterX86MetadataPhysicalOperand cross_low_high_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                };
                                BusterX86MetadataPhysicalOperand add_high_low_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 64),
                                };
                                BusterX86MetadataPhysicalOperand cross_high_low_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R11, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSI, 64),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), move_r11_operands,
                                                                           BUSTER_ARRAY_LENGTH(move_r11_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), move_r9_operands,
                                                                           BUSTER_ARRAY_LENGTH(move_r9_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("MUL"), multiply_low_operands,
                                                                           BUSTER_ARRAY_LENGTH(multiply_low_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("IMUL"), cross_low_high_operands,
                                                                           BUSTER_ARRAY_LENGTH(cross_low_high_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("ADD"), add_high_low_operands,
                                                                           BUSTER_ARRAY_LENGTH(add_high_low_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("IMUL"), cross_high_low_operands,
                                                                           BUSTER_ARRAY_LENGTH(cross_high_low_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("ADD"),
                                                                           (BusterX86MetadataPhysicalOperand[2]){
                                                                               codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                                                               codegen_canonical_x64_metadata_gpr(X64_REGISTER_R11, 64)},
                                                                           2))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            else if (operation == IR_BINARY_UNSIGNED_DIVIDE || operation == IR_BINARY_UNSIGNED_REMAINDER ||
                                     operation == IR_BINARY_SIGNED_DIVIDE || operation == IR_BINARY_SIGNED_REMAINDER)
                            {
                                bool signed_division = operation == IR_BINARY_SIGNED_DIVIDE || operation == IR_BINARY_SIGNED_REMAINDER;
                                bool remainder_result = operation == IR_BINARY_UNSIGNED_REMAINDER || operation == IR_BINARY_SIGNED_REMAINDER;
                                if (signed_division)
                                {
                                    BusterX86MetadataPhysicalOperand dividend_sign_move[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                    };
                                    BusterX86MetadataPhysicalOperand dividend_sign_shift[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                        codegen_canonical_x64_metadata_immediate(63, 8),
                                    };
                                    BusterX86MetadataPhysicalOperand divisor_sign_move[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSI, 64),
                                    };
                                    BusterX86MetadataPhysicalOperand divisor_sign_shift[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 64),
                                        codegen_canonical_x64_metadata_immediate(63, 8),
                                    };
                                    BusterX86MetadataPhysicalOperand save_dividend_sign[2] = {
                                        codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, result_displacement + 8),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                    };
                                    BusterX86MetadataPhysicalOperand sign_xor[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                    };
                                    BusterX86MetadataPhysicalOperand save_sign[2] = {
                                        codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 32, result_displacement + 4),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 32),
                                    };
                                    BusterX86MetadataPhysicalOperand dividend_low_xor[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                    };
                                    BusterX86MetadataPhysicalOperand dividend_high_xor[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                    };
                                    BusterX86MetadataPhysicalOperand dividend_low_sub[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                    };
                                    BusterX86MetadataPhysicalOperand dividend_high_sbb[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                    };
                                    BusterX86MetadataPhysicalOperand divisor_low_xor[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 64),
                                    };
                                    BusterX86MetadataPhysicalOperand divisor_high_xor[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSI, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 64),
                                    };
                                    BusterX86MetadataPhysicalOperand divisor_low_sub[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 64),
                                    };
                                    BusterX86MetadataPhysicalOperand divisor_high_sbb[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSI, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 64),
                                    };
                                    if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), dividend_sign_move, 2) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("SAR"), dividend_sign_shift, 2) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), divisor_sign_move, 2) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("SAR"), divisor_sign_shift, 2) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), save_dividend_sign, 2) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), sign_xor, 2) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), save_sign, 2) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), dividend_low_xor, 2) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), dividend_high_xor, 2) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("SUB"), dividend_low_sub, 2) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("SBB"), dividend_high_sbb, 2) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), divisor_low_xor, 2) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), divisor_high_xor, 2) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("SUB"), divisor_low_sub, 2) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("SBB"), divisor_high_sbb, 2))
                                    {
                                        result.error = buffer.error;
                                        return result;
                                    }
                                }
                                BusterX86MetadataPhysicalOperand loop_count_store[2] = {
                                    codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 32, result_displacement),
                                    codegen_canonical_x64_metadata_immediate(128, 32),
                                };
                                BusterX86MetadataPhysicalOperand quotient_low_zero[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 32),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 32),
                                };
                                BusterX86MetadataPhysicalOperand quotient_high_zero[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 32),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 32),
                                };
                                BusterX86MetadataPhysicalOperand remainder_low_zero[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R10, 32),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R10, 32),
                                };
                                BusterX86MetadataPhysicalOperand remainder_high_zero[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R11, 32),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R11, 32),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), loop_count_store, 2) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), quotient_low_zero, 2) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), quotient_high_zero, 2) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), remainder_low_zero, 2) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), remainder_high_zero, 2))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                u32 divide_loop = (u32)buffer.count;
                                BusterX86MetadataPhysicalOperand shift_low[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_immediate(1, 8),
                                };
                                BusterX86MetadataPhysicalOperand shift_high[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                    codegen_canonical_x64_metadata_immediate(1, 8),
                                };
                                BusterX86MetadataPhysicalOperand shift_remainder_low[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R10, 64),
                                    codegen_canonical_x64_metadata_immediate(1, 8),
                                };
                                BusterX86MetadataPhysicalOperand shift_remainder_high[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R11, 64),
                                    codegen_canonical_x64_metadata_immediate(1, 8),
                                };
                                BusterX86MetadataPhysicalOperand shift_quotient_low[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                    codegen_canonical_x64_metadata_immediate(1, 8),
                                };
                                BusterX86MetadataPhysicalOperand shift_quotient_high[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 64),
                                    codegen_canonical_x64_metadata_immediate(1, 8),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("SHL"), shift_low, 2) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("RCL"), shift_high, 2) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("RCL"), shift_remainder_low, 2) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("RCL"), shift_remainder_high, 2) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("SHL"), shift_quotient_low, 2) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("RCL"), shift_quotient_high, 2))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                BusterX86MetadataPhysicalOperand high_compare[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R11, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSI, 64),
                                };
                                BusterX86MetadataPhysicalOperand high_less_branch = codegen_canonical_x64_metadata_relative(0, 32);
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("CMP"), high_compare, 2))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                u32 high_less_patch = (u32)buffer.count;
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JB"), &high_less_branch, 1))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                u32 high_greater_patch = (u32)buffer.count;
                                BusterX86MetadataPhysicalOperand high_greater_branch = codegen_canonical_x64_metadata_relative(0, 32);
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JNBE"), &high_greater_branch, 1))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                BusterX86MetadataPhysicalOperand low_compare[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R10, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                };
                                BusterX86MetadataPhysicalOperand low_less_branch = codegen_canonical_x64_metadata_relative(0, 32);
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("CMP"), low_compare, 2))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                u32 low_less_patch = (u32)buffer.count;
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JB"), &low_less_branch, 1))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                u32 subtract_offset = (u32)buffer.count;
                                BusterX86MetadataPhysicalOperand subtract_low[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R10, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                };
                                BusterX86MetadataPhysicalOperand subtract_high[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R11, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSI, 64),
                                };
                                BusterX86MetadataPhysicalOperand quotient_bit[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 8),
                                    codegen_canonical_x64_metadata_immediate(1, 8),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("SUB"), subtract_low, 2) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("SBB"), subtract_high, 2) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("OR"), quotient_bit, 2))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                u32 divide_skip = (u32)buffer.count;
                                BusterX86MetadataPhysicalOperand loop_count_decrement = codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 32, result_displacement);
                                BusterX86MetadataPhysicalOperand loop_branch = codegen_canonical_x64_metadata_relative(0, 32);
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("DEC"), &loop_count_decrement, 1))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                u32 divide_loop_patch = (u32)buffer.count;
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JNZ"), &loop_branch, 1))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                s32 high_less_delta = (s32)((s64)divide_skip - ((s64)high_less_patch + 6));
                                s32 high_greater_delta = (s32)((s64)subtract_offset - ((s64)high_greater_patch + 6));
                                s32 low_less_delta = (s32)((s64)divide_skip - ((s64)low_less_patch + 6));
                                s32 divide_loop_delta = (s32)((s64)divide_loop - ((s64)divide_loop_patch + 6));
                                if (buffer.error == CODEGEN_ERROR_NONE && high_less_patch + 6 <= buffer.count && high_greater_patch + 6 <= buffer.count &&
                                    low_less_patch + 6 <= buffer.count && divide_loop_patch + 6 <= buffer.count)
                                {
                                    memcpy(buffer.bytes + high_less_patch + 2, &high_less_delta, sizeof(high_less_delta));
                                    memcpy(buffer.bytes + high_greater_patch + 2, &high_greater_delta, sizeof(high_greater_delta));
                                    memcpy(buffer.bytes + low_less_patch + 2, &low_less_delta, sizeof(low_less_delta));
                                    memcpy(buffer.bytes + divide_loop_patch + 2, &divide_loop_delta, sizeof(divide_loop_delta));
                                }
                                else if (buffer.error == CODEGEN_ERROR_NONE)
                                {
                                    buffer.error = CODEGEN_ERROR_CAPACITY;
                                }
                                if (buffer.error != CODEGEN_ERROR_NONE)
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                BusterX86MetadataPhysicalOperand result_low_move[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_gpr(remainder_result ? X64_REGISTER_R10 : X64_REGISTER_R8, 64),
                                };
                                BusterX86MetadataPhysicalOperand result_high_move[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                    codegen_canonical_x64_metadata_gpr(remainder_result ? X64_REGISTER_R11 : X64_REGISTER_R9, 64),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), result_low_move, 2) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), result_high_move, 2))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                if (signed_division)
                                {
                                    if (remainder_result)
                                    {
                                        BusterX86MetadataPhysicalOperand load_dividend_sign[2] = {
                                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                            codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, result_displacement + 8),
                                        };
                                        if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), load_dividend_sign, 2))
                                        {
                                            result.error = buffer.error;
                                            return result;
                                        }
                                    }
                                    else
                                    {
                                        BusterX86MetadataPhysicalOperand load_sign[2] = {
                                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 32),
                                            codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 32, result_displacement + 4),
                                        };
                                        BusterX86MetadataPhysicalOperand negate_sign[] = {
                                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                        };
                                        if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), load_sign, 2) ||
                                            !codegen_canonical_x64_metadata_emit(&buffer, S8("NEG"), negate_sign, 1))
                                        {
                                            result.error = buffer.error;
                                            return result;
                                        }
                                    }
                                    BusterX86MetadataPhysicalOperand result_low_xor[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                    };
                                    BusterX86MetadataPhysicalOperand result_high_xor[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                    };
                                    BusterX86MetadataPhysicalOperand result_low_sub[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                    };
                                    BusterX86MetadataPhysicalOperand result_high_sbb[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                    };
                                    if (!codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), result_low_xor, 2) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), result_high_xor, 2) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("SUB"), result_low_sub, 2) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("SBB"), result_high_sbb, 2))
                                    {
                                        result.error = buffer.error;
                                        return result;
                                    }
                                }
                            }
                            else if (operation == IR_BINARY_SHIFT_LEFT || operation == IR_BINARY_SIGNED_SHIFT_RIGHT ||
                                     operation == IR_BINARY_UNSIGNED_SHIFT_RIGHT)
                            {
                                BusterX86MetadataPhysicalOperand count_compare_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                    codegen_canonical_x64_metadata_immediate(64, 8),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("CMP"), count_compare_operands,
                                                                          BUSTER_ARRAY_LENGTH(count_compare_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                BusterX86MetadataPhysicalOperand large_branch_operand = codegen_canonical_x64_metadata_relative(0, 32);
                                u32 large_branch_offset = (u32)buffer.count;
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JNB"), &large_branch_operand, 1))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                BusterX86MetadataPhysicalOperand variable_shift_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 8),
                                };
                                BusterX86MetadataPhysicalOperand high_variable_shift_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 8),
                                };
                                bool shift_ok = true;
                                if (operation == IR_BINARY_SHIFT_LEFT)
                                {
                                    BusterX86MetadataPhysicalOperand double_shift_operands[3] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 8),
                                    };
                                    shift_ok = codegen_canonical_x64_metadata_emit(&buffer, S8("SHLD"), double_shift_operands,
                                                                                   BUSTER_ARRAY_LENGTH(double_shift_operands)) &&
                                               codegen_canonical_x64_metadata_emit(&buffer, S8("SHL"), variable_shift_operands,
                                                                                   BUSTER_ARRAY_LENGTH(variable_shift_operands));
                                }
                                else
                                {
                                    BusterX86MetadataPhysicalOperand double_shift_operands[3] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 8),
                                    };
                                    shift_ok = codegen_canonical_x64_metadata_emit(&buffer, S8("SHRD"), double_shift_operands,
                                                                                   BUSTER_ARRAY_LENGTH(double_shift_operands)) &&
                                               codegen_canonical_x64_metadata_emit(&buffer,
                                                                                   operation == IR_BINARY_SIGNED_SHIFT_RIGHT ? S8("SAR") : S8("SHR"),
                                                                                   high_variable_shift_operands,
                                                                                   BUSTER_ARRAY_LENGTH(high_variable_shift_operands));
                                }
                                if (!shift_ok)
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                BusterX86MetadataPhysicalOperand end_branch_operand = codegen_canonical_x64_metadata_relative(0, 32);
                                u32 end_branch_offset = (u32)buffer.count;
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JMP"), &end_branch_operand, 1))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                u32 large_shift_offset = (u32)buffer.count;
                                BusterX86MetadataPhysicalOperand subtract_count_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                    codegen_canonical_x64_metadata_immediate(64, 8),
                                };
                                BusterX86MetadataPhysicalOperand move_large_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(operation == IR_BINARY_SHIFT_LEFT ? X64_REGISTER_R8 : X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_gpr(operation == IR_BINARY_SHIFT_LEFT ? X64_REGISTER_RAX : X64_REGISTER_RDX, 64),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("SUB"), subtract_count_operands,
                                                                          BUSTER_ARRAY_LENGTH(subtract_count_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), move_large_operands,
                                                                          BUSTER_ARRAY_LENGTH(move_large_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                if (operation == IR_BINARY_SHIFT_LEFT)
                                {
                                    BusterX86MetadataPhysicalOperand zero_high_operands[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    };
                                    BusterX86MetadataPhysicalOperand move_saved_low_operands[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64),
                                    };
                                    BusterX86MetadataPhysicalOperand high_shift_operands[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 8),
                                    };
                                    if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), move_saved_low_operands,
                                                                              BUSTER_ARRAY_LENGTH(move_saved_low_operands)) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), zero_high_operands,
                                                                              BUSTER_ARRAY_LENGTH(zero_high_operands)) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("SHL"), high_shift_operands,
                                                                              BUSTER_ARRAY_LENGTH(high_shift_operands)))
                                    {
                                        result.error = buffer.error;
                                        return result;
                                    }
                                }
                                else
                                {
                                    if (!codegen_canonical_x64_metadata_emit(&buffer,
                                                                              operation == IR_BINARY_SIGNED_SHIFT_RIGHT ? S8("SAR") : S8("SHR"),
                                                                              variable_shift_operands,
                                                                              BUSTER_ARRAY_LENGTH(variable_shift_operands)))
                                    {
                                        result.error = buffer.error;
                                        return result;
                                    }
                                    if (operation == IR_BINARY_SIGNED_SHIFT_RIGHT)
                                    {
                                        BusterX86MetadataPhysicalOperand sign_high_operands[2] = {
                                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                            codegen_canonical_x64_metadata_immediate(63, 8),
                                        };
                                        if (!codegen_canonical_x64_metadata_emit(&buffer, S8("SAR"), sign_high_operands,
                                                                                  BUSTER_ARRAY_LENGTH(sign_high_operands)))
                                        {
                                            result.error = buffer.error;
                                            return result;
                                        }
                                    }
                                    else
                                    {
                                        BusterX86MetadataPhysicalOperand zero_high_operands[2] = {
                                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                        };
                                        if (!codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), zero_high_operands,
                                                                                  BUSTER_ARRAY_LENGTH(zero_high_operands)))
                                        {
                                            result.error = buffer.error;
                                            return result;
                                        }
                                    }
                                }
                                u32 end_shift_offset = (u32)buffer.count;
                                if (buffer.error == CODEGEN_ERROR_NONE && large_branch_offset + 6 <= buffer.count && end_branch_offset + 5 <= buffer.count)
                                {
                                    s32 large_delta = (s32)(large_shift_offset - (large_branch_offset + 6));
                                    s32 end_delta = (s32)(end_shift_offset - (end_branch_offset + 5));
                                    memcpy(buffer.bytes + large_branch_offset + 2, &large_delta, sizeof(large_delta));
                                    memcpy(buffer.bytes + end_branch_offset + 1, &end_delta, sizeof(end_delta));
                                }
                                else if (buffer.error == CODEGEN_ERROR_NONE)
                                {
                                    buffer.error = CODEGEN_ERROR_CAPACITY;
                                }
                                if (buffer.error != CODEGEN_ERROR_NONE)
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            else if (operation == IR_BINARY_INTEGER_EQUAL || operation == IR_BINARY_INTEGER_NOT_EQUAL)
                            {
                                BusterX86MetadataPhysicalOperand low_xor_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                };
                                BusterX86MetadataPhysicalOperand high_xor_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSI, 64),
                                };
                                BusterX86MetadataPhysicalOperand or_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                };
                                BusterX86MetadataPhysicalOperand set_operands[] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 8),
                                };
                                BusterX86MetadataPhysicalOperand widen_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 8),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), low_xor_operands,
                                                                          BUSTER_ARRAY_LENGTH(low_xor_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), high_xor_operands,
                                                                          BUSTER_ARRAY_LENGTH(high_xor_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("OR"), or_operands,
                                                                          BUSTER_ARRAY_LENGTH(or_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer,
                                                                          operation == IR_BINARY_INTEGER_EQUAL ? S8("SETZ") : S8("SETNZ"),
                                                                          set_operands, BUSTER_ARRAY_LENGTH(set_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("MOVZX"), widen_operands,
                                                                          BUSTER_ARRAY_LENGTH(widen_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                c_x64_store_result(&emitter, result_displacement);
                                instruction_id = instruction->next;
                                continue;
                            }
                            else if (operation >= IR_BINARY_SIGNED_LESS && operation <= IR_BINARY_UNSIGNED_GREATER_EQUAL)
                            {
                                bool less = operation == IR_BINARY_SIGNED_LESS || operation == IR_BINARY_SIGNED_LESS_EQUAL ||
                                            operation == IR_BINARY_UNSIGNED_LESS || operation == IR_BINARY_UNSIGNED_LESS_EQUAL;
                                bool inclusive = operation == IR_BINARY_SIGNED_LESS_EQUAL || operation == IR_BINARY_SIGNED_GREATER_EQUAL ||
                                                 operation == IR_BINARY_UNSIGNED_LESS_EQUAL || operation == IR_BINARY_UNSIGNED_GREATER_EQUAL;
                                bool signed_compare = operation >= IR_BINARY_SIGNED_LESS && operation <= IR_BINARY_SIGNED_GREATER_EQUAL;
                                u8 high_condition = less ? (signed_compare ? 0x9c : 0x92) : (signed_compare ? 0x9f : 0x97);
                                u8 low_condition = less ? (inclusive ? 0x96 : 0x92) : (inclusive ? 0x93 : 0x97);
                                String8 high_condition_mnemonic = high_condition == 0x9c ? S8("SETL")
                                                                    : high_condition == 0x9f ? S8("SETNLE")
                                                                    : high_condition == 0x92 ? S8("SETB")
                                                                                             : S8("SETNBE");
                                String8 low_condition_mnemonic = low_condition == 0x92 ? S8("SETB")
                                                                   : low_condition == 0x96 ? S8("SETBE")
                                                                   : low_condition == 0x93 ? S8("SETNB")
                                                                                           : S8("SETNBE");
                                BusterX86MetadataPhysicalOperand high_compare[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RSI, 64),
                                };
                                BusterX86MetadataPhysicalOperand high_condition_set =
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 8);
                                BusterX86MetadataPhysicalOperand high_equal_set =
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R10, 8);
                                BusterX86MetadataPhysicalOperand low_compare[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                };
                                BusterX86MetadataPhysicalOperand low_condition_set =
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 8);
                                BusterX86MetadataPhysicalOperand high_condition_widen[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 32),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 8),
                                };
                                BusterX86MetadataPhysicalOperand high_equal_widen[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R10, 32),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R10, 8),
                                };
                                BusterX86MetadataPhysicalOperand low_condition_widen[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 8),
                                };
                                BusterX86MetadataPhysicalOperand combine_and[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R10, 32),
                                };
                                BusterX86MetadataPhysicalOperand combine_or[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_R9, 32),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("CMP"), high_compare,
                                                                          BUSTER_ARRAY_LENGTH(high_compare)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, high_condition_mnemonic,
                                                                          &high_condition_set, 1) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("SETZ"), &high_equal_set, 1) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("CMP"), low_compare,
                                                                          BUSTER_ARRAY_LENGTH(low_compare)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, low_condition_mnemonic,
                                                                          &low_condition_set, 1) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("MOVZX"), high_condition_widen,
                                                                          BUSTER_ARRAY_LENGTH(high_condition_widen)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("MOVZX"), high_equal_widen,
                                                                          BUSTER_ARRAY_LENGTH(high_equal_widen)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("MOVZX"), low_condition_widen,
                                                                          BUSTER_ARRAY_LENGTH(low_condition_widen)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("AND"), combine_and,
                                                                          BUSTER_ARRAY_LENGTH(combine_and)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("OR"), combine_or,
                                                                          BUSTER_ARRAY_LENGTH(combine_or)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                c_x64_store_result(&emitter, result_displacement);
                                instruction_id = instruction->next;
                                continue;
                            }
                            else
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            c_x64_store_result(&emitter, result_displacement);
                            c_x64_store_high_rdx(&emitter, result_displacement);
                            instruction_id = instruction->next;
                            continue;
                        }
                        bool wide = codegen_canonical_register_is_64_bit(program, operand_type);
                        c_x64_load(&emitter, 0x85, instruction->operands[0]);
                        c_x64_load(&emitter, 0x8d, instruction->operands[1]);
                        if (buffer.error != CODEGEN_ERROR_NONE)
                        {
                            result.error = buffer.error;
                            return result;
                        }
                        u16 scalar_width = wide ? 64 : 32;
                        BusterX86MetadataPhysicalOperand scalar_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, scalar_width),
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, scalar_width),
                        };
                        switch (instruction->binary_operation)
                        {
                        case IR_BINARY_INTEGER_ADD:
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("ADD"), scalar_operands,
                                                                      BUSTER_ARRAY_LENGTH(scalar_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            break;
                        case IR_BINARY_INTEGER_SUBTRACT:
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("SUB"), scalar_operands,
                                                                      BUSTER_ARRAY_LENGTH(scalar_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            break;
                        case IR_BINARY_INTEGER_MULTIPLY:
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("IMUL"), scalar_operands,
                                                                      BUSTER_ARRAY_LENGTH(scalar_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            break;
                        case IR_BINARY_SIGNED_DIVIDE:
                        case IR_BINARY_SIGNED_REMAINDER:
                            if (!codegen_canonical_x64_metadata_emit(&buffer, wide ? S8("CQO") : S8("CDQ"), 0, 0) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("IDIV"), &scalar_operands[1], 1))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            if (instruction->binary_operation == IR_BINARY_SIGNED_REMAINDER)
                            {
                                BusterX86MetadataPhysicalOperand remainder_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, scalar_width),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, scalar_width),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), remainder_operands,
                                                                          BUSTER_ARRAY_LENGTH(remainder_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            break;
                        case IR_BINARY_UNSIGNED_DIVIDE:
                        case IR_BINARY_UNSIGNED_REMAINDER:
                            {
                                BusterX86MetadataPhysicalOperand zero_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, scalar_width),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, scalar_width),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), zero_operands,
                                                                          BUSTER_ARRAY_LENGTH(zero_operands)) ||
                                    !codegen_canonical_x64_metadata_emit(&buffer, S8("DIV"), &scalar_operands[1], 1))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                if (instruction->binary_operation == IR_BINARY_UNSIGNED_REMAINDER)
                                {
                                    BusterX86MetadataPhysicalOperand remainder_operands[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, scalar_width),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, scalar_width),
                                    };
                                    if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), remainder_operands,
                                                                              BUSTER_ARRAY_LENGTH(remainder_operands)))
                                    {
                                        result.error = buffer.error;
                                        return result;
                                    }
                                }
                            }
                            break;
                        case IR_BINARY_SHIFT_LEFT:
                        case IR_BINARY_SIGNED_SHIFT_RIGHT:
                        case IR_BINARY_UNSIGNED_SHIFT_RIGHT:
                            {
                                BusterX86MetadataPhysicalOperand shift_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, scalar_width),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 8),
                                };
                                String8 shift_mnemonic = instruction->binary_operation == IR_BINARY_SHIFT_LEFT ? S8("SHL")
                                                      : instruction->binary_operation == IR_BINARY_SIGNED_SHIFT_RIGHT ? S8("SAR")
                                                                                                                       : S8("SHR");
                                if (!codegen_canonical_x64_metadata_emit(&buffer, shift_mnemonic, shift_operands,
                                                                          BUSTER_ARRAY_LENGTH(shift_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            break;
                        case IR_BINARY_INTEGER_BITWISE_AND:
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("AND"), scalar_operands,
                                                                      BUSTER_ARRAY_LENGTH(scalar_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            break;
                        case IR_BINARY_INTEGER_BITWISE_OR:
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("OR"), scalar_operands,
                                                                      BUSTER_ARRAY_LENGTH(scalar_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            break;
                        case IR_BINARY_INTEGER_BITWISE_XOR:
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("XOR"), scalar_operands,
                                                                      BUSTER_ARRAY_LENGTH(scalar_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            break;
                        case IR_BINARY_INTEGER_EQUAL:
                        case IR_BINARY_POINTER_EQUAL:
                        case IR_BINARY_INTEGER_NOT_EQUAL:
                        case IR_BINARY_POINTER_NOT_EQUAL:
                        case IR_BINARY_SIGNED_LESS:
                        case IR_BINARY_SIGNED_LESS_EQUAL:
                        case IR_BINARY_SIGNED_GREATER:
                        case IR_BINARY_SIGNED_GREATER_EQUAL:
                        case IR_BINARY_UNSIGNED_LESS:
                        case IR_BINARY_UNSIGNED_LESS_EQUAL:
                        case IR_BINARY_UNSIGNED_GREATER:
                        case IR_BINARY_UNSIGNED_GREATER_EQUAL:
                        {
                            String8 condition_mnemonic = instruction->binary_operation == IR_BINARY_INTEGER_EQUAL ||
                                                                  instruction->binary_operation == IR_BINARY_POINTER_EQUAL ? S8("SETZ")
                                      : instruction->binary_operation == IR_BINARY_INTEGER_NOT_EQUAL ||
                                                instruction->binary_operation == IR_BINARY_POINTER_NOT_EQUAL ? S8("SETNZ")
                                      : instruction->binary_operation == IR_BINARY_SIGNED_LESS ? S8("SETL")
                                      : instruction->binary_operation == IR_BINARY_SIGNED_LESS_EQUAL ? S8("SETLE")
                                      : instruction->binary_operation == IR_BINARY_SIGNED_GREATER ? S8("SETNLE")
                                      : instruction->binary_operation == IR_BINARY_SIGNED_GREATER_EQUAL ? S8("SETNL")
                                      : instruction->binary_operation == IR_BINARY_UNSIGNED_LESS ? S8("SETB")
                                      : instruction->binary_operation == IR_BINARY_UNSIGNED_LESS_EQUAL ? S8("SETBE")
                                      : instruction->binary_operation == IR_BINARY_UNSIGNED_GREATER ? S8("SETNBE")
                                      : instruction->binary_operation == IR_BINARY_UNSIGNED_GREATER_EQUAL ? S8("SETNB")
                                                                                                           : (String8){0};
                            BusterX86MetadataPhysicalOperand set_operands[] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 8),
                            };
                            BusterX86MetadataPhysicalOperand widen_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 8),
                            };
                            if (!condition_mnemonic.length ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("CMP"), scalar_operands,
                                                                      BUSTER_ARRAY_LENGTH(scalar_operands)) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, condition_mnemonic, set_operands,
                                                                      BUSTER_ARRAY_LENGTH(set_operands)) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("MOVZX"), widen_operands,
                                                                      BUSTER_ARRAY_LENGTH(widen_operands)))
                            {
                                result.error = buffer.error ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            break;
                        }
                        default:
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        c_x64_store_result(&emitter, result_displacement);
                    }
                    else if (instruction->opcode == IR_OPCODE_LABEL_ADDRESS)
                    {
                        if (instruction->target_count != 1)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        BusterX86MetadataPhysicalOperand label_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            codegen_canonical_x64_metadata_rip_relative(64, 0),
                        };
                        u32 label_offset = (u32)buffer.count;
                        if (!codegen_canonical_x64_metadata_emit(&buffer, S8("LEA"), label_operands, BUSTER_ARRAY_LENGTH(label_operands)))
                        {
                            result.error = buffer.error;
                            return result;
                        }
                        if (!c_branch_patch_push(&emitter, (CCanonicalBranchPatch){
                            .target = instruction->targets[0],
                            .offset = label_offset + 3,
                            .label_address = true,
                            }))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        c_x64_store_result(&emitter, result_displacement);
                    }
                    else if (instruction->opcode == IR_OPCODE_BRANCH)
                    {
                        BusterX86MetadataPhysicalOperand branch_operand = codegen_canonical_x64_metadata_relative(0, 32);
                        u32 branch_offset = (u32)buffer.count;
                        if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JMP"), &branch_operand, 1))
                        {
                            result.error = buffer.error;
                            return result;
                        }
                        if (!c_branch_patch_push(&emitter, (CCanonicalBranchPatch){
                            .target = instruction->targets[0],
                            .offset = branch_offset + 1,
                            }))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_BRANCH_IF)
                    {
                        c_x64_load(&emitter, 0x85, instruction->operands[0]);
                        BusterX86MetadataPhysicalOperand test_operands[2] = {
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                            codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                        };
                        BusterX86MetadataPhysicalOperand branch_operand = codegen_canonical_x64_metadata_relative(0, 32);
                        if (buffer.error || !codegen_canonical_x64_metadata_emit(&buffer, S8("TEST"), test_operands,
                                                                                   BUSTER_ARRAY_LENGTH(test_operands)))
                        {
                            result.error = buffer.error;
                            return result;
                        }
                        u32 conditional_branch_offset = (u32)buffer.count;
                        if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JNZ"), &branch_operand, 1))
                        {
                            result.error = buffer.error;
                            return result;
                        }
                        if (!c_branch_patch_push(&emitter, (CCanonicalBranchPatch){
                            .target = instruction->targets[0],
                            .offset = conditional_branch_offset + 2,
                            }))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        u32 fallthrough_branch_offset = (u32)buffer.count;
                        if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JMP"), &branch_operand, 1))
                        {
                            result.error = buffer.error;
                            return result;
                        }
                        if (!c_branch_patch_push(&emitter, (CCanonicalBranchPatch){
                            .target = instruction->targets[1],
                            .offset = fallthrough_branch_offset + 1,
                            }))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_INDIRECT_BRANCH)
                    {
                        if (instruction->operand_count != 1)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        c_x64_load(&emitter, 0x85, instruction->operands[0]);
                        BusterX86MetadataPhysicalOperand branch_operand = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64);
                        if (buffer.error || !codegen_canonical_x64_metadata_emit(&buffer, S8("JMP"), &branch_operand, 1))
                        {
                            result.error = buffer.error;
                            return result;
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_SWITCH)
                    {
                        if (instruction->target_count != instruction->immediate_count + 1)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        c_x64_load(&emitter, 0x85, instruction->operands[0]);
                        for (u32 case_index = 0; case_index < instruction->immediate_count; case_index += 1)
                        {
                            BusterX86MetadataPhysicalOperand move_case_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                codegen_canonical_x64_metadata_unsigned_immediate(instruction->immediates[case_index], 64),
                            };
                            BusterX86MetadataPhysicalOperand compare_case_operands[2] = {
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                            };
                            BusterX86MetadataPhysicalOperand case_branch_operand = codegen_canonical_x64_metadata_relative(0, 32);
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), move_case_operands,
                                                                       BUSTER_ARRAY_LENGTH(move_case_operands)) ||
                                !codegen_canonical_x64_metadata_emit(&buffer, S8("CMP"), compare_case_operands,
                                                                       BUSTER_ARRAY_LENGTH(compare_case_operands)))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            u32 case_branch_offset = (u32)buffer.count;
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JZ"), &case_branch_operand, 1))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            if (!c_branch_patch_push(&emitter, (CCanonicalBranchPatch){
                                .target = instruction->targets[case_index],
                                .offset = case_branch_offset + 2,
                                .conditional = true,
                                }))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                        }
                        BusterX86MetadataPhysicalOperand default_branch_operand = codegen_canonical_x64_metadata_relative(0, 32);
                        u32 default_branch_offset = (u32)buffer.count;
                        if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JMP"), &default_branch_operand, 1))
                        {
                            result.error = buffer.error;
                            return result;
                        }
                        if (!c_branch_patch_push(&emitter, (CCanonicalBranchPatch){
                            .target = instruction->targets[instruction->target_count - 1],
                            .offset = default_branch_offset + 1,
                            }))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_SIMD)
                    {
                        if (!codegen_canonical_x64_simd_operation(&buffer, instruction, value_offsets, canonical_x64_frame_base_offset, target))
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        x64_upper_vector_dirty = true;
                        x64_last_wide_vector_result = IR_VALUE_ID_INVALID;
                        x64_last_wide_vector_size = 0;
                        result.statistics.simd_operation_count += 1;
                    }
                    else if (instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY)
                    {
                        IrInstructionExtra asm_extra = ir_instruction_extra(function, ir_instruction_self_id(function, instruction));
                        bool cpuid = string_equal(asm_extra.literal, S8("cpuid"));
                        bool xgetbv = string_equal(asm_extra.literal, S8("xgetbv"));
                        bool undefined = string_equal(asm_extra.literal, S8("ud2"));
                        bool no_instruction = !asm_extra.literal.length;
                        bool nop = string_equal(asm_extra.literal, S8("nop"));
                        bool pause = string_equal(asm_extra.literal, S8("pause"));
                        bool interrupt = string_equal(asm_extra.literal, S8("int3"));
                        u32 jump_target_index = 0;
                        bool jump_label = codegen_inline_assembly_jump_target(function, instruction, asm_extra.literal, S8("jmp %l"), &jump_target_index);
                        bool operandless = undefined || nop || pause || interrupt;
                        bool ordinary = !cpuid && !xgetbv && !operandless && !no_instruction && !jump_label;
                        if ((operandless && instruction->operand_count) || (jump_label && instruction->target_count < 2) ||
                            instruction->operand_count != instruction->immediate_count || asm_extra.operand_name_count != instruction->operand_count ||
                            (instruction->operand_count && (!instruction->operands || !instruction->immediates || !asm_extra.operand_names)) ||
                            (asm_extra.clobber_count && !asm_extra.clobbers))
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        for (u32 clobber_index = 0; clobber_index < asm_extra.clobber_count; clobber_index += 1)
                        {
                            String8 clobber = asm_extra.clobbers[clobber_index];
                            bool accepted = string_equal(clobber, S8("memory")) || string_equal(clobber, S8("cc")) ||
                                            string_equal(clobber, S8("rax")) || string_equal(clobber, S8("eax")) || string_equal(clobber, S8("ax")) ||
                                            string_equal(clobber, S8("al")) || string_equal(clobber, S8("rbx")) || string_equal(clobber, S8("ebx")) ||
                                            string_equal(clobber, S8("bx")) || string_equal(clobber, S8("bl")) || string_equal(clobber, S8("rcx")) ||
                                            string_equal(clobber, S8("ecx")) || string_equal(clobber, S8("cx")) || string_equal(clobber, S8("cl")) ||
                                            string_equal(clobber, S8("rdx")) || string_equal(clobber, S8("edx")) || string_equal(clobber, S8("dx")) ||
                                            string_equal(clobber, S8("dl")) || string_equal(clobber, S8("rsi")) || string_equal(clobber, S8("esi")) ||
                                            string_equal(clobber, S8("si")) || string_equal(clobber, S8("sil")) || string_equal(clobber, S8("rdi")) ||
                                            string_equal(clobber, S8("edi")) || string_equal(clobber, S8("di")) || string_equal(clobber, S8("dil"));
                            if (!accepted && clobber.length >= 2 && clobber.pointer[0] == 'r')
                            {
                                u64 number = 0;
                                String8 suffix = {
                                    .pointer = clobber.pointer + 1,
                                    .length = clobber.length - 1,
                                };
                                accepted = codegen_decimal_number(suffix, &number) && number >= 8 && number <= 11;
                            }
                            if (!accepted)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                        }
                        bool indirect_operands = false;
                        bool used_registers[12] = {0};
                        bool clobbered_registers[12] = {0};
                        bool* indirect = arena_allocate(arena, bool, instruction->operand_count ? instruction->operand_count : 1);
                        X64Register* asm_registers = arena_allocate(arena, X64Register, instruction->operand_count ? instruction->operand_count : 1);
                        for (u32 clobber_index = 0; clobber_index < asm_extra.clobber_count; clobber_index += 1)
                        {
                            X64Register clobber_register = X64_REGISTER_RAX;
                            if (codegen_inline_assembly_clobber_register(asm_extra.clobbers[clobber_index], &clobber_register))
                            {
                                clobbered_registers[clobber_register] = true;
                                used_registers[clobber_register] = true;
                            }
                        }
                        // Reserve every fixed-register operand before assigning
                        // any generic r operand.  The allocation order is not
                        // part of GNU asm's constraint semantics: a generic
                        // operand appearing before an a/b/c/d operand must not
                        // steal that fixed register and make a valid operand
                        // list appear to alias two outputs.
                        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                        {
                            u64 constraint = instruction->immediates[operand_index];
                            if (!codegen_inline_assembly_constraint_shape_valid(constraint, operand_index, instruction->operand_count, 0) ||
                                (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) > IR_INLINE_ASSEMBLY_CONSTRAINT_R)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            if ((constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0)
                            {
                                u32 match_index = IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(constraint);
                                u64 output_constraint = instruction->immediates[match_index];
                                if ((output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) == 0 ||
                                    (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) != 0 ||
                                    (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) !=
                                        (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK))
                                {
                                    result.error = CODEGEN_ERROR_INVALID_IR;
                                    return result;
                                }
                                for (u32 previous_index = 0; previous_index < operand_index; previous_index += 1)
                                {
                                    u64 previous_constraint = instruction->immediates[previous_index];
                                    if ((previous_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0 &&
                                        IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(previous_constraint) == match_index)
                                    {
                                        result.error = CODEGEN_ERROR_INVALID_IR;
                                        return result;
                                    }
                                }
                            }
                            X64Register fixed_register = X64_REGISTER_RAX;
                            if (codegen_inline_assembly_constraint_register(constraint, &fixed_register))
                            {
                                if (clobbered_registers[fixed_register])
                                {
                                    result.error = CODEGEN_ERROR_INVALID_IR;
                                    return result;
                                }
                                used_registers[fixed_register] = true;
                            }
                        }
                        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                        {
                            IrValueId operand = instruction->operands[operand_index];
                            if (operand.value >= function->value_count || function->values[operand.value].definition.value >= function->instruction_count)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrInstruction* definition = function->instructions + function->values[operand.value].definition.value;
                            indirect[operand_index] = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                                       definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE;
                            indirect_operands |= indirect[operand_index];
                        }
                        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                        {
                            u64 constraint = instruction->immediates[operand_index];
                            u64 constraint_index = constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK;
                            if (constraint_index > IR_INLINE_ASSEMBLY_CONSTRAINT_R)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrValueId operand = instruction->operands[operand_index];
                            if (operand.value >= function->value_count || function->values[operand.value].definition.value >= function->instruction_count)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrInstruction* definition = function->instructions + function->values[operand.value].definition.value;
                            indirect[operand_index] = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                                       definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE;
                            indirect_operands |= indirect[operand_index];
                            IrType* operand_type = ir_type_from_id(&program->types, function->values[operand.value].canonical_type);
                            if (ordinary && (codegen_inline_assembly_type_class(operand_type) == IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID ||
                                             !codegen_canonical_x64_asm_memory_width((u32)operand_type->layout.size)))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            if ((constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0)
                            {
                                if (codegen_inline_assembly_type_class(operand_type) == IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID)
                                {
                                    result.error = CODEGEN_ERROR_INVALID_IR;
                                    return result;
                                }
                                u32 match_index = IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(constraint);
                                IrValueId output = instruction->operands[match_index];
                                u64 output_constraint = instruction->immediates[match_index];
                                IrType* output_type = ir_type_from_id(&program->types, function->values[output.value].canonical_type);
                                if ((output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) == 0 ||
                                    (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) != 0 ||
                                    (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) !=
                                        (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) ||
                                    !codegen_inline_assembly_types_compatible(output_type, operand_type))
                                {
                                    result.error = CODEGEN_ERROR_INVALID_IR;
                                    return result;
                                }
                                for (u32 previous_index = 0; previous_index < operand_index; previous_index += 1)
                                {
                                    u64 previous_constraint = instruction->immediates[previous_index];
                                    if ((previous_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0 &&
                                        IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(previous_constraint) == match_index)
                                    {
                                        result.error = CODEGEN_ERROR_INVALID_IR;
                                        return result;
                                    }
                                }
                                asm_registers[operand_index] = asm_registers[match_index];
                                continue;
                            }
                            X64Register register_index = X64_REGISTER_RAX;
                            bool is_fixed_register = codegen_inline_assembly_constraint_register(constraint, &register_index);
                            if (is_fixed_register)
                            {
                                if (clobbered_registers[register_index])
                                {
                                    result.error = CODEGEN_ERROR_INVALID_IR;
                                    return result;
                                }
                                for (u32 previous_index = 0; previous_index < operand_index; previous_index += 1)
                                {
                                    if (asm_registers[previous_index] == register_index)
                                    {
                                        u64 previous_constraint = instruction->immediates[previous_index];
                                        bool current_output = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) != 0;
                                        bool previous_output = (previous_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) != 0;
                                        bool current_read_write = (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) != 0;
                                        bool previous_read_write = (previous_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) != 0;
                                        bool output_input_pair = current_output != previous_output &&
                                                                 ((current_output && !current_read_write) || (previous_output && !previous_read_write));
                                        if (!output_input_pair)
                                        {
                                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                            return result;
                                        }
                                    }
                                }
                                used_registers[register_index] = true;
                            }
                            else
                            {
                                X64Register const* candidates = result.abi == CODEGEN_ABI_X86_64_WINDOWS ? codegen_x64_asm_windows_registers
                                                                                                           : codegen_x64_asm_system_v_registers;
                                u32 candidate_count = result.abi == CODEGEN_ABI_X86_64_WINDOWS ? BUSTER_ARRAY_LENGTH(codegen_x64_asm_windows_registers)
                                                                                                 : BUSTER_ARRAY_LENGTH(codegen_x64_asm_system_v_registers);
                                bool found = false;
                                for (u32 candidate_index = 0; candidate_index < candidate_count; candidate_index += 1)
                                {
                                    X64Register candidate = candidates[candidate_index];
                                    if (!used_registers[candidate] && !(indirect_operands && candidate == X64_REGISTER_R11))
                                    {
                                        register_index = candidate;
                                        used_registers[candidate] = true;
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                            }
                            asm_registers[operand_index] = register_index;
                        }
                        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                        {
                            u64 constraint = instruction->immediates[operand_index];
                            if ((constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) && !(constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE))
                            {
                                continue;
                            }
                            IrValueId input = instruction->operands[operand_index];
                            IrType* input_type = ir_type_from_id(&program->types, function->values[input.value].canonical_type);
                            if (!input_type || !input_type->layout.resolved || input_type->layout.size > UINT32_MAX ||
                                !codegen_canonical_x64_asm_memory_width((u32)input_type->layout.size))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            if (indirect[operand_index])
                            {
                                codegen_canonical_x64_asm_load(&buffer, X64_REGISTER_R11, X64_REGISTER_RBP,
                                                              (u32)c_x64_frame_displacement(&emitter, value_offsets[input.value]), 8);
                                codegen_canonical_x64_asm_load(&buffer, asm_registers[operand_index], X64_REGISTER_R11, 0,
                                                              (u32)input_type->layout.size);
                            }
                            else
                            {
                                codegen_canonical_x64_asm_load(&buffer, asm_registers[operand_index], X64_REGISTER_RBP,
                                                              (u32)c_x64_frame_displacement(&emitter, value_offsets[input.value]),
                                                              (u32)input_type->layout.size);
                            }
                        }
                        if (cpuid || undefined || nop || interrupt)
                        {
                            String8 mnemonic = cpuid ? S8("CPUID") : undefined ? S8("UD2") : nop ? S8("NOP") : S8("INT3");
                            if (!codegen_canonical_x64_metadata_emit(&buffer, mnemonic, 0, 0))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                        else if (xgetbv)
                        {
                            AssemblyEncodeResult encoded = assembly_encode(arena, asm_extra.literal,
                                                                            (AssemblyEncodeOptions){
                                                                                .target = target,
                                                                                .syntax = codegen_inline_assembly_syntax(options),
                                                                            });
                            if (encoded.diagnostic_count || encoded.relocation_count)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            for (u32 symbol_index = 0; symbol_index < encoded.symbol_count; symbol_index += 1)
                            {
                                if (!encoded.symbols[symbol_index].defined)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                            }
                            u8* encoded_bytes = 0;
                            if (!codegen_buffer_reserve(&buffer, encoded.bytes.length, &encoded_bytes))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                            if (encoded.bytes.length)
                            {
                                memcpy(encoded_bytes, encoded.bytes.pointer, encoded.bytes.length);
                            }
                        }
                        else if (pause)
                        {
                            // PAUSE is an architectural baseline instruction
                            // even though the imported ISA row is tagged with
                            // the PAUSE feature.  Inline asm has historically
                            // accepted it unconditionally; provide that
                            // explicit feature authorization while still
                            // emitting through the canonical metadata bridge.
                            String8 feature_names[] = {S8("pause")};
                            if (!codegen_canonical_x64_metadata_emit_features(
                                    &buffer, S8("PAUSE"), 0, 0,
                                    (BusterX86MetadataFeatureInput){.names = feature_names, .count = BUSTER_ARRAY_LENGTH(feature_names)}))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                        }
                        else if (ordinary)
                        {
                            String8 source = {0};
                            if (!codegen_inline_assembly_resolve_template(arena, program, function, instruction, asm_extra, asm_registers,
                                                                           codegen_inline_assembly_syntax(options), &source))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            AssemblyEncodeResult encoded = assembly_encode(arena, source,
                                                                            (AssemblyEncodeOptions){
                                                                                .target = target,
                                                                                .syntax = codegen_inline_assembly_syntax(options),
                                                                            });
                            if (encoded.diagnostic_count || encoded.relocation_count)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            for (u32 symbol_index = 0; symbol_index < encoded.symbol_count; symbol_index += 1)
                            {
                                if (!encoded.symbols[symbol_index].defined)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                            }
                            u8* encoded_bytes = 0;
                            if (!codegen_buffer_reserve(&buffer, encoded.bytes.length, &encoded_bytes))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                            if (encoded.bytes.length)
                            {
                                memcpy(encoded_bytes, encoded.bytes.pointer, encoded.bytes.length);
                            }
                        }
                        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                        {
                            u64 constraint = instruction->immediates[operand_index];
                            if (!(constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT))
                            {
                                continue;
                            }
                            IrValueId place_id = instruction->operands[operand_index];
                            if (place_id.value >= function->value_count)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrValue* place = function->values + place_id.value;
                            if (place->definition.value >= function->instruction_count)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrInstruction* definition = function->instructions + place->definition.value;
                            IrType* output_type = ir_type_from_id(&program->types, place->canonical_type);
                            if (!output_type || !output_type->layout.resolved || output_type->layout.size > UINT32_MAX ||
                                !codegen_canonical_x64_asm_memory_width((u32)output_type->layout.size))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            bool output_indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                                   definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE;
                            if (output_indirect)
                            {
                                codegen_canonical_x64_asm_load(&buffer, X64_REGISTER_R11, X64_REGISTER_RBP,
                                                              (u32)c_x64_frame_displacement(&emitter, value_offsets[place_id.value]), 8);
                                codegen_canonical_x64_asm_store(&buffer, X64_REGISTER_R11, asm_registers[operand_index], 0,
                                                               (u32)output_type->layout.size);
                            }
                            else
                            {
                                codegen_canonical_x64_asm_store(&buffer, X64_REGISTER_RBP, asm_registers[operand_index],
                                                               (u32)c_x64_frame_displacement(&emitter, value_offsets[place_id.value]),
                                                               (u32)output_type->layout.size);
                            }
                        }
                        // Inline assembly may use RBX for a fixed b operand or
                        // declare it clobbered.  Restore the ABI-owned value
                        // before either falling through or taking an asm-goto
                        // edge; otherwise a successor can observe the clobber
                        // or call with an invalid callee-saved register.
                        c_x64_restore_rbx(&emitter);
                        if (jump_label)
                        {
                            BusterX86MetadataPhysicalOperand jump_operand = codegen_canonical_x64_metadata_relative(0, 32);
                            u32 jump_offset = (u32)buffer.count;
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JMP"), &jump_operand, 1))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            if (!c_branch_patch_push(&emitter, (CCanonicalBranchPatch){
                                .target = instruction->targets[jump_target_index],
                                .offset = jump_offset + 1,
                                }))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                        }
                        else if (instruction->target_count)
                        {
                            BusterX86MetadataPhysicalOperand jump_operand = codegen_canonical_x64_metadata_relative(0, 32);
                            u32 jump_offset = (u32)buffer.count;
                            if (!codegen_canonical_x64_metadata_emit(&buffer, S8("JMP"), &jump_operand, 1))
                            {
                                result.error = buffer.error;
                                return result;
                            }
                            if (!c_branch_patch_push(&emitter, (CCanonicalBranchPatch){
                                .target = instruction->targets[0],
                                .offset = jump_offset + 1,
                                }))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_DEBUG_TRAP)
                    {
                        if (!codegen_canonical_x64_metadata_emit(&buffer, S8("INT3"), 0, 0))
                        {
                            result.error = buffer.error;
                            return result;
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_UNREACHABLE)
                    {
                        if (!codegen_canonical_x64_metadata_emit(&buffer, S8("UD2"), 0, 0))
                        {
                            result.error = buffer.error;
                            return result;
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_RETURN)
                    {
                        if (instruction->operand_count)
                        {
                            IrValueId return_value = instruction->operands[0];
                            IrType* return_type = ir_type_from_id(&program->types, function->values[return_value.value].canonical_type);
                            u32 return_parts = 0;
                            bool aggregate_return =
                                codegen_canonical_integer_aggregate_parts(program, function->values[return_value.value].canonical_type, &return_parts);
                            CodegenCanonicalAbiValue aggregate_return_abi =
                                codegen_canonical_aggregate_abi(program, function->values[return_value.value].canonical_type, result.abi, true, false);
                            if (codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, function->values[return_value.value].canonical_type))
                            {
                                if (result.abi != CODEGEN_ABI_X86_64_SYSTEM_V ||
                                    !codegen_canonical_x64_type_is_f80_x87_shape_cached(f80_cache, program, function->values[return_value.value].canonical_type) ||
                                    !codegen_canonical_x64_abi_is_f80_result(return_type, &aggregate_return_abi) || x87_stack_depth != 0)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                codegen_canonical_x64_x87_memory(&buffer, false, X64_REGISTER_RBP,
                                                                 c_x64_frame_displacement(&emitter, value_offsets[return_value.value]));
                                x87_stack_depth += 1;
                                c_x64_restore_rbx(&emitter);
                                codegen_canonical_x64_emit_return(&buffer, frame_size, result.abi, windows_dynamic_stack);
                                x87_stack_depth = 0; // the RET terminates this path; the next block starts with an empty stack.
                                instruction_id = instruction->next;
                                continue;
                            }
                            if (aggregate_return_abi.part_count && !aggregate_return_abi.indirect)
                            {
                                aggregate_return = true;
                                return_parts = aggregate_return_abi.part_count;
                            }
                            if (result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && aggregate_return_abi.part_count && !aggregate_return_abi.indirect &&
                                !aggregate_return_abi.memory)
                            {
                                u32 integer_index = 0;
                                u32 float_index = 0;
                                for (u32 part_index = 0; part_index < aggregate_return_abi.part_count; part_index += 1)
                                {
                                    CodegenCanonicalAbiPart* part = aggregate_return_abi.parts + part_index;
                                    s32 displacement = c_x64_frame_displacement(&emitter, value_offsets[return_value.value]) + (s32)part->value_offset;
                                    if (codegen_canonical_abi_part_is_float(part->abi_class))
                                    {
                                        u32 register_size = 0;
                                        u32 register_count_used = codegen_canonical_x64_vector_part_registers(&target, part->size, &register_size);
                                        // Two vector registers is what a result
                                        // gets, unless the part is one this
                                        // target has to split, which takes as
                                        // many as it takes and starts at zero.
                                        if (!register_count_used || float_index + register_count_used > BUSTER_MAX((u32)2, register_count_used))
                                        {
                                            result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                            return result;
                                        }
                                        for (u32 chunk = 0; chunk < register_count_used; chunk += 1)
                                        {
                                            if (!codegen_canonical_x64_float_memory(&buffer, target, float_index, displacement + (s32)(chunk * register_size),
                                                                                    register_size, false))
                                            {
                                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                                return result;
                                            }
                                            float_index += 1;
                                        }
                                    }
                                    else
                                    {
                                        if (integer_index >= 2)
                                        {
                                            result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                            return result;
                                        }
                                        X64Register result_register = integer_index ? X64_REGISTER_RDX : X64_REGISTER_RAX;
                                        BusterX86MetadataPhysicalOperand load_operands[2] = {
                                            codegen_canonical_x64_metadata_gpr(result_register, 64),
                                            codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, displacement),
                                        };
                                        if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), load_operands,
                                                                                   BUSTER_ARRAY_LENGTH(load_operands)))
                                        {
                                            result.error = buffer.error;
                                            return result;
                                        }
                                        integer_index += 1;
                                    }
                                }
                                c_x64_restore_rbx(&emitter);
                                codegen_canonical_x64_emit_return(&buffer, frame_size, result.abi, windows_dynamic_stack);
                                instruction_id = instruction->next;
                                continue;
                            }
                            if (aggregate_return_abi.indirect)
                            {
                                if (!x64_indirect_return)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                s64 hidden_displacement = codegen_canonical_x64_rebase_frame_displacement(&buffer, hidden_result_displacement,
                                                                                                             canonical_x64_frame_base_offset);
                                BusterX86MetadataPhysicalOperand hidden_load_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                    codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, hidden_displacement),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), hidden_load_operands,
                                                                           BUSTER_ARRAY_LENGTH(hidden_load_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                for (u32 part_index = 0; part_index < return_parts; part_index += 1)
                                {
                                    s64 displacement = c_x64_frame_displacement(&emitter, value_offsets[return_value.value]) + (s32)(part_index * 8);
                                    BusterX86MetadataPhysicalOperand load_part_operands[2] = {
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                        codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, displacement),
                                    };
                                    BusterX86MetadataPhysicalOperand store_part_operands[2] = {
                                        codegen_canonical_x64_metadata_memory_relaxed(X64_REGISTER_RCX, 64, (s64)(part_index * 8)),
                                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                    };
                                    if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), load_part_operands,
                                                                               BUSTER_ARRAY_LENGTH(load_part_operands)) ||
                                        !codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), store_part_operands,
                                                                               BUSTER_ARRAY_LENGTH(store_part_operands)))
                                    {
                                        result.error = buffer.error;
                                        return result;
                                    }
                                }
                                BusterX86MetadataPhysicalOperand move_result_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RCX, 64),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), move_result_operands,
                                                                           BUSTER_ARRAY_LENGTH(move_result_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                                c_x64_restore_rbx(&emitter);
                                codegen_canonical_x64_emit_return(&buffer, frame_size, result.abi, windows_dynamic_stack);
                                instruction_id = instruction->next;
                                continue;
                            }
                            if (return_type && return_type->kind == IR_TYPE_FLOAT)
                            {
                                if (return_type->bit_width != 32 && return_type->bit_width != 64)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                u16 float_width = (u16)return_type->bit_width;
                                BusterX86MetadataPhysicalOperand float_load_operands[2] = {
                                    codegen_canonical_x64_metadata_vector(0, float_width),
                                    codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, float_width,
                                                                            c_x64_frame_displacement(&emitter, value_offsets[return_value.value])),
                                };
                                String8 float_features[] = {return_type->bit_width == 32 ? S8("sse") : S8("sse2")};
                                if (!codegen_canonical_x64_metadata_emit_features(
                                        &buffer, return_type->bit_width == 32 ? S8("MOVSS") : S8("MOVQ"), float_load_operands,
                                        BUSTER_ARRAY_LENGTH(float_load_operands),
                                        (BusterX86MetadataFeatureInput){.names = float_features, .count = BUSTER_ARRAY_LENGTH(float_features)}))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                            else
                            {
                                c_x64_load(&emitter, 0x85, return_value);
                            }
                            if (aggregate_return && return_parts == 2)
                            {
                                BusterX86MetadataPhysicalOperand high_return_operands[2] = {
                                    codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
                                    codegen_canonical_x64_metadata_memory(
                                        X64_REGISTER_RBP, 64, c_x64_frame_displacement(&emitter, value_offsets[return_value.value]) + 8),
                                };
                                if (!codegen_canonical_x64_metadata_emit(&buffer, S8("MOV"), high_return_operands,
                                                                           BUSTER_ARRAY_LENGTH(high_return_operands)))
                                {
                                    result.error = buffer.error;
                                    return result;
                                }
                            }
                        }
                        c_x64_restore_rbx(&emitter);
                        codegen_canonical_x64_emit_return(&buffer, frame_size, result.abi, windows_dynamic_stack);
                    }
                    else
                    {
                        result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                        return result;
                    }
                }
                else
                {
                    u32 result_offset = instruction->result.value == IR_ID_UNDERLYING_INVALID ? 0 : value_offsets[instruction->result.value];
                    if (instruction->opcode == IR_OPCODE_LOCAL)
                    {
                        IrValue* local = function->values + instruction->result.value;
                        u32 local_alignment = local->alignment;
                        codegen_canonical_a64_base_address(&buffer, 9, 28, aligned_local_offsets[instruction->result.value]);
                        a64_emit_constant(&buffer, 10, local_alignment - 1);
                        codegen_emit_u32(&buffer, 0x8b0a0129);
                        a64_emit_constant(&buffer, 10, ~(u64)(local_alignment - 1));
                        codegen_emit_u32(&buffer, 0x8a0a0129);
                        c_a64_store(&emitter, 9, result_offset);
                    }
                    else if (instruction->opcode == IR_OPCODE_STACK_ALLOCATE)
                    {
                        u32 stack_alignment = (u32)instruction->immediates[0];
                        stack_alignment = BUSTER_MAX(stack_alignment, 16);
                        c_a64_load(&emitter, 9, instruction->operands[0]);
                        a64_emit_constant(&buffer, 10, stack_alignment - 1);
                        codegen_emit_u32(&buffer, 0x8b0a0129);
                        a64_emit_constant(&buffer, 10, ~(u64)(stack_alignment - 1));
                        codegen_emit_u32(&buffer, 0x8a0a0129);
                        codegen_emit_u32(&buffer, 0xf140053f);
                        codegen_emit_u32(&buffer, 0x540000a3);
                        codegen_emit_u32(&buffer, 0xd14007ff);
                        codegen_emit_u32(&buffer, 0xf94003ff);
                        codegen_emit_u32(&buffer, 0xd1400529);
                        codegen_emit_u32(&buffer, 0x17fffffb);
                        codegen_emit_u32(&buffer, 0xcb2963ff);
                        codegen_emit_u32(&buffer, 0xf94003ff);
                        codegen_emit_u32(&buffer, 0x910003ea);
                        c_a64_store(&emitter, 10, result_offset);
                    }
                    else if (instruction->opcode == IR_OPCODE_STACK_SAVE)
                    {
                        codegen_emit_u32(&buffer, 0x910003e9);
                        c_a64_store(&emitter, 9, result_offset);
                    }
                    else if (instruction->opcode == IR_OPCODE_STACK_RESTORE)
                    {
                        c_a64_load(&emitter, 9, instruction->operands[0]);
                        codegen_emit_u32(&buffer, 0x9100013f);
                    }
                    else if (instruction->opcode == IR_OPCODE_ARGUMENT)
                    {
                        u32 argument_index = (u32)instruction->immediates[0];
                        IrType* function_type = ir_type_from_id(&program->types, function->canonical_type);
                        if (!function_type || function_type->kind != IR_TYPE_FUNCTION || argument_index >= function_type->parameter_count)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        u32 register_index = 0;
                        u32 float_register_index = 0;
                        u32 prior_stack_parts = 0;
                        for (u32 prior_index = 0; prior_index < argument_index; prior_index += 1)
                        {
                            u32 prior_parts = 1;
                            bool prior_aggregate =
                                codegen_canonical_integer_aggregate_parts(program, function_type->parameter_types[prior_index], &prior_parts);
                            IrType* prior_type = ir_type_from_id(&program->types, function_type->parameter_types[prior_index]);
                            CodegenCanonicalAbiValue prior_abi =
                                codegen_canonical_aggregate_abi(program, function_type->parameter_types[prior_index], result.abi, false, false);
                            bool prior_hfa = prior_abi.part_count != 0;
                            for (u32 part = 0; part < prior_abi.part_count; part += 1)
                            {
                                prior_hfa &= codegen_canonical_abi_part_is_float(prior_abi.parts[part].abi_class);
                            }
                            if (prior_hfa)
                            {
                                if (float_register_index + prior_abi.part_count <= 8)
                                {
                                    float_register_index += prior_abi.part_count;
                                }
                                else
                                {
                                    float_register_index = 8;
                                    prior_stack_parts += (u32)((prior_type->layout.size + 7) / 8);
                                }
                                continue;
                            }
                            if (prior_type && prior_type->kind == IR_TYPE_FLOAT)
                            {
                                if (float_register_index < 8)
                                {
                                    float_register_index += 1;
                                }
                                else
                                {
                                    prior_stack_parts += 1;
                                }
                                continue;
                            }
                            if (prior_aggregate && prior_type && prior_type->layout.size > 16)
                            {
                                prior_parts = 1;
                            }
                            if (register_index + prior_parts <= 8)
                            {
                                register_index += prior_parts;
                            }
                            else
                            {
                                prior_stack_parts += prior_parts;
                            }
                        }
                        u32 part_count = 1;
                        IrType* argument_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        bool aggregate = codegen_canonical_integer_aggregate_parts(program, instruction->canonical_type, &part_count);
                        CodegenCanonicalAbiValue argument_abi = codegen_canonical_aggregate_abi(program, instruction->canonical_type, result.abi, false, false);
                        bool argument_hfa = argument_abi.part_count != 0;
                        for (u32 part = 0; part < argument_abi.part_count; part += 1)
                        {
                            argument_hfa &= codegen_canonical_abi_part_is_float(argument_abi.parts[part].abi_class);
                        }
                        bool indirect = aggregate && argument_type && argument_type->layout.size > 16;
                        u32 abi_part_count = indirect ? 1 : part_count;
                        if (!argument_type || ((argument_type->kind == IR_TYPE_STRUCT || argument_type->kind == IR_TYPE_UNION) && !aggregate))
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                            return result;
                        }
                        if (argument_type->kind == IR_TYPE_FLOAT)
                        {
                            if (argument_type->bit_width != 32 && argument_type->bit_width != 64)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                            if (float_register_index < 8)
                            {
                                u32 scale = argument_type->bit_width == 32 ? 4 : 8;
                                if (!codegen_canonical_a64_frame_float_memory_operation(&buffer, float_register_index, result_offset, scale, true))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                            else
                            {
                                if (!codegen_canonical_a64_memory_operation_base(&buffer, 9, 16 + prior_stack_parts * 8, 8, false, false, 29) ||
                                    !codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset, 8, true, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (argument_hfa)
                        {
                            if (float_register_index + argument_abi.part_count <= 8)
                            {
                                for (u32 part = 0; part < argument_abi.part_count; part += 1)
                                {
                                    codegen_canonical_a64_frame_float_memory_operation(&buffer, float_register_index + part,
                                                                                       result_offset + argument_abi.parts[part].value_offset,
                                                                                       argument_abi.parts[part].size, true);
                                }
                            }
                            else
                            {
                                for (u32 part = 0; part < argument_abi.part_count; part += 1)
                                {
                                    CodegenCanonicalAbiPart* abi_part = argument_abi.parts + part;
                                    u32 copied = 0;
                                    while (copied < abi_part->size)
                                    {
                                        u32 remaining = abi_part->size - copied;
                                        u32 chunk = remaining >= 8 ? 8 : 4;
                                        u32 source_offset = 16 + prior_stack_parts * 8 + abi_part->value_offset + copied;
                                        u32 destination_offset = result_offset + abi_part->value_offset + copied;
                                        if (!codegen_canonical_a64_memory_operation_base(&buffer, 9, source_offset, chunk, false, false, 29) ||
                                            !codegen_canonical_a64_frame_memory_operation(&buffer, 9, destination_offset, chunk, true, false))
                                        {
                                            result.error = CODEGEN_ERROR_CAPACITY;
                                            return result;
                                        }
                                        copied += chunk;
                                    }
                                }
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (register_index + abi_part_count > 8)
                        {
                            if (indirect)
                            {
                                if (!codegen_canonical_a64_memory_operation_base(&buffer, 10, 16 + prior_stack_parts * 8, 8, false, false, 29))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                for (u32 part_index = 0; part_index < part_count; part_index += 1)
                                {
                                    if (!codegen_canonical_a64_memory_operation_base(&buffer, 9, part_index * 8, 8, false, false, 10) ||
                                        !codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset + part_index * 8, 8, true, false))
                                    {
                                        result.error = CODEGEN_ERROR_CAPACITY;
                                        return result;
                                    }
                                }
                                instruction_id = instruction->next;
                                continue;
                            }
                            for (u32 part_index = 0; part_index < part_count; part_index += 1)
                            {
                                if (!codegen_canonical_a64_memory_operation_base(&buffer, 9, 16 + (prior_stack_parts + part_index) * 8, 8, false, false, 29) ||
                                    !codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset + part_index * 8, 8, true, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (indirect)
                        {
                            u32 source_register = register_index;
                            for (u32 part_index = 0; part_index < part_count; part_index += 1)
                            {
                                if (!codegen_canonical_a64_memory_operation_base(&buffer, 9, part_index * 8, 8, false, false, source_register) ||
                                    !codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset + part_index * 8, 8, true, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        for (u32 part_index = 0; part_index < abi_part_count; part_index += 1)
                        {
                            if (!codegen_canonical_a64_frame_memory_operation(&buffer, register_index + part_index, result_offset + part_index * 8, 8, true,
                                                                             false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_GLOBAL || instruction->opcode == IR_OPCODE_FUNCTION)
                    {
                        if (instruction->opcode == IR_OPCODE_FUNCTION && direct_call_uses[instruction->result.value] == 1)
                        {
                            codegen_emit_u32(&buffer, 0xaa1f03e9);
                            c_a64_store(&emitter, 9, result_offset);
                            instruction_id = instruction->next;
                            continue;
                        }
                        IrSymbol* symbol = ir_symbol_from_id(&program->symbols, instruction->symbol);
                        bool is_thread_local = instruction->opcode == IR_OPCODE_GLOBAL && symbol && symbol->is_thread_local;
                        if (is_thread_local)
                        {
                            if (target.os == OPERATING_SYSTEM_WINDOWS)
                            {
                                u32 index_high_offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0x90000009);
                                u32 index_low_offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0xb9400129);
                                codegen_emit_u32(&buffer, 0xf9402e4a);
                                codegen_emit_u32(&buffer, 0xf8697949);
                                u32 value_offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0x91000129);
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = index_high_offset,
                                    .kind = CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP,
                                    .aarch64 = true,
                                    .is_thread_local = true,
                                    .thread_local_index = true,
                                };
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = index_low_offset,
                                    .kind = CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_INDEX_LO12,
                                    .aarch64 = true,
                                    .is_thread_local = true,
                                    .thread_local_low = true,
                                    .thread_local_index = true,
                                };
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = value_offset,
                                    .kind = CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_OFFSET12,
                                    .aarch64 = true,
                                    .is_thread_local = true,
                                };
                            }
                            else if (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS)
                            {
                                u32 descriptor_high_offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0x90000000);
                                u32 descriptor_low_offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0xf9400000);
                                codegen_emit_u32(&buffer, 0xf9400008);
                                codegen_emit_u32(&buffer, 0xd63f0100);
                                codegen_emit_u32(&buffer, 0xaa0003e9);
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = descriptor_high_offset,
                                    .kind = CODEGEN_MODULE_RELOCATION_AARCH64_MACH_TLVP_PAGE21,
                                    .aarch64 = true,
                                    .is_thread_local = true,
                                };
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = descriptor_low_offset,
                                    .kind = CODEGEN_MODULE_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12,
                                    .aarch64 = true,
                                    .is_thread_local = true,
                                    .thread_local_low = true,
                                };
                            }
                            else if (target.os != OPERATING_SYSTEM_LINUX && target.os != OPERATING_SYSTEM_ANDROID)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                            else
                            {
                                codegen_emit_u32(&buffer, 0xd53bd049);
                                u32 high_offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0x91400129);
                                u32 low_offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0x91000129);
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = high_offset,
                                    .kind = CODEGEN_MODULE_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12,
                                    .aarch64 = true,
                                    .is_thread_local = true,
                                };
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = low_offset,
                                    .kind = CODEGEN_MODULE_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12,
                                    .aarch64 = true,
                                    .is_thread_local = true,
                                    .thread_local_low = true,
                                };
                            }
                        }
                        else
                        {
                            if (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS)
                            {
                                u32 high_offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0x90000009);
                                u32 low_offset = (u32)buffer.count;
                                codegen_emit_u32(&buffer, 0x91000129);
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = high_offset,
                                    .kind = CODEGEN_MODULE_RELOCATION_AARCH64_MACH_PAGE21,
                                    .aarch64 = true,
                                };
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = low_offset,
                                    .kind = CODEGEN_MODULE_RELOCATION_AARCH64_MACH_PAGEOFF12,
                                    .aarch64 = true,
                                };
                            }
                            else
                            {
                                codegen_emit_u32(&buffer, 0x58000049);
                                codegen_emit_u32(&buffer, 0x14000003);
                                u32 offset = (u32)buffer.count;
                                codegen_emit_u64(&buffer, 0);
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = instruction->symbol,
                                    .offset = offset,
                                    .kind = CODEGEN_MODULE_RELOCATION_ABSOLUTE64,
                                    .absolute = true,
                                };
                            }
                        }
                        if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset, 8, true, false))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_LOAD || instruction->opcode == IR_OPCODE_ATOMIC_LOAD)
                    {
                        IrValue* place = function->values + instruction->operands[0].value;
                        IrInstruction* definition = function->instructions + place->definition.value;
                        u32 aggregate_parts = 0;
                        bool aggregate = codegen_canonical_integer_aggregate_parts(program, instruction->canonical_type, &aggregate_parts);
                        IrType* loaded_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        bool indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                        definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE ||
                                        (definition->opcode == IR_OPCODE_LOCAL && place->alignment > 16);
                        if (instruction->opcode == IR_OPCODE_ATOMIC_LOAD && aggregate)
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        if (instruction->opcode == IR_OPCODE_ATOMIC_LOAD && instruction->memory_order != IR_MEMORY_ORDER_RELAXED)
                        {
                            if (!loaded_type || !loaded_type->layout.resolved ||
                                (loaded_type->layout.size != 1 && loaded_type->layout.size != 2 && loaded_type->layout.size != 4 &&
                                 loaded_type->layout.size != 8))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            if (indirect)
                            {
                                c_a64_load(&emitter, 10, instruction->operands[0]);
                            }
                            else
                            {
                                codegen_canonical_a64_base_address(&buffer, 10, 28, value_offsets[instruction->operands[0].value]);
                            }
                            a64_emit_atomic_pointer(&buffer, 9, 10, (u32)loaded_type->layout.size, false);
                            c_a64_store(&emitter, 9, result_offset);
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (aggregate)
                        {
                            if (!loaded_type || !loaded_type->layout.resolved)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            if (indirect)
                            {
                                c_a64_load(&emitter, 10, instruction->operands[0]);
                            }
                            for (u32 part_index = 0; part_index < aggregate_parts; part_index += 1)
                            {
                                u32 part_offset = part_index * 8;
                                u64 part_size = BUSTER_MIN((u64)8, loaded_type->layout.size - part_offset);
                                u64 part_copied = 0;
                                while (part_copied < part_size)
                                {
                                    u32 copy_offset = part_offset + (u32)part_copied;
                                    u64 remaining = part_size - part_copied;
                                    u32 chunk = codegen_canonical_copy_chunk(
                                        remaining, indirect ? copy_offset : value_offsets[instruction->operands[0].value] + copy_offset,
                                        result_offset + copy_offset);
                                    if (indirect)
                                    {
                                        a64_emit_load_pointer_offset(&buffer, 9, 10, copy_offset, chunk);
                                    }
                                    else
                                    {
                                        a64_emit_load_pointer_offset(&buffer, 9, 28, value_offsets[instruction->operands[0].value] + copy_offset, chunk);
                                    }
                                    a64_emit_store_pointer_offset(&buffer, 9, 28, result_offset + copy_offset, chunk);
                                    part_copied += chunk;
                                }
                            }
                        }
                        else if (indirect)
                        {
                            if (!loaded_type || !loaded_type->layout.resolved ||
                                (loaded_type->layout.size != 1 && loaded_type->layout.size != 2 && loaded_type->layout.size != 4 &&
                                 loaded_type->layout.size != 8))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            a64_emit_load_pointer_offset(&buffer, 9, 28, value_offsets[instruction->operands[0].value], 8);
                            a64_emit_load_pointer(&buffer, 9, 9, (u32)loaded_type->layout.size);
                        }
                        else
                        {
                            c_a64_load(&emitter, 9, instruction->operands[0]);
                        }
                        if (!aggregate)
                        {
                            c_a64_store(&emitter, 9, result_offset);
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_INDEX)
                    {
                        IrValueId base = instruction->operands[0];
                        IrInstruction* base_definition = function->instructions + function->values[base.value].definition.value;
                        IrType* base_type = ir_type_from_id(&program->types, function->values[base.value].canonical_type);
                        u32 base_offset = value_offsets[base.value];
                        if (base_definition->opcode == IR_OPCODE_LOCAL || (function->values[base.value].category == IR_VALUE_VALUE && base_type &&
                                                                           (base_type->kind == IR_TYPE_ARRAY || base_type->kind == IR_TYPE_VECTOR)))
                        {
                            if (base_definition->opcode == IR_OPCODE_LOCAL && function->values[base.value].alignment > 16)
                            {
                                c_a64_load(&emitter, 9, base);
                            }
                            else
                            {
                                codegen_canonical_a64_base_address(&buffer, 9, 28, base_offset);
                            }
                        }
                        else
                        {
                            c_a64_load(&emitter, 9, base);
                        }
                        c_a64_load(&emitter, 10, instruction->operands[1]);
                        IrType* index_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
                        IrType* element = ir_type_from_id(&program->types, instruction->canonical_type);
                        if (!index_type || index_type->kind != IR_TYPE_INTEGER || !element || !element->layout.resolved)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        if (index_type->is_signed && index_type->bit_width < 64)
                        {
                            u32 sign_extend = index_type->bit_width == 8    ? 0x93401d4a
                                              : index_type->bit_width == 16 ? 0x93403d4a
                                              : index_type->bit_width == 32 ? 0x93407d4a
                                                                            : 0;
                            if (!sign_extend)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            codegen_emit_u32(&buffer, sign_extend);
                        }
                        // The stride is an arbitrary 64-bit constant, not a movz
                        // immediate: an element of 64 KiB or more needs the movk
                        // continuation, and used to be rejected outright.
                        a64_emit_constant_compact(&buffer, 11, element->layout.size);
                        codegen_emit_u32(&buffer, 0x9b0b7d4a);
                        codegen_emit_u32(&buffer, 0x8b0a0129);
                        c_a64_store(&emitter, 9, result_offset);
                    }
                    else if (instruction->opcode == IR_OPCODE_ADDRESS_OF)
                    {
                        IrValueId object = instruction->operands[0];
                        IrInstruction* definition = function->instructions + function->values[object.value].definition.value;
                        u32 object_offset = value_offsets[object.value];
                        if (definition->opcode == IR_OPCODE_LOCAL)
                        {
                            if (function->values[object.value].alignment > 16)
                            {
                                c_a64_load(&emitter, 9, object);
                            }
                            else
                            {
                                codegen_canonical_a64_base_address(&buffer, 9, 28, object_offset);
                            }
                        }
                        else
                        {
                            c_a64_load(&emitter, 9, object);
                        }
                        c_a64_store(&emitter, 9, result_offset);
                    }
                    else if (instruction->opcode == IR_OPCODE_DEREFERENCE)
                    {
                        c_a64_load(&emitter, 9, instruction->operands[0]);
                        c_a64_store(&emitter, 9, result_offset);
                    }
                    else if (instruction->opcode == IR_OPCODE_FIELD)
                    {
                        IrValueId base = instruction->operands[0];
                        IrInstruction* definition = function->instructions + function->values[base.value].definition.value;
                        u32 base_offset = value_offsets[base.value];
                        if (definition->opcode == IR_OPCODE_LOCAL)
                        {
                            if (function->values[base.value].alignment > 16)
                            {
                                c_a64_load(&emitter, 9, base);
                            }
                            else
                            {
                                codegen_canonical_a64_base_address(&buffer, 9, 28, base_offset);
                            }
                        }
                        else
                        {
                            c_a64_load(&emitter, 9, base);
                        }
                        IrType* aggregate = ir_type_from_id(&program->types, function->values[base.value].canonical_type);
                        u64 field_index = instruction->immediates[0];
                        if (!aggregate || field_index >= aggregate->field_count)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        u64 field_offset = aggregate->fields[field_index].offset;
                        if (field_offset <= A64_IMM12_MAX)
                        {
                            codegen_emit_u32(&buffer, 0x91000129 | ((u32)field_offset << 10));
                        }
                        else
                        {
                            a64_emit_constant(&buffer, 10, field_offset);
                            codegen_emit_u32(&buffer, 0x8b0a0129);
                        }
                        c_a64_store(&emitter, 9, result_offset);
                    }
                    else if (instruction->opcode == IR_OPCODE_CAST)
                    {
                        IrType* source_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
                        IrType* target_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        IrConversionOperation conversion = instruction->conversion_operation;
                        c_a64_load(&emitter, 9, instruction->operands[0]);
                        if (!target_type || !source_type)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        if (conversion == IR_CONVERSION_INTEGER_SIGN_EXTEND && source_type->kind == IR_TYPE_INTEGER)
                        {
                            u32 sign_extend = source_type->bit_width == 8    ? 0x93401d29
                                              : source_type->bit_width == 16 ? 0x93403d29
                                              : source_type->bit_width == 32 ? 0x93407d29
                                                                             : 0;
                            if (sign_extend)
                            {
                                codegen_emit_u32(&buffer, sign_extend);
                            }
                        }
                        else if ((conversion == IR_CONVERSION_INTEGER_ZERO_EXTEND || conversion == IR_CONVERSION_INTEGER_TRUNCATE ||
                                  conversion == IR_CONVERSION_INTEGER_REINTERPRET) &&
                                 source_type->kind == IR_TYPE_INTEGER && target_type->kind == IR_TYPE_INTEGER)
                        {
                            u32 effective_bit_width = conversion == IR_CONVERSION_INTEGER_ZERO_EXTEND ? source_type->bit_width
                                                      : conversion == IR_CONVERSION_INTEGER_REINTERPRET && source_type->bit_width < target_type->bit_width
                                                          ? source_type->bit_width
                                                          : target_type->bit_width;
                            u32 zero_extend = effective_bit_width == 8    ? 0x53001d29
                                              : effective_bit_width == 16 ? 0x53003d29
                                              : effective_bit_width == 32 ? 0x2a0903e9
                                                                          : 0;
                            if (zero_extend)
                            {
                                codegen_emit_u32(&buffer, zero_extend);
                            }
                        }
                        if (target_type && source_type && target_type->kind == IR_TYPE_FLOAT && source_type->kind == IR_TYPE_INTEGER &&
                            (instruction->conversion_operation == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT ||
                             instruction->conversion_operation == IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT))
                        {
                            if (instruction->conversion_operation == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT && source_type->bit_width < 64)
                            {
                                u32 sign_extend = source_type->bit_width == 8 ? 0x93401d29 : source_type->bit_width == 16 ? 0x93403d29 : 0x93407d29;
                                codegen_emit_u32(&buffer, sign_extend);
                            }
                            u32 encoded = instruction->conversion_operation == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT ? 0x9e220000 : 0x9e230000;
                            if (target_type->bit_width == 64)
                            {
                                encoded |= 0x00400000;
                            }
                            else if (target_type->bit_width != 32)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            codegen_emit_u32(&buffer, encoded | (9 << 5));
                            codegen_canonical_a64_frame_float_memory_operation(&buffer, 0, result_offset, (u32)target_type->layout.size, true);
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (target_type && source_type && target_type->kind == IR_TYPE_FLOAT && source_type->kind == IR_TYPE_FLOAT &&
                            (instruction->conversion_operation == IR_CONVERSION_FLOAT_EXTEND ||
                             instruction->conversion_operation == IR_CONVERSION_FLOAT_TRUNCATE))
                        {
                            codegen_canonical_a64_frame_float_memory_operation(&buffer, 0, value_offsets[instruction->operands[0].value],
                                                                               (u32)source_type->layout.size, false);
                            codegen_emit_u32(&buffer, instruction->conversion_operation == IR_CONVERSION_FLOAT_EXTEND ? 0x1e22c000 : 0x1e624000);
                            codegen_canonical_a64_frame_float_memory_operation(&buffer, 0, result_offset, (u32)target_type->layout.size, true);
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (target_type && source_type && target_type->kind == IR_TYPE_INTEGER && source_type->kind == IR_TYPE_FLOAT &&
                            (instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER ||
                             instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER))
                        {
                            codegen_canonical_a64_frame_float_memory_operation(&buffer, 0, value_offsets[instruction->operands[0].value],
                                                                               (u32)source_type->layout.size, false);
                            u32 encoded = instruction->conversion_operation == IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER ? 0x9e380000 : 0x9e390000;
                            if (source_type->bit_width == 64)
                            {
                                encoded |= 0x00400000;
                            }
                            codegen_emit_u32(&buffer, encoded | 9);
                            c_a64_store(&emitter, 9, result_offset);
                            instruction_id = instruction->next;
                            continue;
                        }
                        c_a64_store(&emitter, 9, result_offset);
                    }
                    else if (instruction->opcode == IR_OPCODE_STORE || instruction->opcode == IR_OPCODE_ATOMIC_STORE)
                    {
                        IrValue* place = function->values + instruction->operands[0].value;
                        IrInstruction* definition = function->instructions + place->definition.value;
                        u32 aggregate_parts = 0;
                        bool aggregate = codegen_canonical_integer_aggregate_parts(program, function->values[instruction->operands[1].value].canonical_type,
                                                                                   &aggregate_parts);
                        IrType* stored_type = ir_type_from_id(&program->types, function->values[instruction->operands[1].value].canonical_type);
                        if (!aggregate && stored_type && stored_type->layout.resolved && stored_type->layout.size > 8 &&
                            stored_type->layout.size <= (u64)UINT32_MAX * 8)
                        {
                            aggregate = true;
                            aggregate_parts = (u32)((stored_type->layout.size + 7) / 8);
                        }
                        bool indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                        definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE ||
                                        (definition->opcode == IR_OPCODE_LOCAL && place->alignment > 16);
                        if (instruction->opcode == IR_OPCODE_ATOMIC_STORE && aggregate)
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        if (instruction->opcode == IR_OPCODE_ATOMIC_STORE && instruction->memory_order != IR_MEMORY_ORDER_RELAXED)
                        {
                            if (!stored_type || !stored_type->layout.resolved ||
                                (stored_type->layout.size != 1 && stored_type->layout.size != 2 && stored_type->layout.size != 4 &&
                                 stored_type->layout.size != 8))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            c_a64_load(&emitter, 9, instruction->operands[1]);
                            if (indirect)
                            {
                                c_a64_load(&emitter, 10, instruction->operands[0]);
                            }
                            else
                            {
                                codegen_canonical_a64_base_address(&buffer, 10, 28, value_offsets[instruction->operands[0].value]);
                            }
                            a64_emit_atomic_pointer(&buffer, 9, 10, (u32)stored_type->layout.size, true);
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (aggregate)
                        {
                            if (!stored_type || !stored_type->layout.resolved)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            if (indirect)
                            {
                                c_a64_load(&emitter, 10, instruction->operands[0]);
                            }
                            for (u32 part_index = 0; part_index < aggregate_parts; part_index += 1)
                            {
                                u32 part_offset = part_index * 8;
                                u64 part_size = BUSTER_MIN((u64)8, stored_type->layout.size - part_offset);
                                u64 part_copied = 0;
                                while (part_copied < part_size)
                                {
                                    u64 remaining = part_size - part_copied;
                                    u32 copy_offset = part_offset + (u32)part_copied;
                                    u32 chunk =
                                        codegen_canonical_copy_chunk(remaining, value_offsets[instruction->operands[1].value] + copy_offset, copy_offset);
                                    a64_emit_load_pointer_offset(&buffer, 9, 28, value_offsets[instruction->operands[1].value] + copy_offset, chunk);
                                    if (indirect)
                                    {
                                        a64_emit_store_pointer_offset(&buffer, 9, 10, copy_offset, chunk);
                                    }
                                    else
                                    {
                                        a64_emit_store_pointer_offset(&buffer, 9, 28, value_offsets[instruction->operands[0].value] + copy_offset, chunk);
                                    }
                                    part_copied += chunk;
                                }
                            }
                        }
                        else if (indirect)
                        {
                            if (!stored_type || !stored_type->layout.resolved ||
                                (stored_type->layout.size != 1 && stored_type->layout.size != 2 && stored_type->layout.size != 4 &&
                                 stored_type->layout.size != 8))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            c_a64_load(&emitter, 10, instruction->operands[1]);
                            c_a64_load(&emitter, 9, instruction->operands[0]);
                            a64_emit_store_pointer(&buffer, 10, 9, (u32)stored_type->layout.size);
                        }
                        else
                        {
                            c_a64_load(&emitter, 9, instruction->operands[1]);
                            u32 place_offset = value_offsets[instruction->operands[0].value];
                            if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, place_offset, 8, true, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_ATOMIC_READ_MODIFY_WRITE)
                    {
                        IrValue* place = function->values + instruction->operands[0].value;
                        IrInstruction* definition = function->instructions + place->definition.value;
                        IrType* value_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        bool indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                        definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE ||
                                        (definition->opcode == IR_OPCODE_LOCAL && place->alignment > 16);
                        bool pointer_arithmetic = value_type && value_type->kind == IR_TYPE_POINTER &&
                                                  (instruction->atomic_operation == IR_ATOMIC_ADD || instruction->atomic_operation == IR_ATOMIC_SUBTRACT);
                        if (!value_type ||
                            (!pointer_arithmetic && value_type->kind != IR_TYPE_INTEGER &&
                             (instruction->atomic_operation != IR_ATOMIC_EXCHANGE ||
                              (value_type->kind != IR_TYPE_BOOLEAN && value_type->kind != IR_TYPE_POINTER))) ||
                            !value_type->layout.resolved ||
                            (value_type->layout.size != 1 && value_type->layout.size != 2 && value_type->layout.size != 4 && value_type->layout.size != 8) ||
                            instruction->atomic_operation >= IR_ATOMIC_OPERATION_COUNT)
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        c_a64_load(&emitter, 11, instruction->operands[1]);
                        if (indirect)
                        {
                            c_a64_load(&emitter, 10, instruction->operands[0]);
                        }
                        else
                        {
                            codegen_canonical_a64_base_address(&buffer, 10, 28, value_offsets[instruction->operands[0].value]);
                        }
                        bool acquire = instruction->memory_order == IR_MEMORY_ORDER_CONSUME || instruction->memory_order == IR_MEMORY_ORDER_ACQUIRE ||
                                       instruction->memory_order == IR_MEMORY_ORDER_ACQUIRE_RELEASE || instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL;
                        bool release = instruction->memory_order == IR_MEMORY_ORDER_RELEASE || instruction->memory_order == IR_MEMORY_ORDER_ACQUIRE_RELEASE ||
                                       instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL;
                        u32 retry_offset = (u32)buffer.count;
                        a64_emit_atomic_exclusive_load(&buffer, 9, 10, (u32)value_type->layout.size, acquire);
                        u32 operation = 0;
                        bool wide = value_type->layout.size == 8;
                        switch (instruction->atomic_operation)
                        {
                        case IR_ATOMIC_ADD:
                            operation = wide ? UINT32_C(0x8b0b012c) : UINT32_C(0x0b0b012c);
                            break;
                        case IR_ATOMIC_SUBTRACT:
                            operation = wide ? UINT32_C(0xcb0b012c) : UINT32_C(0x4b0b012c);
                            break;
                        case IR_ATOMIC_BITWISE_AND:
                            operation = wide ? UINT32_C(0x8a0b012c) : UINT32_C(0x0a0b012c);
                            break;
                        case IR_ATOMIC_BITWISE_OR:
                            operation = wide ? UINT32_C(0xaa0b012c) : UINT32_C(0x2a0b012c);
                            break;
                        case IR_ATOMIC_BITWISE_XOR:
                            operation = wide ? UINT32_C(0xca0b012c) : UINT32_C(0x4a0b012c);
                            break;
                        case IR_ATOMIC_EXCHANGE:
                            operation = wide ? UINT32_C(0xaa0b03ec) : UINT32_C(0x2a0b03ec);
                            break;
                        case IR_ATOMIC_OPERATION_COUNT:
                            break;
                        }
                        codegen_emit_u32(&buffer, operation);
                        a64_emit_atomic_exclusive_store(&buffer, 13, 12, 10, (u32)value_type->layout.size, release);
                        s64 retry_displacement = (s64)retry_offset - (s64)buffer.count;
                        if (retry_displacement % 4 || retry_displacement / 4 < -INT64_C(0x40000) || retry_displacement / 4 > INT64_C(0x3ffff))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        codegen_emit_u32(&buffer, UINT32_C(0x35000000) | (((u32)(retry_displacement / 4) & UINT32_C(0x7ffff)) << 5) | 13);
                        c_a64_store(&emitter, 9, result_offset);
                    }
                    else if (instruction->opcode == IR_OPCODE_ATOMIC_COMPARE_EXCHANGE)
                    {
                        IrValue* place = function->values + instruction->operands[0].value;
                        IrInstruction* definition = function->instructions + place->definition.value;
                        IrType* value_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        bool indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                        definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE ||
                                        (definition->opcode == IR_OPCODE_LOCAL && place->alignment > 16);
                        if (!value_type || (value_type->kind != IR_TYPE_INTEGER && value_type->kind != IR_TYPE_POINTER) || !value_type->layout.resolved ||
                            (value_type->layout.size != 1 && value_type->layout.size != 2 && value_type->layout.size != 4 && value_type->layout.size != 8))
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        c_a64_load(&emitter, 12, instruction->operands[1]);
                        c_a64_load(&emitter, 11, instruction->operands[2]);
                        if (indirect)
                        {
                            c_a64_load(&emitter, 10, instruction->operands[0]);
                        }
                        else
                        {
                            codegen_canonical_a64_base_address(&buffer, 10, 28, value_offsets[instruction->operands[0].value]);
                        }
                        bool acquire =
                            instruction->memory_order == IR_MEMORY_ORDER_CONSUME || instruction->memory_order == IR_MEMORY_ORDER_ACQUIRE ||
                            instruction->memory_order == IR_MEMORY_ORDER_ACQUIRE_RELEASE || instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL ||
                            instruction->failure_memory_order == IR_MEMORY_ORDER_CONSUME || instruction->failure_memory_order == IR_MEMORY_ORDER_ACQUIRE ||
                            instruction->failure_memory_order == IR_MEMORY_ORDER_SEQUENTIAL;
                        bool release = instruction->memory_order == IR_MEMORY_ORDER_RELEASE || instruction->memory_order == IR_MEMORY_ORDER_ACQUIRE_RELEASE ||
                                       instruction->memory_order == IR_MEMORY_ORDER_SEQUENTIAL;
                        u32 retry_offset = (u32)buffer.count;
                        a64_emit_atomic_exclusive_load(&buffer, 9, 10, (u32)value_type->layout.size, acquire);
                        bool wide = value_type->layout.size == 8;
                        codegen_emit_u32(&buffer, wide ? UINT32_C(0xeb0c013f) : UINT32_C(0x6b0c013f));
                        codegen_emit_u32(&buffer, UINT32_C(0x54000061));
                        a64_emit_atomic_exclusive_store(&buffer, 13, 11, 10, (u32)value_type->layout.size, release);
                        s64 retry_displacement = (s64)retry_offset - (s64)buffer.count;
                        if (retry_displacement % 4 || retry_displacement / 4 < -INT64_C(0x40000) || retry_displacement / 4 > INT64_C(0x3ffff))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        codegen_emit_u32(&buffer, UINT32_C(0x35000000) | (((u32)(retry_displacement / 4) & UINT32_C(0x7ffff)) << 5) | 13);
                        codegen_emit_u32(&buffer, UINT32_C(0xd5033f5f));
                        c_a64_store(&emitter, 9, result_offset);
                    }
                    else if (instruction->opcode == IR_OPCODE_ATOMIC_FENCE)
                    {
                        if (!instruction->atomic_signal_fence && instruction->memory_order != IR_MEMORY_ORDER_RELAXED)
                        {
                            codegen_emit_u32(&buffer, UINT32_C(0xd5033bbf));
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_CLEAR_INSTRUCTION_CACHE)
                    {
                        if (instruction->operand_count != 2)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        c_a64_load(&emitter, 9, instruction->operands[0]);
                        c_a64_load(&emitter, 10, instruction->operands[1]);
                        codegen_emit_u32(&buffer, UINT32_C(0xaa0903eb));
                        codegen_emit_u32(&buffer, UINT32_C(0xeb0a013f));
                        codegen_emit_u32(&buffer, UINT32_C(0x54000082));
                        codegen_emit_u32(&buffer, UINT32_C(0xd50b7b29));
                        codegen_emit_u32(&buffer, UINT32_C(0x91001129));
                        codegen_emit_u32(&buffer, UINT32_C(0x17fffffc));
                        codegen_emit_u32(&buffer, UINT32_C(0xd5033b9f));
                        codegen_emit_u32(&buffer, UINT32_C(0xaa0b03e9));
                        codegen_emit_u32(&buffer, UINT32_C(0xeb0a013f));
                        codegen_emit_u32(&buffer, UINT32_C(0x54000082));
                        codegen_emit_u32(&buffer, UINT32_C(0xd50b7529));
                        codegen_emit_u32(&buffer, UINT32_C(0x91001129));
                        codegen_emit_u32(&buffer, UINT32_C(0x17fffffc));
                        codegen_emit_u32(&buffer, UINT32_C(0xd5033b9f));
                        codegen_emit_u32(&buffer, UINT32_C(0xd5033fdf));
                    }
                    else if (instruction->opcode == IR_OPCODE_CONSTANT_INTEGER || instruction->opcode == IR_OPCODE_CONSTANT_FLOAT)
                    {
                        u64 immediate = instruction->immediates[0];
                        if (instruction->opcode == IR_OPCODE_CONSTANT_INTEGER && instruction->immediate_is_negative)
                        {
                            immediate = 0 - immediate;
                        }
                        bool wide = codegen_canonical_register_is_64_bit(program, instruction->canonical_type);
                        codegen_emit_u32(&buffer, (wide ? 0xd2800009 : 0x52800009) | ((u32)(immediate & 0xffff) << 5));
                        codegen_emit_u32(&buffer, (wide ? 0xf2a00009 : 0x72a00009) | ((u32)((immediate >> 16) & 0xffff) << 5));
                        if (wide)
                        {
                            codegen_emit_u32(&buffer, 0xf2c00009 | ((u32)((immediate >> 32) & 0xffff) << 5));
                            codegen_emit_u32(&buffer, 0xf2e00009 | ((u32)((immediate >> 48) & 0xffff) << 5));
                        }
                        c_a64_store(&emitter, 9, result_offset);
                    }
                    else if (instruction->opcode == IR_OPCODE_VA_START)
                    {
                        if (!canonical_variadic)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        u32 gp_count = 0;
                        u32 stack_parts = 0;
                        for (u32 parameter_index = 0; parameter_index < canonical_function_type->parameter_count; parameter_index += 1)
                        {
                            IrTypeId parameter_type = canonical_function_type->parameter_types[parameter_index];
                            IrType* parameter = ir_type_from_id(&program->types, parameter_type);
                            u32 part_count = 1;
                            bool aggregate = codegen_canonical_integer_aggregate_parts(program, parameter_type, &part_count);
                            if (aggregate && parameter && parameter->layout.size > 16)
                            {
                                part_count = 1;
                            }
                            if (gp_count + part_count <= 8)
                            {
                                gp_count += part_count;
                            }
                            else
                            {
                                stack_parts += part_count;
                            }
                        }
                        if (aarch64_darwin)
                        {
                            codegen_canonical_a64_base_address(&buffer, 9, 29, 16 + stack_parts * 8);
                            if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset, 8, true, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        a64_emit_constant(&buffer, 9, gp_count * 8);
                        if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset, 8, true, false))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        u32 overflow_offset = 16 + stack_parts * 8;
                        if (overflow_offset > A64_IMM12_MAX)
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        codegen_emit_u32(&buffer, 0x910003a9 | (overflow_offset << 10));
                        if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset + 8, 8, true, false))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        codegen_canonical_a64_base_address(&buffer, 9, 28, aarch64_va_save_offset);
                        if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset + 16, 8, true, false) ||
                            !codegen_canonical_a64_frame_memory_operation(&buffer, 31, result_offset + 24, 8, true, false))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_VA_COPY)
                    {
                        c_a64_load(&emitter, 10, instruction->operands[0]);
                        if (aarch64_darwin)
                        {
                            codegen_emit_u32(&buffer, 0xf9400149);
                            if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset, 8, true, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        for (u32 component = 0; component < 4; component += 1)
                        {
                            codegen_emit_u32(&buffer, 0xf9400149 | (component << 10));
                            if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset + component * 8, 8, true, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_VA_END)
                    {
                        if (aarch64_darwin)
                        {
                            instruction_id = instruction->next;
                            continue;
                        }
                        c_a64_load(&emitter, 10, instruction->operands[0]);
                        a64_emit_constant(&buffer, 9, 1);
                        codegen_emit_u32(&buffer, 0xf9000d49);
                    }
                    else if (instruction->opcode == IR_OPCODE_VA_ARG)
                    {
                        IrType* value_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        u32 part_count = 1;
                        bool aggregate = codegen_canonical_integer_aggregate_parts(program, instruction->canonical_type, &part_count);
                        if (!value_type || !value_type->layout.size || value_type->layout.size > 16 ||
                            (!aggregate && value_type->kind != IR_TYPE_INTEGER && value_type->kind != IR_TYPE_BOOLEAN && value_type->kind != IR_TYPE_POINTER &&
                             value_type->kind != IR_TYPE_FLOAT))
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        c_a64_load(&emitter, 10, instruction->operands[0]);
                        if (aarch64_darwin)
                        {
                            codegen_emit_u32(&buffer, 0xf940014b);
                            for (u32 part_index = 0; part_index < part_count; part_index += 1)
                            {
                                if (!codegen_canonical_a64_memory_operation_base(&buffer, 9, part_index * 8, 8, false, false, 11) ||
                                    !codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset + part_index * 8, 8, true, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                            a64_emit_constant(&buffer, 9, part_count * 8);
                            codegen_emit_u32(&buffer, 0x8b09016b);
                            codegen_emit_u32(&buffer, 0xf900014b);
                            instruction_id = instruction->next;
                            continue;
                        }
                        codegen_emit_u32(&buffer, 0xf940014b);
                        u32 increment = part_count * 8;
                        u32 limit = 64 - increment;
                        codegen_emit_u32(&buffer, 0xf100017f | (limit << 10));
                        u32 overflow_patch = (u32)buffer.count;
                        codegen_emit_u32(&buffer, 0x54000008);
                        codegen_emit_u32(&buffer, 0xf940094c);
                        codegen_emit_u32(&buffer, 0x8b0b018c);
                        for (u32 part_index = 0; part_index < part_count; part_index += 1)
                        {
                            codegen_emit_u32(&buffer, 0xf9400189 | (part_index << 10));
                            if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset + part_index * 8, 8, true, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                        }
                        codegen_emit_u32(&buffer, 0x9100016b | (increment << 10));
                        codegen_emit_u32(&buffer, 0xf900014b);
                        u32 end_patch = (u32)buffer.count;
                        codegen_emit_u32(&buffer, 0x14000000);
                        u32 overflow_offset = (u32)buffer.count;
                        codegen_emit_u32(&buffer, 0xf940054c);
                        for (u32 part_index = 0; part_index < part_count; part_index += 1)
                        {
                            codegen_emit_u32(&buffer, 0xf9400189 | (part_index << 10));
                            if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, result_offset + part_index * 8, 8, true, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                        }
                        codegen_emit_u32(&buffer, 0x9100018c | (increment << 10));
                        codegen_emit_u32(&buffer, 0xf900054c);
                        u32 end_offset = (u32)buffer.count;
                        u32 conditional = 0x54000008 | (((overflow_offset - overflow_patch) / 4) << 5);
                        memcpy(buffer.bytes + overflow_patch, &conditional, sizeof(conditional));
                        u32 branch = 0x14000000 | ((end_offset - end_patch) / 4);
                        memcpy(buffer.bytes + end_patch, &branch, sizeof(branch));
                    }
                    else if (instruction->opcode == IR_OPCODE_CALL)
                    {
                        u32 argument_count = instruction->operand_count - 1;
                        IrType* callee_type = ir_type_from_id(&program->types, function->values[instruction->operands[0].value].canonical_type);
                        IrType* callee_function_type = callee_type && callee_type->kind == IR_TYPE_POINTER
                                                           ? ir_type_from_id(&program->types, callee_type->element_type)
                                                           : callee_type;
                        bool darwin_variadic_call = result.abi == CODEGEN_ABI_AARCH64_DARWIN && callee_function_type &&
                                                    callee_function_type->kind == IR_TYPE_FUNCTION && callee_function_type->is_variadic;
                        bool* argument_on_stack = arena_allocate(arena, bool, argument_count);
                        u32* argument_stack_offset = arena_allocate(arena, u32, argument_count);
                        bool* argument_indirect = arena_allocate(arena, bool, argument_count);
                        u8* argument_float_register = arena_allocate(arena, u8, argument_count);
                        memset(argument_on_stack, 0, sizeof(*argument_on_stack) * argument_count);
                        memset(argument_indirect, 0, sizeof(*argument_indirect) * argument_count);
                        memset(argument_float_register, UINT8_MAX, sizeof(*argument_float_register) * argument_count);
                        u32 simulated_registers = 0;
                        u32 simulated_float_registers = 0;
                        u32 stack_part_count = 0;
                        for (u32 argument_array_index = 0; argument_array_index < argument_count; argument_array_index += 1)
                        {
                            IrValueId argument = instruction->operands[argument_array_index + 1];
                            IrTypeId type_id = function->values[argument.value].canonical_type;
                            IrType* type = ir_type_from_id(&program->types, type_id);
                            u32 part_count = 1;
                            bool aggregate = codegen_canonical_integer_aggregate_parts(program, type_id, &part_count);
                            CodegenCanonicalAbiValue argument_abi = codegen_canonical_aggregate_abi(program, type_id, result.abi, false, false);
                            bool argument_hfa = argument_abi.part_count != 0;
                            for (u32 part = 0; part < argument_abi.part_count; part += 1)
                            {
                                argument_hfa &= codegen_canonical_abi_part_is_float(argument_abi.parts[part].abi_class);
                            }
                            if (!type || ((type->kind == IR_TYPE_STRUCT || type->kind == IR_TYPE_UNION) && !aggregate))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                            bool unnamed_variadic = darwin_variadic_call && argument_array_index >= callee_function_type->parameter_count;
                            if (type->kind == IR_TYPE_FLOAT)
                            {
                                if (!unnamed_variadic && simulated_float_registers < 8)
                                {
                                    argument_float_register[argument_array_index] = (u8)simulated_float_registers++;
                                }
                                else
                                {
                                    argument_on_stack[argument_array_index] = true;
                                    argument_stack_offset[argument_array_index] = stack_part_count;
                                    stack_part_count += 1;
                                }
                                continue;
                            }
                            if (argument_hfa)
                            {
                                if (!unnamed_variadic && simulated_float_registers + argument_abi.part_count <= 8)
                                {
                                    argument_float_register[argument_array_index] = (u8)simulated_float_registers;
                                    simulated_float_registers += argument_abi.part_count;
                                }
                                else
                                {
                                    if (!unnamed_variadic)
                                    {
                                        simulated_float_registers = 8;
                                    }
                                    argument_on_stack[argument_array_index] = true;
                                    argument_stack_offset[argument_array_index] = stack_part_count;
                                    stack_part_count += (u32)((type->layout.size + 7) / 8);
                                }
                                continue;
                            }
                            bool indirect = aggregate && type->layout.size > 16;
                            if (indirect)
                            {
                                part_count = 1;
                                argument_indirect[argument_array_index] = true;
                            }
                            if (!unnamed_variadic && simulated_registers + part_count <= 8)
                            {
                                simulated_registers += part_count;
                            }
                            else
                            {
                                argument_on_stack[argument_array_index] = true;
                                argument_stack_offset[argument_array_index] = stack_part_count;
                                stack_part_count += part_count;
                            }
                        }
                        u32 stack_size = (stack_part_count * 8 + 15) & ~(u32)15;
                        if (stack_size)
                        {
                            codegen_canonical_a64_adjust_stack(&buffer, stack_size, true);
                        }
                        for (u32 argument_array_index = 0; argument_array_index < argument_count; argument_array_index += 1)
                        {
                            if (!argument_on_stack[argument_array_index])
                            {
                                continue;
                            }
                            IrValueId argument = instruction->operands[argument_array_index + 1];
                            if (argument_indirect[argument_array_index])
                            {
                                u32 source_offset = value_offsets[argument.value];
                                codegen_canonical_a64_base_address(&buffer, 9, 28, source_offset);
                                if (!codegen_canonical_a64_memory_operation(&buffer, 9, argument_stack_offset[argument_array_index] * 8, 8, true, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                continue;
                            }
                            u32 part_count = 1;
                            codegen_canonical_integer_aggregate_parts(program, function->values[argument.value].canonical_type, &part_count);
                            for (u32 part_index = 0; part_index < part_count; part_index += 1)
                            {
                                u32 source_offset = value_offsets[argument.value] + part_index * 8;
                                if (!codegen_canonical_a64_frame_memory_operation(&buffer, 9, source_offset, 8, false, false) ||
                                    !codegen_canonical_a64_memory_operation(&buffer, 9, (argument_stack_offset[argument_array_index] + part_index) * 8, 8, true,
                                                                            false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                        }
                        u32 register_index = 0;
                        for (u32 argument_index = 1; argument_index < instruction->operand_count; argument_index += 1)
                        {
                            IrValueId argument = instruction->operands[argument_index];
                            IrTypeId argument_type_id = function->values[argument.value].canonical_type;
                            IrType* argument_type = ir_type_from_id(&program->types, argument_type_id);
                            u32 part_count = 1;
                            bool aggregate = codegen_canonical_integer_aggregate_parts(program, argument_type_id, &part_count);
                            CodegenCanonicalAbiValue argument_abi = codegen_canonical_aggregate_abi(program, argument_type_id, result.abi, false, false);
                            bool argument_hfa = argument_abi.part_count != 0;
                            for (u32 part = 0; part < argument_abi.part_count; part += 1)
                            {
                                argument_hfa &= codegen_canonical_abi_part_is_float(argument_abi.parts[part].abi_class);
                            }
                            bool indirect = aggregate && argument_type && argument_type->layout.size > 16;
                            if (indirect)
                            {
                                part_count = 1;
                            }
                            if (argument_on_stack[argument_index - 1])
                            {
                                continue;
                            }
                            if (!argument_type || ((argument_type->kind == IR_TYPE_STRUCT || argument_type->kind == IR_TYPE_UNION) && !aggregate) ||
                                register_index + part_count > 8)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                return result;
                            }
                            if (argument_type->kind == IR_TYPE_FLOAT)
                            {
                                u8 float_register = argument_float_register[argument_index - 1];
                                if (float_register >= 8 || (argument_type->bit_width != 32 && argument_type->bit_width != 64))
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                u32 source_offset = value_offsets[argument.value];
                                if (!codegen_canonical_a64_frame_float_memory_operation(&buffer, float_register, source_offset, argument_type->bit_width / 8,
                                                                                        false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                continue;
                            }
                            if (argument_hfa)
                            {
                                u32 float_register = argument_float_register[argument_index - 1];
                                for (u32 part = 0; part < argument_abi.part_count; part += 1)
                                {
                                    CodegenCanonicalAbiPart* abi_part = argument_abi.parts + part;
                                    u32 source_offset = value_offsets[argument.value] + abi_part->value_offset;
                                    if (float_register + part >= 8)
                                    {
                                        result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                        return result;
                                    }
                                    codegen_canonical_a64_frame_float_memory_operation(&buffer, float_register + part, source_offset, abi_part->size, false);
                                }
                                continue;
                            }
                            for (u32 part_index = 0; part_index < part_count; part_index += 1)
                            {
                                u32 source_offset = value_offsets[argument.value] + part_index * 8;
                                if (indirect)
                                {
                                    codegen_canonical_a64_base_address(&buffer, register_index, 28, source_offset);
                                }
                                else
                                {
                                    if (!codegen_canonical_a64_frame_memory_operation(&buffer, register_index, source_offset, 8, false, false))
                                    {
                                        result.error = CODEGEN_ERROR_CAPACITY;
                                        return result;
                                    }
                                }
                                register_index += 1;
                            }
                        }
                        CodegenCanonicalAbiValue call_return_abi =
                            codegen_canonical_aggregate_abi(program, instruction->canonical_type, result.abi, true, false);
                        bool call_indirect_return = call_return_abi.indirect;
                        if (call_indirect_return)
                        {
                            u32 return_offset = result_offset;
                            codegen_canonical_a64_base_address(&buffer, 8, 28, return_offset);
                        }
                        bool indirect_call = callee_type && callee_type->kind == IR_TYPE_POINTER;
                        if (indirect_call)
                        {
                            u32 callee_offset = value_offsets[instruction->operands[0].value];
                            if (!codegen_canonical_a64_frame_memory_operation(&buffer, 16, callee_offset, 8, false, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                            codegen_emit_u32(&buffer, 0xd63f0200);
                        }
                        else
                        {
                            u32 offset = (u32)buffer.count;
                            codegen_emit_u32(&buffer, 0x94000000);
                            result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                .symbol = instruction->symbol,
                                .offset = offset,
                                .kind = CODEGEN_MODULE_RELOCATION_AARCH64_CALL26,
                                .aarch64 = true,
                            };
                        }
                        if (stack_size)
                        {
                            codegen_canonical_a64_adjust_stack(&buffer, stack_size, false);
                        }
                        if (instruction->result.value != IR_ID_UNDERLYING_INVALID)
                        {
                            IrType* return_type = ir_type_from_id(&program->types, instruction->canonical_type);
                            if (return_type && return_type->kind == IR_TYPE_FLOAT)
                            {
                                if (return_type->bit_width != 32 && return_type->bit_width != 64)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                if (!codegen_canonical_a64_frame_float_memory_operation(&buffer, 0, result_offset, return_type->bit_width / 8, true))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                instruction_id = instruction->next;
                                continue;
                            }
                            bool return_hfa = call_return_abi.part_count != 0;
                            for (u32 part = 0; part < call_return_abi.part_count; part += 1)
                            {
                                return_hfa &= codegen_canonical_abi_part_is_float(call_return_abi.parts[part].abi_class);
                            }
                            if (return_hfa)
                            {
                                for (u32 part = 0; part < call_return_abi.part_count; part += 1)
                                {
                                    codegen_canonical_a64_frame_float_memory_operation(&buffer, part, result_offset + call_return_abi.parts[part].value_offset,
                                                                                       call_return_abi.parts[part].size, true);
                                }
                                instruction_id = instruction->next;
                                continue;
                            }
                            u32 return_parts = 0;
                            bool aggregate_return = codegen_canonical_integer_aggregate_parts(program, instruction->canonical_type, &return_parts);
                            if (!call_indirect_return)
                            {
                                c_a64_store(&emitter, 0, result_offset);
                            }
                            if (!call_indirect_return && aggregate_return && return_parts == 2)
                            {
                                if (!codegen_canonical_a64_frame_memory_operation(&buffer, 1, result_offset + 8, 8, true, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_ARRAY)
                    {
                        IrType* array = ir_type_from_id(&program->types, instruction->canonical_type);
                        IrType* element = array ? ir_type_from_id(&program->types, array->element_type) : 0;
                        if (!array || !element || (array->kind != IR_TYPE_ARRAY && array->kind != IR_TYPE_VECTOR) ||
                            instruction->operand_count != array->element_count)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        for (u32 element_index = 0; element_index < instruction->operand_count; element_index += 1)
                        {
                            u64 copied = 0;
                            while (copied < element->layout.size)
                            {
                                u64 remaining = element->layout.size - copied;
                                u64 source_offset = (u64)value_offsets[instruction->operands[element_index].value] + copied;
                                u64 destination_offset = (u64)value_offsets[instruction->result.value] + element_index * element->layout.size + copied;
                                u32 chunk = codegen_canonical_copy_chunk(remaining, source_offset, destination_offset);
                                if (source_offset > UINT32_MAX || destination_offset > UINT32_MAX ||
                                    !codegen_canonical_a64_frame_memory_operation(&buffer, 9, (u32)source_offset, chunk, false, false) ||
                                    !codegen_canonical_a64_frame_memory_operation(&buffer, 9, (u32)destination_offset, chunk, true, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                copied += chunk;
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_AGGREGATE)
                    {
                        IrType* aggregate = ir_type_from_id(&program->types, instruction->canonical_type);
                        if (!aggregate || instruction->operand_count != instruction->immediate_count)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        u64 aggregate_copied = 0;
                        while (aggregate_copied < aggregate->layout.size)
                        {
                            u64 remaining = aggregate->layout.size - aggregate_copied;
                            u64 destination_offset = (u64)result_offset + aggregate_copied;
                            u32 chunk = codegen_canonical_copy_chunk(remaining, destination_offset, destination_offset);
                            if (destination_offset > UINT32_MAX ||
                                !codegen_canonical_a64_frame_memory_operation(&buffer, 31, (u32)destination_offset, chunk, true, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                            aggregate_copied += chunk;
                        }
                        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                        {
                            u64 field_index = instruction->immediates[operand_index];
                            if (field_index >= aggregate->field_count)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrField* field = aggregate->fields + field_index;
                            IrType* field_type = ir_type_from_id(&program->types, field->type);
                            if (!field_type || !field_type->layout.resolved)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            u32 field_offset = result_offset + (u32)field->offset;
                            if (field->is_bit_field)
                            {
                                if (!field->bit_width)
                                {
                                    continue;
                                }
                                u32 field_size = (u32)field_type->layout.size;
                                if (field_size != 1 && field_size != 2 && field_size != 4 && field_size != 8)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                                c_a64_load(&emitter, 9, instruction->operands[operand_index]);
                                if (field->bit_width < 64)
                                {
                                    a64_emit_constant(&buffer, 10, ((u64)1 << field->bit_width) - 1);
                                    codegen_emit_u32(&buffer, 0x8a0a0129);
                                }
                                if (field->bit_offset)
                                {
                                    u32 shift = field->bit_offset;
                                    codegen_emit_u32(&buffer, 0xd3400000 | ((64 - shift) << 16) | ((63 - shift) << 10) | (9 << 5) | 9);
                                }
                                if (!codegen_canonical_a64_frame_memory_operation(&buffer, 10, field_offset, field_size, false, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                codegen_emit_u32(&buffer, 0xaa09014a);
                                if (!codegen_canonical_a64_frame_memory_operation(&buffer, 10, field_offset, field_size, true, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                continue;
                            }
                            u64 field_copied = 0;
                            while (field_copied < field_type->layout.size)
                            {
                                u64 remaining = field_type->layout.size - field_copied;
                                u64 source_offset = (u64)value_offsets[instruction->operands[operand_index].value] + field_copied;
                                u64 destination_offset = (u64)field_offset + field_copied;
                                u32 chunk = codegen_canonical_copy_chunk(remaining, source_offset, destination_offset);
                                if (source_offset > UINT32_MAX || destination_offset > UINT32_MAX ||
                                    !codegen_canonical_a64_frame_memory_operation(&buffer, 9, (u32)source_offset, chunk, false, false) ||
                                    !codegen_canonical_a64_frame_memory_operation(&buffer, 9, (u32)destination_offset, chunk, true, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                field_copied += chunk;
                            }
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_UNARY)
                    {
                        IrType* canonical_unary_type = ir_type_from_id(&program->types, instruction->canonical_type);
                        if (canonical_unary_type && canonical_unary_type->kind == IR_TYPE_VECTOR)
                        {
                            if (!codegen_canonical_a64_vector_operation(&buffer, program, function, instruction, value_offsets))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        c_a64_load(&emitter, 9, instruction->operands[0]);
                        IrType* type = ir_type_from_id(&program->types, instruction->canonical_type);
                        if (instruction->unary_operation == IR_UNARY_BOOLEAN_NOT)
                        {
                            codegen_emit_u32(&buffer, 0x7100013f);
                            codegen_emit_u32(&buffer, 0x1a9f17e9);
                            c_a64_store(&emitter, 9, result_offset);
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (instruction->unary_operation == IR_UNARY_FLOAT_NEGATE)
                        {
                            if (!type || type->kind != IR_TYPE_FLOAT || (type->bit_width != 32 && type->bit_width != 64))
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            codegen_emit_u32(&buffer, type->bit_width == 32 ? 0x52b0000a : 0xd2f0000a);
                            codegen_emit_u32(&buffer, type->bit_width == 32 ? 0x4a0a0129 : 0xca0a0129);
                            c_a64_store(&emitter, 9, result_offset);
                            instruction_id = instruction->next;
                            continue;
                        }
                        u32 operation =
                            instruction->unary_operation == IR_UNARY_INTEGER_NEGATE                 ? (type && type->bit_width > 32 ? 0xcb0903e9 : 0x4b0903e9)
                            : instruction->unary_operation == IR_UNARY_INTEGER_BITWISE_NOT          ? (type && type->bit_width > 32 ? 0xaa2903e9 : 0x2a2903e9)
                            : instruction->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS  ? (type && type->bit_width > 32 ? 0xdac01129 : 0x5ac01129)
                            : instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS ? (type && type->bit_width > 32 ? 0xdac00129 : 0x5ac00129)
                                                                                                    : 0;
                        if (!operation)
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        codegen_emit_u32(&buffer, operation);
                        if (instruction->unary_operation == IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS)
                        {
                            codegen_emit_u32(&buffer, type && type->bit_width > 32 ? 0xdac01129 : 0x5ac01129);
                        }
                        c_a64_store(&emitter, 9, result_offset);
                    }
                    else if (instruction->opcode == IR_OPCODE_BINARY)
                    {
                        IrTypeId operand_type = function->values[instruction->operands[0].value].canonical_type;
                        IrType* operand_type_value = ir_type_from_id(&program->types, operand_type);
                        if (operand_type_value && operand_type_value->kind == IR_TYPE_VECTOR)
                        {
                            if (!codegen_canonical_a64_vector_operation(&buffer, program, function, instruction, value_offsets))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
                        if (operand_type_value && operand_type_value->kind == IR_TYPE_FLOAT)
                        {
                            u32 width = operand_type_value->bit_width;
                            if (width != 32 && width != 64)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            u32 left_offset = value_offsets[instruction->operands[0].value];
                            u32 right_offset = value_offsets[instruction->operands[1].value];
                            u32 float_size = width == 32 ? 4 : 8;
                            if (!codegen_canonical_a64_float_memory_operation(&buffer, 0, left_offset, float_size, false) ||
                                !codegen_canonical_a64_float_memory_operation(&buffer, 1, right_offset, float_size, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                            IrBinaryOperation operation = instruction->binary_operation;
                            if (operation >= IR_BINARY_FLOAT_ADD && operation <= IR_BINARY_FLOAT_DIVIDE)
                            {
                                u32 encoded = operation == IR_BINARY_FLOAT_ADD        ? 0x1e212800
                                              : operation == IR_BINARY_FLOAT_SUBTRACT ? 0x1e213800
                                              : operation == IR_BINARY_FLOAT_MULTIPLY ? 0x1e210800
                                                                                      : 0x1e211800;
                                if (width == 64)
                                {
                                    encoded |= 0x00400000;
                                }
                                codegen_emit_u32(&buffer, encoded);
                                if (!codegen_canonical_a64_float_memory_operation(&buffer, 0, result_offset, float_size, true))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                instruction_id = instruction->next;
                                continue;
                            }
                            u32 condition = operation == IR_BINARY_FLOAT_EQUAL           ? 0
                                            : operation == IR_BINARY_FLOAT_NOT_EQUAL     ? 1
                                            : operation == IR_BINARY_FLOAT_LESS          ? 4
                                            : operation == IR_BINARY_FLOAT_LESS_EQUAL    ? 9
                                            : operation == IR_BINARY_FLOAT_GREATER       ? 12
                                            : operation == IR_BINARY_FLOAT_GREATER_EQUAL ? 10
                                                                                         : UINT32_MAX;
                            if (condition == UINT32_MAX)
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            codegen_emit_u32(&buffer, width == 32 ? 0x1e212000 : 0x1e612000);
                            codegen_emit_u32(&buffer, 0x1a9f07e9 | ((condition ^ 1) << 12));
                            c_a64_store(&emitter, 9, result_offset);
                            instruction_id = instruction->next;
                            continue;
                        }
                        u32 wide_mask = codegen_canonical_register_is_64_bit(program, operand_type) ? 0x80000000 : 0;
                        c_a64_load(&emitter, 9, instruction->operands[0]);
                        c_a64_load(&emitter, 10, instruction->operands[1]);
                        u32 operation = 0;
                        switch (instruction->binary_operation)
                        {
                        case IR_BINARY_INTEGER_ADD:
                            operation = 0x0b0a0129;
                            break;
                        case IR_BINARY_INTEGER_SUBTRACT:
                            operation = 0x4b0a0129;
                            break;
                        case IR_BINARY_INTEGER_MULTIPLY:
                            operation = 0x1b0a7d29;
                            break;
                        case IR_BINARY_SIGNED_DIVIDE:
                            operation = 0x1aca0d29;
                            break;
                        case IR_BINARY_SIGNED_REMAINDER:
                            codegen_emit_u32(&buffer, codegen_canonical_a64_remainder_divide_instruction(true, wide_mask != 0));
                            operation = 0x1b0aa569;
                            break;
                        case IR_BINARY_UNSIGNED_DIVIDE:
                            operation = 0x1aca0929;
                            break;
                        case IR_BINARY_UNSIGNED_REMAINDER:
                            codegen_emit_u32(&buffer, codegen_canonical_a64_remainder_divide_instruction(false, wide_mask != 0));
                            operation = 0x1b0aa569;
                            break;
                        case IR_BINARY_SHIFT_LEFT:
                            operation = 0x1aca2129;
                            break;
                        case IR_BINARY_SIGNED_SHIFT_RIGHT:
                            operation = 0x1aca2929;
                            break;
                        case IR_BINARY_UNSIGNED_SHIFT_RIGHT:
                            operation = 0x1aca2529;
                            break;
                        case IR_BINARY_INTEGER_BITWISE_AND:
                            operation = 0x0a0a0129;
                            break;
                        case IR_BINARY_INTEGER_BITWISE_OR:
                            operation = 0x2a0a0129;
                            break;
                        case IR_BINARY_INTEGER_BITWISE_XOR:
                            operation = 0x4a0a0129;
                            break;
                        case IR_BINARY_INTEGER_EQUAL:
                        case IR_BINARY_POINTER_EQUAL:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9f17e9;
                            break;
                        case IR_BINARY_INTEGER_NOT_EQUAL:
                        case IR_BINARY_POINTER_NOT_EQUAL:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9f07e9;
                            break;
                        case IR_BINARY_SIGNED_LESS:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9fa7e9;
                            break;
                        case IR_BINARY_SIGNED_LESS_EQUAL:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9fc7e9;
                            break;
                        case IR_BINARY_SIGNED_GREATER:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9fd7e9;
                            break;
                        case IR_BINARY_SIGNED_GREATER_EQUAL:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9fb7e9;
                            break;
                        case IR_BINARY_UNSIGNED_LESS:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9f27e9;
                            break;
                        case IR_BINARY_UNSIGNED_LESS_EQUAL:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9f87e9;
                            break;
                        case IR_BINARY_UNSIGNED_GREATER:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9f97e9;
                            break;
                        case IR_BINARY_UNSIGNED_GREATER_EQUAL:
                            codegen_emit_u32(&buffer, 0x6b0a013f | wide_mask);
                            operation = 0x1a9f37e9;
                            break;
                        default:
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        codegen_emit_u32(&buffer, operation | wide_mask);
                        c_a64_store(&emitter, 9, result_offset);
                    }
                    else if (instruction->opcode == IR_OPCODE_LABEL_ADDRESS)
                    {
                        if (instruction->target_count != 1)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        if (!c_branch_patch_push(&emitter, (CCanonicalBranchPatch){
                            .target = instruction->targets[0],
                            .offset = (u32)buffer.count,
                            .aarch64 = true,
                            .label_address = true,
                            }))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        emitter.branch_patches[emitter.branch_patch_count - 1].secondary_offset = (u32)buffer.count + 4;
                        // Materialize the byte delta from this ADR to the
                        // target block.  Unlike ADRP, this remains correct
                        // when the text section is placed at a non-page
                        // aligned address or concatenated after another
                        // object: both addresses move by the same amount.
                        codegen_emit_u32(&buffer, 0x10000009);
                        codegen_emit_u32(&buffer, 0xd280000a);
                        codegen_emit_u32(&buffer, 0xf2a0000a);
                        codegen_emit_u32(&buffer, 0xf2c0000a);
                        codegen_emit_u32(&buffer, 0xf2e0000a);
                        codegen_emit_u32(&buffer, 0x8b0a0129);
                        c_a64_store(&emitter, 9, result_offset);
                    }
                    else if (instruction->opcode == IR_OPCODE_BRANCH)
                    {
                        if (!c_branch_patch_push(&emitter, (CCanonicalBranchPatch){
                            .target = instruction->targets[0],
                            .offset = (u32)buffer.count,
                            .aarch64 = true,
                            }))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        codegen_emit_u32(&buffer, 0x14000000);
                    }
                    else if (instruction->opcode == IR_OPCODE_INDIRECT_BRANCH)
                    {
                        if (instruction->operand_count != 1)
                        {
                            result.error = CODEGEN_ERROR_INVALID_IR;
                            return result;
                        }
                        c_a64_load(&emitter, 9, instruction->operands[0]);
                        codegen_emit_u32(&buffer, 0xd61f0120);
                    }
                    else if (instruction->opcode == IR_OPCODE_BRANCH_IF)
                    {
                        c_a64_load(&emitter, 9, instruction->operands[0]);
                        codegen_emit_u32(&buffer, 0xf100013f);
                        codegen_emit_u32(&buffer, 0x54000040);
                        if (!c_branch_patch_push(&emitter, (CCanonicalBranchPatch){
                            .target = instruction->targets[0],
                            .offset = (u32)buffer.count,
                            .aarch64 = true,
                            }))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        codegen_emit_u32(&buffer, 0x14000000);
                        if (!c_branch_patch_push(&emitter, (CCanonicalBranchPatch){
                            .target = instruction->targets[1],
                            .offset = (u32)buffer.count,
                            .aarch64 = true,
                            }))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        codegen_emit_u32(&buffer, 0x14000000);
                    }
                    else if (instruction->opcode == IR_OPCODE_SWITCH)
                    {
                        c_a64_load(&emitter, 9, instruction->operands[0]);
                        for (u32 case_index = 0; case_index < instruction->immediate_count; case_index += 1)
                        {
                            u64 immediate = instruction->immediates[case_index];
                            codegen_emit_u32(&buffer, 0xd280000a | ((u32)(immediate & 0xffff) << 5));
                            codegen_emit_u32(&buffer, 0xf2a0000a | ((u32)((immediate >> 16) & 0xffff) << 5));
                            codegen_emit_u32(&buffer, 0xf2c0000a | ((u32)((immediate >> 32) & 0xffff) << 5));
                            codegen_emit_u32(&buffer, 0xf2e0000a | ((u32)((immediate >> 48) & 0xffff) << 5));
                            codegen_emit_u32(&buffer, 0xeb0a013f);
                            codegen_emit_u32(&buffer, 0x54000041);
                            if (!c_branch_patch_push(&emitter, (CCanonicalBranchPatch){
                                .target = instruction->targets[case_index],
                                .offset = (u32)buffer.count,
                                .aarch64 = true,
                                }))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                            codegen_emit_u32(&buffer, 0x14000000);
                        }
                        if (!c_branch_patch_push(&emitter, (CCanonicalBranchPatch){
                            .target = instruction->targets[instruction->target_count - 1],
                            .offset = (u32)buffer.count,
                            .aarch64 = true,
                            }))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        codegen_emit_u32(&buffer, 0x14000000);
                    }
                    else if (instruction->opcode == IR_OPCODE_INLINE_ASSEMBLY)
                    {
                        IrInstructionExtra asm_extra = ir_instruction_extra(function, ir_instruction_self_id(function, instruction));
                        bool empty = !asm_extra.literal.length;
                        bool brk = string_equal(asm_extra.literal, S8("brk #0"));
                        u32 nop_count = codegen_canonical_a64_nop_count(asm_extra.literal);
                        bool nop = nop_count != 0;
                        bool yield = string_equal(asm_extra.literal, S8("yield"));
                        bool wait_event = string_equal(asm_extra.literal, S8("wfe"));
                        bool wait_interrupt = string_equal(asm_extra.literal, S8("wfi"));
                        bool send_event = string_equal(asm_extra.literal, S8("sev"));
                        bool send_event_local = string_equal(asm_extra.literal, S8("sevl"));
                        u32 jump_target_index = 0;
                        bool jump_label = codegen_inline_assembly_jump_target(function, instruction, asm_extra.literal, S8("b %l"), &jump_target_index);
                        if ((!empty && !brk && !nop && !yield && !wait_event && !wait_interrupt && !send_event && !send_event_local && !jump_label) ||
                            (jump_label && instruction->target_count < 2) || instruction->operand_count != instruction->immediate_count ||
                            asm_extra.operand_name_count != instruction->operand_count ||
                            (instruction->operand_count && (!instruction->operands || !instruction->immediates || !asm_extra.operand_names)) ||
                            (asm_extra.clobber_count && !asm_extra.clobbers))
                        {
                            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                            return result;
                        }
                        u32* asm_registers = arena_allocate(arena, u32, instruction->operand_count ? instruction->operand_count : 1);
                        bool* asm_indirect = arena_allocate(arena, bool, instruction->operand_count ? instruction->operand_count : 1);
                        bool used_asm_registers[8] = {0};
                        // x16 is a valid eighth operand register.  Keep the
                        // address scratch separate so an indirect eighth
                        // output cannot overwrite its own pointer.
                        u32 asm_address_register = 17;
                        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                        {
                            u64 constraint = instruction->immediates[operand_index];
                            if (!codegen_inline_assembly_constraint_shape_valid(constraint, operand_index, instruction->operand_count, 0) ||
                                (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) != IR_INLINE_ASSEMBLY_CONSTRAINT_R)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrValueId operand = instruction->operands[operand_index];
                            if (operand.value >= function->value_count || function->values[operand.value].definition.value >= function->instruction_count)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrInstruction* definition = function->instructions + function->values[operand.value].definition.value;
                            asm_indirect[operand_index] = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                                          definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE;
                            IrType* operand_type = ir_type_from_id(&program->types, function->values[operand.value].canonical_type);
                            if ((constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0)
                            {
                                if (codegen_inline_assembly_type_class(operand_type) == IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID)
                                {
                                    result.error = CODEGEN_ERROR_INVALID_IR;
                                    return result;
                                }
                                u32 match_index = IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(constraint);
                                IrValueId output = instruction->operands[match_index];
                                u64 output_constraint = instruction->immediates[match_index];
                                IrType* output_type = ir_type_from_id(&program->types, function->values[output.value].canonical_type);
                                if ((output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) == 0 ||
                                    (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) != 0 ||
                                    (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) !=
                                        (output_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK) ||
                                    !codegen_inline_assembly_types_compatible(output_type, operand_type))
                                {
                                    result.error = CODEGEN_ERROR_INVALID_IR;
                                    return result;
                                }
                                for (u32 previous_index = 0; previous_index < operand_index; previous_index += 1)
                                {
                                    u64 previous_constraint = instruction->immediates[previous_index];
                                    if ((previous_constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH) != 0 &&
                                        IR_INLINE_ASSEMBLY_CONSTRAINT_MATCH_INDEX(previous_constraint) == match_index)
                                    {
                                        result.error = CODEGEN_ERROR_INVALID_IR;
                                        return result;
                                    }
                                }
                                asm_registers[operand_index] = asm_registers[match_index];
                            }
                            else
                            {
                                u32 register_index = 0;
                                bool found = false;
                                for (u32 candidate = 0; candidate < BUSTER_ARRAY_LENGTH(used_asm_registers); candidate += 1)
                                {
                                    if (!used_asm_registers[candidate])
                                    {
                                        used_asm_registers[candidate] = true;
                                        register_index = 9 + candidate;
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                    return result;
                                }
                                asm_registers[operand_index] = register_index;
                            }
                            if (!operand_type || !operand_type->layout.resolved || operand_type->layout.size > UINT32_MAX ||
                                !codegen_canonical_a64_asm_memory_width((u32)operand_type->layout.size))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            if ((constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) == 0 ||
                                (constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_READ_WRITE) != 0)
                            {
                                if (asm_indirect[operand_index])
                                {
                                    if (!codegen_canonical_a64_frame_memory_operation(&buffer, asm_address_register, value_offsets[operand.value], 8, false, false) ||
                                        !codegen_canonical_a64_memory_operation_base(&buffer, asm_registers[operand_index], 0, (u32)operand_type->layout.size, false, false,
                                                                                      asm_address_register))
                                    {
                                        result.error = CODEGEN_ERROR_CAPACITY;
                                        return result;
                                    }
                                }
                                else if (!codegen_canonical_a64_frame_memory_operation(&buffer, asm_registers[operand_index], value_offsets[operand.value],
                                                                                         (u32)operand_type->layout.size, false, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                        }
                        if (!empty)
                        {
                            if (nop)
                            {
                                for (u32 nop_index = 0; nop_index < nop_count; nop_index += 1)
                                {
                                    codegen_emit_u32(&buffer, 0xd503201f);
                                }
                            }
                            else if (!jump_label)
                            {
                                codegen_emit_u32(&buffer, brk              ? 0xd4200000
                                                          : yield          ? 0xd503203f
                                                          : wait_event     ? 0xd503205f
                                                          : wait_interrupt ? 0xd503207f
                                                          : send_event     ? 0xd503209f
                                                                           : 0xd50320bf);
                            }
                        }
                        for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
                        {
                            u64 constraint = instruction->immediates[operand_index];
                            if ((constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_OUTPUT) == 0)
                            {
                                continue;
                            }
                            IrValueId place_id = instruction->operands[operand_index];
                            if (place_id.value >= function->value_count || function->values[place_id.value].definition.value >= function->instruction_count)
                            {
                                result.error = CODEGEN_ERROR_INVALID_IR;
                                return result;
                            }
                            IrType* output_type = ir_type_from_id(&program->types, function->values[place_id.value].canonical_type);
                            if (!output_type || !output_type->layout.resolved || output_type->layout.size > UINT32_MAX ||
                                !codegen_canonical_a64_asm_memory_width((u32)output_type->layout.size))
                            {
                                result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            IrInstruction* definition = function->instructions + function->values[place_id.value].definition.value;
                            bool output_indirect = definition->opcode == IR_OPCODE_GLOBAL || definition->opcode == IR_OPCODE_INDEX ||
                                                   definition->opcode == IR_OPCODE_FIELD || definition->opcode == IR_OPCODE_DEREFERENCE;
                            if (output_indirect)
                            {
                                if (!codegen_canonical_a64_frame_memory_operation(&buffer, asm_address_register, value_offsets[place_id.value], 8, false, false) ||
                                    !codegen_canonical_a64_memory_operation_base(&buffer, asm_registers[operand_index], 0,
                                                                                   (u32)output_type->layout.size, true, false, asm_address_register))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                            else if (!codegen_canonical_a64_frame_memory_operation(&buffer, asm_registers[operand_index], value_offsets[place_id.value],
                                                                                   (u32)output_type->layout.size, true, false))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                        }
                        if (jump_label)
                        {
                            if (!c_branch_patch_push(&emitter, (CCanonicalBranchPatch){
                                .target = instruction->targets[jump_target_index],
                                .offset = (u32)buffer.count,
                                .aarch64 = true,
                                }))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                            codegen_emit_u32(&buffer, 0x14000000);
                        }
                        else if (instruction->target_count)
                        {
                            if (!c_branch_patch_push(&emitter, (CCanonicalBranchPatch){
                                .target = instruction->targets[0],
                                .offset = (u32)buffer.count,
                                .aarch64 = true,
                                }))
                            {
                                result.error = CODEGEN_ERROR_CAPACITY;
                                return result;
                            }
                            codegen_emit_u32(&buffer, 0x14000000);
                        }
                    }
                    else if (instruction->opcode == IR_OPCODE_DEBUG_TRAP)
                    {
                        codegen_emit_u32(&buffer, 0xd4200000);
                    }
                    else if (instruction->opcode == IR_OPCODE_UNREACHABLE)
                    {
                        codegen_emit_u32(&buffer, 0xd4200000);
                    }
                    else if (instruction->opcode == IR_OPCODE_RETURN)
                    {
                        if (instruction->operand_count)
                        {
                            IrValueId return_value = instruction->operands[0];
                            IrType* return_type = ir_type_from_id(&program->types, function->values[return_value.value].canonical_type);
                            u32 return_parts = 0;
                            bool aggregate_return =
                                codegen_canonical_integer_aggregate_parts(program, function->values[return_value.value].canonical_type, &return_parts);
                            CodegenCanonicalAbiValue aggregate_return_abi =
                                codegen_canonical_aggregate_abi(program, function->values[return_value.value].canonical_type, result.abi, true, false);
                            bool return_hfa = aggregate_return_abi.part_count != 0;
                            for (u32 part = 0; part < aggregate_return_abi.part_count; part += 1)
                            {
                                return_hfa &= codegen_canonical_abi_part_is_float(aggregate_return_abi.parts[part].abi_class);
                            }
                            if (return_hfa)
                            {
                                for (u32 part = 0; part < aggregate_return_abi.part_count; part += 1)
                                {
                                    CodegenCanonicalAbiPart* abi_part = aggregate_return_abi.parts + part;
                                    codegen_canonical_a64_frame_float_memory_operation(
                                        &buffer, part, value_offsets[return_value.value] + abi_part->value_offset, abi_part->size, false);
                                }
                                if (!codegen_epilog_offset_append(descriptor, function->instruction_count,
                                                                  (u32)buffer.count - descriptor->code_offset))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                codegen_emit_u32(&buffer, 0x9100039f);
                                if (!codegen_canonical_a64_memory_operation(&buffer, 28, aarch64_frame_base_save_offset, 8, false, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                if (frame_size)
                                {
                                    codegen_canonical_a64_adjust_stack_described(&buffer, frame_size, false, 0, 0, windows_aarch64);
                                }
                                codegen_emit_u32(&buffer, 0xa8c17bfd);
                                codegen_emit_u32(&buffer, 0xd65f03c0);
                                instruction_id = instruction->next;
                                continue;
                            }
                            if (aarch64_indirect_return)
                            {
                                if (!aggregate_return || !return_type)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                if (!codegen_canonical_a64_frame_memory_operation(&buffer, 10, aarch64_hidden_result_offset, 8, false, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                                u64 copied = 0;
                                while (copied < return_type->layout.size)
                                {
                                    u64 remaining = return_type->layout.size - copied;
                                    u64 source_offset = (u64)value_offsets[return_value.value] + copied;
                                    u32 chunk = codegen_canonical_copy_chunk(remaining, source_offset, copied);
                                    if (source_offset > UINT32_MAX || copied > UINT32_MAX)
                                    {
                                        result.error = CODEGEN_ERROR_CAPACITY;
                                        return result;
                                    }
                                    a64_emit_load_pointer_offset(&buffer, 9, 28, (u32)source_offset, chunk);
                                    a64_emit_store_pointer_offset(&buffer, 9, 10, (u32)copied, chunk);
                                    copied += chunk;
                                }
                            }
                            else if (return_type && return_type->kind == IR_TYPE_FLOAT)
                            {
                                if (return_type->bit_width != 32 && return_type->bit_width != 64)
                                {
                                    result.error = CODEGEN_ERROR_UNSUPPORTED_ABI;
                                    return result;
                                }
                                if (!codegen_canonical_a64_frame_float_memory_operation(&buffer, 0, value_offsets[return_value.value],
                                                                                        return_type->bit_width / 8, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                            else
                            {
                                c_a64_load(&emitter, 0, return_value);
                            }
                            if (!aarch64_indirect_return && aggregate_return && return_parts == 2)
                            {
                                if (!codegen_canonical_a64_frame_memory_operation(&buffer, 1, value_offsets[return_value.value] + 8, 8, false, false))
                                {
                                    result.error = CODEGEN_ERROR_CAPACITY;
                                    return result;
                                }
                            }
                        }
                        if (!codegen_epilog_offset_append(descriptor, function->instruction_count,
                                                          (u32)buffer.count - descriptor->code_offset))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        codegen_emit_u32(&buffer, 0x9100039f);
                        if (!codegen_canonical_a64_memory_operation(&buffer, 28, aarch64_frame_base_save_offset, 8, false, false))
                        {
                            result.error = CODEGEN_ERROR_CAPACITY;
                            return result;
                        }
                        if (frame_size)
                        {
                            codegen_canonical_a64_adjust_stack_described(&buffer, frame_size, false, 0, 0, windows_aarch64);
                        }
                        codegen_emit_u32(&buffer, 0xa8c17bfd);
                        codegen_emit_u32(&buffer, 0xd65f03c0);
                    }
                    else
                    {
                        result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                        return result;
                    }
                }
                if (buffer.error != CODEGEN_ERROR_NONE)
                {
                    result.error = buffer.error;
                    return result;
                }
                instruction_id = instruction->next;
            }
        }
        for (u32 patch_index = 0; patch_index < emitter.branch_patch_count; patch_index += 1)
        {
            CCanonicalBranchPatch patch = emitter.branch_patches[patch_index];
            if (patch.target.value >= function->block_count)
            {
                result.error = CODEGEN_ERROR_INVALID_IR;
                return result;
            }
            s64 delta = (s64)block_offsets[patch.target.value] - (s64)patch.offset;
            if (!patch.aarch64)
            {
                delta -= 4;
                if (delta < INT32_MIN || delta > INT32_MAX)
                {
                    result.error = CODEGEN_ERROR_CAPACITY;
                    return result;
                }
                s32 encoded = (s32)delta;
                memcpy(buffer.bytes + patch.offset, &encoded, sizeof(encoded));
            }
            else
            {
                if (patch.label_address)
                {
                    if (!patch.secondary_offset || patch.secondary_offset < patch.offset || patch.secondary_offset > buffer.count || buffer.count - patch.secondary_offset < 20)
                    {
                        result.error = CODEGEN_ERROR_CAPACITY;
                        return result;
                    }
                    s64 label_address_delta = (s64)block_offsets[patch.target.value] - (s64)patch.offset;
                    u64 bits = (u64)label_address_delta;
                    u32 movz = UINT32_C(0xd280000a) | ((u32)(bits & 0xffff) << 5);
                    u32 movk16 = UINT32_C(0xf2a0000a) | ((u32)((bits >> 16) & 0xffff) << 5);
                    u32 movk32 = UINT32_C(0xf2c0000a) | ((u32)((bits >> 32) & 0xffff) << 5);
                    u32 movk48 = UINT32_C(0xf2e0000a) | ((u32)((bits >> 48) & 0xffff) << 5);
                    memcpy(buffer.bytes + patch.secondary_offset, &movz, sizeof(movz));
                    memcpy(buffer.bytes + patch.secondary_offset + 4, &movk16, sizeof(movk16));
                    memcpy(buffer.bytes + patch.secondary_offset + 8, &movk32, sizeof(movk32));
                    memcpy(buffer.bytes + patch.secondary_offset + 12, &movk48, sizeof(movk48));
                    continue;
                }
                if ((delta & 3) || (patch.conditional ? delta < -(1 << 20) || delta >= (1 << 20) : delta < -(1 << 27) || delta >= (1 << 27)))
                {
                    result.error = CODEGEN_ERROR_CAPACITY;
                    return result;
                }
                u32 instruction = 0;
                memcpy(&instruction, buffer.bytes + patch.offset, sizeof(instruction));
                u32 immediate = (u32)(delta >> 2);
                instruction |= patch.conditional ? (immediate & 0x7ffff) << 5 : immediate & 0x03ffffff;
                memcpy(buffer.bytes + patch.offset, &instruction, sizeof(instruction));
            }
        }
        u32 kept_label_address_count = 0;
        for (u32 side_index = 0; side_index < label_address_relocation_count; side_index += 1)
        {
            u32 relocation_index = label_address_relocation_indices[side_index];
            CodegenModuleRelocation* relocation = result.relocations + relocation_index;
            if (relocation->symbol.value != function->symbol.value)
            {
                label_address_relocation_indices[kept_label_address_count++] = relocation_index;
                continue;
            }
            if (relocation->label_block.value >= function->block_count)
            {
                result.error = CODEGEN_ERROR_INVALID_IR;
                return result;
            }
            s64 block_addend = (s64)block_offsets[relocation->label_block.value] - (s64)descriptor->code_offset;
            if ((block_addend > 0 && relocation->addend > INT64_MAX - block_addend) ||
                (block_addend < 0 && relocation->addend < INT64_MIN - block_addend))
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
            relocation->addend += block_addend;
            relocation->label_address = false;
        }
        label_address_relocation_count = kept_label_address_count;
        descriptor->code_size = (u32)buffer.count - descriptor->code_offset;
        if (options.debug_info)
        {
            codegen_record_canonical_locations(&result, function, value_offsets, block_offsets, descriptor->code_offset, (u32)buffer.count, target, frame_size,
                                               (s32)canonical_x64_frame_base_offset, debug_location_capacity);
        }
    }
    for (u32 relocation_index = 0; relocation_index < result.relocation_count; relocation_index += 1)
    {
        if (result.relocations[relocation_index].label_address)
        {
            result.error = CODEGEN_ERROR_INVALID_IR;
            return result;
        }
    }
    for (u32 assembly_index = 0; assembly_index < module->assembly_count; assembly_index += 1)
    {
        if (!codegen_emit_global_assembly(arena, program, module->assemblies[assembly_index], target, &buffer, &result))
        {
            result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
            return result;
        }
    }
    while (result.function_count < result.entry_count)
    {
        u32 function_index = result.function_count;
        CodegenModuleEntry* entry = result.entries + function_index;
        u32 end = function_index + 1 < result.entry_count ? result.entries[function_index + 1].offset : (u32)buffer.count;
        result.functions[result.function_count++] = (CodegenFunctionDescriptor){
            .symbol = entry->symbol,
            .code_offset = entry->offset,
            .code_size = end - entry->offset,
        };
    }
    result.code = (ByteSlice){
        .pointer = buffer.bytes,
        .length = buffer.count,
    };
    result.statistics.code_bytes = result.code.length;
    result.error = buffer.error;
    return result;
}

CodegenModule codegen_generate_canonical_module(Arena* arena, IrProgram* program, IrModule* module, Target target, CodegenModuleOptions options)
{
    CodegenModule result = {
        .ir_module = module,
        .abi = codegen_abi_for_target(target),
    };
    if (!arena || !program || !module || result.abi >= CODEGEN_ABI_COUNT || (target.cpu_arch != CPU_ARCH_X86_64 && target.cpu_arch != CPU_ARCH_AARCH64))
    {
        result.error = CODEGEN_ERROR_UNSUPPORTED_TARGET;
        return result;
    }
    codegen_prewarm_for_target(target);
    // ABI records and the target-for-ABI cache are mutable on first use, and an
    // attempt must not be the thing that fills a cache the next attempt reads.
    // Freezing both here also keeps them out of the rewind below.
    ir_prepare_program_abi(program, codegen_canonical_ir_abi_convention(result.abi));
    if (!options.assume_validated)
    {
        IrValidationResult validation = ir_validate_canonical_module(program, module);
        if (validation.error != IR_VALIDATION_NONE)
        {
            result.error = CODEGEN_ERROR_INVALID_IR;
            return result;
        }
    }
    CodegenCanonicalX64F80Cache f80_cache = {0};
    CodegenX64MetadataCache* x64_metadata_cache = 0;
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        f80_cache = codegen_canonical_x64_f80_cache_initialize(arena, program);
        if (f80_cache.allocation_failed)
        {
            result.error = CODEGEN_ERROR_CAPACITY;
            return result;
        }
        // Every row the cache holds is keyed on instruction shape alone, so it
        // stays valid across a retry and is allocated before the temporal scope
        // opens rather than being rebuilt by each attempt. A caller may pass a
        // deliberately small arena to exercise a capacity-limited module, so a
        // cache that does not fit is skipped instead of failing the module:
        // emission treats an absent cache as a miss on every query.
        u32 template_capacity = codegen_canonical_x64_template_capacity(module->function_count);
        u64 cache_size = sizeof(CodegenX64MetadataCache) + (u64)template_capacity * sizeof(CodegenX64TemplateCacheEntry);
        if (module->function_count && cache_size <= arena->reserved_size - BUSTER_MIN(arena->position, arena->reserved_size))
        {
            x64_metadata_cache = arena_allocate(arena, CodegenX64MetadataCache, 1);
            memset(x64_metadata_cache, 0, sizeof(*x64_metadata_cache));
            x64_metadata_cache->templates = arena_allocate(arena, CodegenX64TemplateCacheEntry, template_capacity);
            memset(x64_metadata_cache->templates, 0, sizeof(*x64_metadata_cache->templates) * template_capacity);
            x64_metadata_cache->template_mask = template_capacity - 1u;
        }
    }
    // The code buffer is reserved at a flat rate per IR instruction, which is
    // an estimate rather than a bound: an instruction that moves an aggregate
    // encodes a load and a store per eightbyte, so its size grows with the type
    // and a module holding a few wide values by value outgrows the rate.
    // Measuring the excess up front costs a walk of every function's operands
    // on every module, wide values or not, and that walk is a percent of
    // compile throughput. Generating the module again with twice the room costs
    // nothing until it is needed. The attempt owns nothing outside this arena
    // beyond the caches prepared above, so rewinding it is the whole undo, and
    // the reserve's own `UINT32_MAX` ceiling ends the doubling.
    TemporalArena attempt_scope = arena_begin_temporal(arena);
    for (u64 capacity_scale = 1;; capacity_scale *= 2)
    {
        bool code_buffer_exhausted = false;
        result = codegen_generate_canonical_module_attempt(arena, program, &f80_cache, module, target, options, capacity_scale, &code_buffer_exhausted,
                                                           x64_metadata_cache);
        // Every other capacity failure -- a frame displacement out of range, a
        // frame past `UINT32_MAX`, a reserve that cannot be addressed -- is one
        // more room cannot fix, and is reported as it stands.
        if (!code_buffer_exhausted)
        {
            return result;
        }
        scratch_end(attempt_scope);
    }
}

CodegenExecutable codegen_make_executable(CodegenFunction function)
{
    CodegenExecutable result = {0};
    if (function.error != CODEGEN_ERROR_NONE || !function.code.length)
    {
        result.error = function.error ? function.error : CODEGEN_ERROR_INVALID_IR;
        return result;
    }
    u64 page_size = os_get_page_size();
    u64 data_offset = (function.code.length + 15) & ~(u64)15;
    u64 image_size = data_offset + function.read_only_data.length;
    u64 allocation_size = (image_size + page_size - 1) & ~(page_size - 1);
    void* address = os_reserve(0, allocation_size, (ProtectionFlags){.read = 1, .write = 1}, (MapFlags){.priv = 1, .anonymous = 1});
    if (!address)
    {
        result.error = CODEGEN_ERROR_EXECUTABLE_MEMORY;
        return result;
    }
    memcpy(address, function.code.pointer, function.code.length);
    memcpy((u8*)address + data_offset, function.read_only_data.pointer, function.read_only_data.length);
    for (CodegenDataRelocation* relocation = function.first_data_relocation; relocation; relocation = relocation->next)
    {
        u8* patch = (u8*)address + relocation->code_offset;
        u8* target = (u8*)address + data_offset + relocation->data_offset;
        if (relocation->kind == CODEGEN_DATA_RELOCATION_X86_64_PC32)
        {
            s64 displacement = target - (patch + 4);
            if (displacement < INT32_MIN || displacement > INT32_MAX)
            {
                os_unreserve(address, allocation_size);
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
            s32 displacement_32 = (s32)displacement;
            memcpy(patch, &displacement_32, sizeof(displacement_32));
        }
        else if (relocation->kind == CODEGEN_DATA_RELOCATION_ABSOLUTE64)
        {
            u64 value = (u64)(uintptr_t)target;
            memcpy(patch, &value, sizeof(value));
        }
        else
        {
            os_unreserve(address, allocation_size);
            result.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
            return result;
        }
    }
    if (!os_commit(address, allocation_size, (ProtectionFlags){.read = 1, .execute = 1}, false))
    {
        os_unreserve(address, allocation_size);
        result.error = CODEGEN_ERROR_EXECUTABLE_MEMORY;
        return result;
    }
    if (!os_flush_instruction_cache(address, image_size))
    {
        os_unreserve(address, allocation_size);
        result.error = CODEGEN_ERROR_EXECUTABLE_MEMORY;
        return result;
    }
    result.address = address;
    result.allocation_size = allocation_size;
    return result;
}

void codegen_release_executable(CodegenExecutable executable)
{
    if (executable.address && executable.allocation_size)
    {
        os_unreserve(executable.address, executable.allocation_size);
    }
}
