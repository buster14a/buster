// Canonical-IR-to-native-code orchestration: one call to
// codegen_generate_canonical_module near the bottom of the file turns an
// IrModule into a CodegenModule — code bytes, global data images,
// relocations, unwind actions, debug locations, and statistics — for x86-64
// and AArch64. Every public allocator spelling routes through machine
// selection, placement, and metadata-backed emission. The legacy NONE
// spelling is an alias for MIR_STACK; a machine failure is returned to the
// caller and never rerouted to the direct canonical emitter.
//
// Nearly everything here sits under one function:
// codegen_generate_canonical_module_attempt lays out global data (read-only
// / writable / thread-local / zero-fill images plus initializer
// relocations), then per function runs the machine path. The direct-emitter
// body is removed at cutover. The remaining ABI and metadata helpers have
// machine, assembly, or focused regression consumers.
// codegen_generate_canonical_module wraps the attempt in a retry loop
// that grows the code-buffer capacity scale when an attempt runs out.
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
//   codegen_x64_emit_windows_stack_allocate,    shared machine stack probes
//   codegen_a64_windows_large_stack_adjust      and unwind descriptions
//   codegen_global_assembly_*,                   module-level asm: directives
//   codegen_emit_global_assembly                 here, instructions through
//                                                assembly_encode, relocations
//                                                into the module
//   a64_emit_*, codegen_canonical_a64_*          AArch64 stack probes and
//                                                checked address helpers
//   codegen_slot_costs_build,                    the attempt's per-type frame
//   codegen_record_machine_line_marks            slot table and the line rows
//                                                of a machine-emitted function
//   codegen_machine_debug_index_build,           MIR debug values to native
//   codegen_machine_debug_reference_timeline,    location ranges, event-driven:
//   codegen_record_machine_locations             per-function indexes, one
//                                                change-point timeline per
//                                                referenced virtual register,
//                                                seeds clipped from them

#include <buster/lib/compiler/codegen/codegen_internal.h>
#include <buster/lib/compiler/codegen/bootstrap_trace.h>

bool codegen_module_relocation_kind_valid(u8 kind)
{
    return kind < (u8)CODEGEN_MODULE_RELOCATION_COUNT;
}

bool codegen_module_relocation_kind_is_aarch64(u8 kind)
{
    switch (kind)
    {
    case CODEGEN_MODULE_RELOCATION_AARCH64_CALL26:
    case CODEGEN_MODULE_RELOCATION_AARCH64_BRANCH26:
    case CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP:
    case CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_INDEX_LO12:
    case CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_OFFSET12:
    case CODEGEN_MODULE_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12:
    case CODEGEN_MODULE_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12:
    case CODEGEN_MODULE_RELOCATION_AARCH64_MACH_TLVP_PAGE21:
    case CODEGEN_MODULE_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12:
    case CODEGEN_MODULE_RELOCATION_AARCH64_MACH_PAGE21:
    case CODEGEN_MODULE_RELOCATION_AARCH64_MACH_PAGEOFF12:
        return true;
    default:
        return false;
    }
}

bool codegen_module_relocation_kind_is_absolute(u8 kind)
{
    return kind == CODEGEN_MODULE_RELOCATION_ABSOLUTE32 || kind == CODEGEN_MODULE_RELOCATION_ABSOLUTE64;
}

bool codegen_module_relocation_kind_is_thread_local(u8 kind)
{
    switch (kind)
    {
    case CODEGEN_MODULE_RELOCATION_X86_64_TPOFF32:
    case CODEGEN_MODULE_RELOCATION_X86_64_GOTTPOFF:
    case CODEGEN_MODULE_RELOCATION_X86_64_TLSGD:
    case CODEGEN_MODULE_RELOCATION_X86_64_PE_TLS_INDEX_PC32:
    case CODEGEN_MODULE_RELOCATION_PE_TLS_OFFSET32:
    case CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP:
    case CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_INDEX_LO12:
    case CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_OFFSET12:
    case CODEGEN_MODULE_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12:
    case CODEGEN_MODULE_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12:
    case CODEGEN_MODULE_RELOCATION_X86_64_MACH_TLV_PC32:
    case CODEGEN_MODULE_RELOCATION_AARCH64_MACH_TLVP_PAGE21:
    case CODEGEN_MODULE_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12:
        return true;
    default:
        return false;
    }
}

bool codegen_module_relocation_kind_is_thread_local_low(u8 kind)
{
    return kind == CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_INDEX_LO12 ||
           kind == CODEGEN_MODULE_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12 ||
           kind == CODEGEN_MODULE_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12;
}

bool codegen_module_relocation_kind_is_thread_local_index(u8 kind)
{
    return kind == CODEGEN_MODULE_RELOCATION_X86_64_PE_TLS_INDEX_PC32 ||
           kind == CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP ||
           kind == CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_INDEX_LO12;
}

bool codegen_module_relocation_valid(CodegenModuleRelocation* relocation)
{
    if (!relocation || !codegen_module_relocation_kind_valid(relocation->kind) ||
        relocation->source >= CODEGEN_MODULE_RELOCATION_SOURCE_COUNT)
    {
        return false;
    }

    // label_address is independent payload, but only ABSOLUTE64 has the width
    // required to carry a static label address.
    return !relocation->label_address || relocation->kind == CODEGEN_MODULE_RELOCATION_ABSOLUTE64;
}

#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/integer.h>
#include <buster/lib/os.h>
#include <buster/lib/string.h>

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
    // Zero-operand hints still pass through the general template resolver
    // when operands, physical clobbers or asm-goto edges give the operation
    // allocator-visible state.  The shared assembler, not the legacy direct
    // emitter, owns their bytes once the transaction is represented in MIR.
    S8_INITIALIZER("nop"), S8_INITIALIZER("pause"),
    S8_INITIALIZER("mov"), S8_INITIALIZER("movb"), S8_INITIALIZER("movw"), S8_INITIALIZER("movl"), S8_INITIALIZER("movq"), S8_INITIALIZER("movzx"), S8_INITIALIZER("movsx"), S8_INITIALIZER("movzb"), S8_INITIALIZER("movzw"), S8_INITIALIZER("movzl"),
    S8_INITIALIZER("movsxb"), S8_INITIALIZER("movsxw"), S8_INITIALIZER("movsxl"), S8_INITIALIZER("add"), S8_INITIALIZER("addb"), S8_INITIALIZER("addw"), S8_INITIALIZER("addl"), S8_INITIALIZER("addq"), S8_INITIALIZER("sub"), S8_INITIALIZER("subb"),
    S8_INITIALIZER("subw"), S8_INITIALIZER("subl"), S8_INITIALIZER("subq"), S8_INITIALIZER("xor"), S8_INITIALIZER("xorb"), S8_INITIALIZER("xorw"), S8_INITIALIZER("xorl"), S8_INITIALIZER("xorq"), S8_INITIALIZER("or"), S8_INITIALIZER("orb"), S8_INITIALIZER("orw"),
    S8_INITIALIZER("orl"), S8_INITIALIZER("orq"), S8_INITIALIZER("and"), S8_INITIALIZER("andb"), S8_INITIALIZER("andw"), S8_INITIALIZER("andl"), S8_INITIALIZER("andq"), S8_INITIALIZER("cmp"), S8_INITIALIZER("cmpb"), S8_INITIALIZER("cmpw"), S8_INITIALIZER("cmpl"),
    S8_INITIALIZER("cmpq"), S8_INITIALIZER("test"), S8_INITIALIZER("testb"), S8_INITIALIZER("testw"), S8_INITIALIZER("testl"), S8_INITIALIZER("testq"), S8_INITIALIZER("xchg"), S8_INITIALIZER("xchgb"), S8_INITIALIZER("xchgw"), S8_INITIALIZER("xchgl"),
    S8_INITIALIZER("xchgq"), S8_INITIALIZER("inc"), S8_INITIALIZER("incb"), S8_INITIALIZER("incw"), S8_INITIALIZER("incl"), S8_INITIALIZER("incq"), S8_INITIALIZER("dec"), S8_INITIALIZER("decb"), S8_INITIALIZER("decw"), S8_INITIALIZER("decl"), S8_INITIALIZER("decq"),
    S8_INITIALIZER("neg"), S8_INITIALIZER("negb"), S8_INITIALIZER("negw"), S8_INITIALIZER("negl"), S8_INITIALIZER("negq"), S8_INITIALIZER("not"), S8_INITIALIZER("notb"), S8_INITIALIZER("notw"), S8_INITIALIZER("notl"), S8_INITIALIZER("notq"), S8_INITIALIZER("bswap"),
    S8_INITIALIZER("bswapl"), S8_INITIALIZER("bswapq"),
    // SYSCALL takes no operands at all, so it needs none of the memory or
    // immediate machinery this list exists to keep out; its register effects
    // are exactly what a C-level constraint and clobber list already state.
    // It is what a libc's system-call layer is written against.
    S8_INITIALIZER("syscall"),
    // The read-modify-write instructions a libc's atomics are written in, the
    // LOCK prefix that makes them atomic, the bit scans its ctz/clz reduce to,
    // and the HLT its abort path ends on. Each writes only its named operands.
    S8_INITIALIZER("lock"), S8_INITIALIZER("cmpxchg"), S8_INITIALIZER("cmpxchgb"), S8_INITIALIZER("cmpxchgw"), S8_INITIALIZER("cmpxchgl"),
    S8_INITIALIZER("cmpxchgq"), S8_INITIALIZER("xadd"), S8_INITIALIZER("xaddb"), S8_INITIALIZER("xaddw"), S8_INITIALIZER("xaddl"),
    S8_INITIALIZER("xaddq"), S8_INITIALIZER("bsf"), S8_INITIALIZER("bsfl"), S8_INITIALIZER("bsfq"), S8_INITIALIZER("bsr"),
    S8_INITIALIZER("bsrl"), S8_INITIALIZER("bsrq"), S8_INITIALIZER("hlt"),
    // LEA computes an address into its destination and reads no memory, so it
    // needs none of the machinery the memory forms above are kept out for. It
    // is how a template names a symbol -- `lea sym(%rip),%0` is musl's
    // GETFUNCSYM, which is the position-independent way to take the address of
    // a hidden function before the program is relocated.
    S8_INITIALIZER("lea"), S8_INITIALIZER("leaw"), S8_INITIALIZER("leal"), S8_INITIALIZER("leaq"),
    // JMP writes no operand at all: it leaves the block, and with it the
    // function. Everything the emitter would have put after the template is
    // unreachable, which is the whole point at a libc's two entry hand-offs --
    // a loader jumping to the program it relocated, and a thread jumping onto
    // a stack that is not the one it is unmapping.
    S8_INITIALIZER("jmp"),
    // asm-goto conditional transfers retain a real MIR fallthrough edge and
    // bind each symbolic branch relocation to its target continuation. These
    // are the GNU/Intel aliases accepted by the shared x86 metadata assembler.
    S8_INITIALIZER("ja"), S8_INITIALIZER("jae"), S8_INITIALIZER("jb"), S8_INITIALIZER("jbe"),
    S8_INITIALIZER("jc"), S8_INITIALIZER("je"), S8_INITIALIZER("jg"), S8_INITIALIZER("jge"),
    S8_INITIALIZER("jl"), S8_INITIALIZER("jle"), S8_INITIALIZER("jna"), S8_INITIALIZER("jnae"),
    S8_INITIALIZER("jnb"), S8_INITIALIZER("jnbe"), S8_INITIALIZER("jnc"), S8_INITIALIZER("jne"),
    S8_INITIALIZER("jng"), S8_INITIALIZER("jnge"), S8_INITIALIZER("jnl"), S8_INITIALIZER("jnle"),
    S8_INITIALIZER("jno"), S8_INITIALIZER("jnp"), S8_INITIALIZER("jns"), S8_INITIALIZER("jnz"),
    S8_INITIALIZER("jo"), S8_INITIALIZER("jp"), S8_INITIALIZER("jpe"), S8_INITIALIZER("jpo"),
    S8_INITIALIZER("js"), S8_INITIALIZER("jz"),
    // The LOOP and CX-zero families have only an eight-bit architectural
    // displacement. For private asm-goto labels the shared assembler expands
    // them to the original predicate plus a near landing jump, so they reach
    // the same explicit MIR control-continuation model as ordinary branches.
    S8_INITIALIZER("loop"), S8_INITIALIZER("loope"), S8_INITIALIZER("loopz"),
    S8_INITIALIZER("loopne"), S8_INITIALIZER("loopnz"),
    S8_INITIALIZER("jecxz"), S8_INITIALIZER("jrcxz"),
    // The scalar SSE instructions musl's own x86-64 math is written in, which
    // are the only reason the 'x' operand class exists here. Each writes only
    // its named vector operands, so they need none of the memory or immediate
    // machinery this list keeps out; the shift-by-immediate pair carries a '$'
    // literal, which the operand scan below already admits.
    S8_INITIALIZER("sqrtsd"), S8_INITIALIZER("sqrtss"), S8_INITIALIZER("cvtsd2si"), S8_INITIALIZER("cvtss2si"),
    S8_INITIALIZER("pcmpeqd"), S8_INITIALIZER("psrlq"), S8_INITIALIZER("psrld"), S8_INITIALIZER("andps"),
    // The x87 instructions musl's own `long double` math is written in. Each
    // reads and writes the stack positions the 't'/'u' operands were pushed
    // into and nothing else, except FISTP, which also pops -- which is what the
    // `st` clobber beside it declares. FNSTSW writes AX, and the template names
    // that register literally because the same asm pins it with an `a` output.
    S8_INITIALIZER("fsqrt"), S8_INITIALIZER("frndint"), S8_INITIALIZER("fabs"), S8_INITIALIZER("fprem"),
    S8_INITIALIZER("fprem1"), S8_INITIALIZER("fnstsw"), S8_INITIALIZER("fistpll"), S8_INITIALIZER("fistpq"),
    // The x87 control-word pair, which touches only the control word and its
    // one memory operand -- no stack position, no general register -- so it
    // needs nothing beyond the `m` machinery FNSTSW already exercises.  It is
    // how a program changes x87 precision or rounding: CPython's configure
    // probes exactly `fnstcw %0` / `fldcw %0` for HAVE_GCC_ASM_FOR_X87, and
    // the FSTCW spelling is the wait form the assembler already folds onto
    // FNSTCW.
    S8_INITIALIZER("fnstcw"), S8_INITIALIZER("fstcw"), S8_INITIALIZER("fldcw"),
};

// The registers a template may name literally. The rule the ban exists for is
// aliasing: a literal register that the emitter can also hand to an operand
// could be overwritten under the template's feet. These four can never be
// handed out -- RSP is the stack pointer and FS/GS are segment selectors --
// and each is allowed only in a position where it cannot do anything else:
// RSP as a memory base, which is the fence idiom `lock orl $0,(%rsp)`, and
// FS/GS before a colon, which is the segment override a thread-pointer read
// is spelled with.
//
// RSP has one further position, and what bounds it is the block leaving the
// function rather than where the name appears: a template whose last
// statement is an unconditional jump may also write the stack pointer,
// because the frame the emitter built is never read again. That is musl's
// CRTJMP -- `mov %1,%%rsp ; jmp *%0` -- and
// codegen_inline_assembly_transfers_control is what decides it.
BUSTER_GLOBAL_LOCAL String8 const codegen_x64_asm_literal_base_registers[] = {
    S8_INITIALIZER("rsp"),
    S8_INITIALIZER("esp"),
};
BUSTER_GLOBAL_LOCAL String8 const codegen_x64_asm_literal_segment_registers[] = {
    S8_INITIALIZER("fs"),
    S8_INITIALIZER("gs"),
};
BUSTER_GLOBAL_LOCAL String8 const codegen_x64_asm_registers[] = {
    S8_INITIALIZER("rax"), S8_INITIALIZER("eax"), S8_INITIALIZER("ax"), S8_INITIALIZER("al"), S8_INITIALIZER("rcx"), S8_INITIALIZER("ecx"), S8_INITIALIZER("cx"), S8_INITIALIZER("cl"), S8_INITIALIZER("rdx"), S8_INITIALIZER("edx"), S8_INITIALIZER("dx"), S8_INITIALIZER("dl"),
    S8_INITIALIZER("rbx"), S8_INITIALIZER("ebx"), S8_INITIALIZER("bx"), S8_INITIALIZER("bl"), S8_INITIALIZER("rsp"), S8_INITIALIZER("esp"), S8_INITIALIZER("sp"), S8_INITIALIZER("spl"), S8_INITIALIZER("rbp"), S8_INITIALIZER("ebp"), S8_INITIALIZER("bp"), S8_INITIALIZER("bpl"),
    S8_INITIALIZER("rsi"), S8_INITIALIZER("esi"), S8_INITIALIZER("si"), S8_INITIALIZER("sil"), S8_INITIALIZER("rdi"), S8_INITIALIZER("edi"), S8_INITIALIZER("di"), S8_INITIALIZER("dil"), S8_INITIALIZER("r8"), S8_INITIALIZER("r8d"), S8_INITIALIZER("r8w"), S8_INITIALIZER("r8b"),
    S8_INITIALIZER("r9"), S8_INITIALIZER("r9d"), S8_INITIALIZER("r9w"), S8_INITIALIZER("r9b"), S8_INITIALIZER("r10"), S8_INITIALIZER("r10d"), S8_INITIALIZER("r10w"), S8_INITIALIZER("r10b"), S8_INITIALIZER("r11"), S8_INITIALIZER("r11d"), S8_INITIALIZER("r11w"),
    S8_INITIALIZER("r11b"), S8_INITIALIZER("r12"), S8_INITIALIZER("r12d"), S8_INITIALIZER("r12w"), S8_INITIALIZER("r12b"), S8_INITIALIZER("r13"), S8_INITIALIZER("r13d"), S8_INITIALIZER("r13w"), S8_INITIALIZER("r13b"), S8_INITIALIZER("r14"), S8_INITIALIZER("r14d"),
    S8_INITIALIZER("r14w"), S8_INITIALIZER("r14b"), S8_INITIALIZER("r15"), S8_INITIALIZER("r15d"), S8_INITIALIZER("r15w"), S8_INITIALIZER("r15b"),
    // The SSE file is here for the same reason the general one is: an operand
    // in the 'x' class is allocated a vector register, and a template that
    // also writes one by name could overwrite it. Refusing the literal spelling
    // is what keeps the allocation the only way into the file.
    S8_INITIALIZER("xmm0"), S8_INITIALIZER("xmm1"), S8_INITIALIZER("xmm2"), S8_INITIALIZER("xmm3"), S8_INITIALIZER("xmm4"), S8_INITIALIZER("xmm5"),
    S8_INITIALIZER("xmm6"), S8_INITIALIZER("xmm7"), S8_INITIALIZER("xmm8"), S8_INITIALIZER("xmm9"), S8_INITIALIZER("xmm10"), S8_INITIALIZER("xmm11"),
    S8_INITIALIZER("xmm12"), S8_INITIALIZER("xmm13"), S8_INITIALIZER("xmm14"), S8_INITIALIZER("xmm15"),
    // `st` covers every spelling of an x87 stack position, because the scan
    // that reaches this table reads letters and digits and stops at the
    // parenthesis: `%st`, `%st(0)` and `%st(3)` all arrive here as "st". None
    // of them may be written by hand. The emitter pushes the operands into
    // their positions before the template and pops what is left after it, so a
    // template that moved the stack itself would leave that accounting wrong.
    S8_INITIALIZER("st"),
};

// The vector registers an 'x' operand may be allocated. The canonical emitter
// keeps every value in its frame slot between instructions, so none of these is
// live across the template; the pool is capped at eight because the metadata
// bridge's vector operand is encoded from a register index in that range.
BUSTER_GLOBAL_LOCAL String8 const codegen_x64_asm_vector_names[] = {
    S8_INITIALIZER("xmm0"), S8_INITIALIZER("xmm1"), S8_INITIALIZER("xmm2"), S8_INITIALIZER("xmm3"),
    S8_INITIALIZER("xmm4"), S8_INITIALIZER("xmm5"), S8_INITIALIZER("xmm6"), S8_INITIALIZER("xmm7"),
};

BUSTER_GLOBAL_LOCAL void codegen_emit_u8(CodegenBuffer* buffer, u8 value);
BUSTER_GLOBAL_LOCAL void codegen_emit_u32(CodegenBuffer* buffer, u32 value);
BUSTER_GLOBAL_LOCAL BUSTER_COLD BUSTER_PRESERVE_MOST void codegen_buffer_report_exhausted(CodegenBuffer* buffer);
BUSTER_GLOBAL_LOCAL u32 codegen_inline_assembly_type_class(IrType* type);
bool codegen_inline_assembly_clobber_register(String8 clobber, X64Register* register_out);
bool codegen_inline_assembly_clobber_vector_register(String8 clobber, u32* register_out);
bool codegen_inline_assembly_constraint_register(u64 constraint, X64Register* register_out);

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
    if ((u32)register_index < BUSTER_ARRAY_LENGTH(codegen_x64_asm_names64))
    {
        switch (width)
        {
        case 1: return codegen_x64_asm_names8[register_index];
        case 2: return codegen_x64_asm_names16[register_index];
        case 4: return codegen_x64_asm_names32[register_index];
        case 8: return codegen_x64_asm_names64[register_index];
        }
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
    if (extra.operand_names)
    {
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
    bool result = false;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(codegen_x64_asm_mnemonics) && !result; index += 1)
    {
        result = string_equal(mnemonic, codegen_x64_asm_mnemonics[index]);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_register_name(String8 token)
{
    bool result = false;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(codegen_x64_asm_registers) && !result; index += 1)
    {
        result = string_equal(token, codegen_x64_asm_registers[index]);
    }

    return result;
}

// The name an operand outside the general registers is spelled with, or an
// empty string for one inside them. Keeping every file behind one lookup is
// what lets the template walk stay a single pass over the references. An x87
// operand names its position rather than a register, because that is what it
// is: the emitter pushed it there.
BUSTER_GLOBAL_LOCAL String8 codegen_inline_assembly_vector_register_name(u64 constraint, u32 const* vector_registers, u32 operand_index)
{
    String8 result = {0};
    u64 constraint_class = constraint & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK;
    if (vector_registers && IR_INLINE_ASSEMBLY_CONSTRAINT_IS_VECTOR(constraint_class) &&
        vector_registers[operand_index] < BUSTER_ARRAY_LENGTH(codegen_x64_asm_vector_names))
    {
        result = codegen_x64_asm_vector_names[vector_registers[operand_index]];
    }
    else if (constraint_class == IR_INLINE_ASSEMBLY_CONSTRAINT_T)
    {
        result = S8("st");
    }
    else if (constraint_class == IR_INLINE_ASSEMBLY_CONSTRAINT_U)
    {
        result = S8("st(1)");
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_name_in_set(String8 name, String8 const* set, u64 count)
{
    for (u64 index = 0; index < count; index += 1)
    {
        if (string_equal(name, set[index]))
        {
            return true;
        }
    }
    return false;
}

// True when the template's last statement is an unconditional jump, so control
// leaves the block and nothing the emitter puts after it is ever reached.
//
// It is what licenses a template to write the stack pointer. RSP is a register
// no operand is ever allocated, so writing it cannot land under another
// operand's feet; what it does destroy is the frame the emitter built, and a
// block that jumps away is a block after which that frame is not read again.
// musl's CRTJMP is the shape: a dynamic loader entering the program it just
// relocated, and a thread jumping onto a borrowed stack before unmapping the
// one it was running on.
//
// Statements are separated the way the rest of this path separates them, by a
// newline or a GNU semicolon, and a directive line is skipped rather than
// counted: `.hidden sym` says nothing about control flow.
BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_transfers_control(String8 source)
{
    bool result = false;
    u64 index = 0;
    while (index < source.length)
    {
        while (index < source.length && (source.pointer[index] == ' ' || source.pointer[index] == '\t' || source.pointer[index] == '\r' ||
                                         source.pointer[index] == '\n' || source.pointer[index] == ';'))
        {
            index += 1;
        }
        u64 mnemonic_start = index;
        while (index < source.length && source.pointer[index] != ' ' && source.pointer[index] != '\t' && source.pointer[index] != '\r' &&
               source.pointer[index] != '\n' && source.pointer[index] != ';' && source.pointer[index] != ',')
        {
            index += 1;
        }
        String8 mnemonic = {
            .pointer = source.pointer + mnemonic_start,
            .length = index - mnemonic_start,
        };
        if (mnemonic.length && mnemonic.pointer[0] != '.')
        {
            result = string_equal(mnemonic, S8("jmp"));
        }
        while (index < source.length && source.pointer[index] != '\n' && source.pointer[index] != ';')
        {
            index += 1;
        }
    }

    return result;
}

// What the template itself is allowed to spell, checked before any operand is
// substituted. The distinction matters: substituting a memory operand produces
// a parenthesized register, and the source is not allowed to write one by hand,
// so the rule has to be applied to the source rather than to the result.
//
// A literal register is refused because it is not tied to a compiler operand: a
// generic input allocated in RAX would be silently overwritten by a template
// that also writes `%%rax`. The two exceptions are registers the emitter can
// never hand out, each allowed only in the position where it cannot alias
// anything -- see codegen_x64_asm_literal_base_registers.
//
// Bracketed memory stays out entirely. In Intel syntax a register is spelled
// without a sigil, so `[rax]` carries no marker this scan could recognize, and
// refusing the brackets is what keeps that syntax as safe as the AT&T one.
//
// The AT&T dereference star is allowed in exactly one place, directly in front
// of an operand reference: `jmp *%0` is how a template leaves the function
// through an address the C side computed. In front of anything else -- a
// literal register, a bare name -- it would be an addressing mode none of the
// passes here has seen.
//
// `transfers_control` is the caller's answer for this same template, and it is
// what the stack-pointer exception hangs on; see
// codegen_x64_asm_literal_base_registers.
//
// `reserved_registers` and `reserved_vector_registers` are the third
// exception, and they are what make the reason above stop applying: a register
// this asm has already committed to -- pinned by a fixed-class operand, or
// named in its clobber list -- is one the emitter cannot also hand to another
// operand, so a template naming it cannot overwrite anything. musl's `fmodl`
// is the shape: `fnstsw %%ax` beside an `"=a"` output, where the literal
// register *is* the operand's register.
//
// `reason_out`, when the caller asks for one, receives the rule's own words for
// the refusal instead of leaving the driver to report an opcode number (#831).
BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_template_literal_valid(Arena* arena, String8 source, bool transfers_control,
                                                                        bool const* reserved_registers,
                                                                        bool const* reserved_vector_registers, String8* reason_out)
{
    for (u64 index = 0; index < source.length; index += 1)
    {
        u8 character = (u8)source.pointer[index];
        if (character == '[' || character == ']')
        {
            if (reason_out)
            {
                *reason_out = S8("asm template writes a bracketed memory reference, which names a register no pass here has seen");
            }
            return false;
        }
        if (character == '*')
        {
            u64 marked = index + 1;
            bool operand_reference = marked + 1 < source.length && source.pointer[marked] == '%' &&
                                     (source.pointer[marked + 1] == '[' || (source.pointer[marked + 1] >= '0' && source.pointer[marked + 1] <= '9'));
            if (!operand_reference)
            {
                if (reason_out)
                {
                    *reason_out = S8("asm template dereferences with '*' somewhere other than directly in front of an operand reference");
                }
                return false;
            }
            continue;
        }
        if (character != '%')
        {
            continue;
        }
        u64 token_start = index + 1;
        bool escaped = token_start < source.length && source.pointer[token_start] == '%';
        if (escaped)
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
            index = token_start - 1;
            continue;
        }
        u64 token_end = token_start;
        while (token_end < source.length && ((source.pointer[token_end] >= 'a' && source.pointer[token_end] <= 'z') ||
                                              (source.pointer[token_end] >= 'A' && source.pointer[token_end] <= 'Z') ||
                                              (source.pointer[token_end] >= '0' && source.pointer[token_end] <= '9')))
        {
            token_end += 1;
        }
        String8 name = {
            .pointer = source.pointer + token_start,
            .length = token_end - token_start,
        };
        if (name.length && codegen_inline_assembly_register_name(name))
        {
            u64 previous = index;
            while (previous && (source.pointer[previous - 1] == ' ' || source.pointer[previous - 1] == '\t'))
            {
                previous -= 1;
            }
            bool memory_base = previous && source.pointer[previous - 1] == '(' &&
                               codegen_inline_assembly_name_in_set(name, codegen_x64_asm_literal_base_registers,
                                                                   BUSTER_ARRAY_LENGTH(codegen_x64_asm_literal_base_registers));
            bool segment_override = token_end < source.length && source.pointer[token_end] == ':' &&
                                    codegen_inline_assembly_name_in_set(name, codegen_x64_asm_literal_segment_registers,
                                                                        BUSTER_ARRAY_LENGTH(codegen_x64_asm_literal_segment_registers));
            // The stack pointer written outright, which only a block that
            // jumps away may do: after it, the frame it moved is never the one
            // anything reads.
            bool stack_hand_off = transfers_control && codegen_inline_assembly_name_in_set(name, codegen_x64_asm_literal_base_registers,
                                                                                          BUSTER_ARRAY_LENGTH(codegen_x64_asm_literal_base_registers));
            X64Register named_register = X64_REGISTER_RAX;
            u32 named_vector_register = 0;
            bool reserved = (reserved_registers && codegen_inline_assembly_clobber_register(name, &named_register) &&
                             reserved_registers[named_register]) ||
                            (reserved_vector_registers && codegen_inline_assembly_clobber_vector_register(name, &named_vector_register) &&
                             reserved_vector_registers[named_vector_register]);
            if (!memory_base && !segment_override && !stack_hand_off && !reserved)
            {
                if (reason_out)
                {
                    *reason_out = string_format(arena, S8("asm template names the literal register '%{S8}', which the emitter could also hand to an operand"), name);
                }
                return false;
            }
        }
        index = token_end - 1;
    }

    return true;
}

// What the substituted source is allowed to be: one mnemonic from the list per
// statement, and operands the emitter either produced itself or the template
// validator already cleared. The punctuation rules live on the template rather
// than here, because by this point a memory operand has legitimately become a
// parenthesized register.
//
// A statement starting with a dot is a directive rather than an instruction:
// it names no register and emits nothing here, so it passes through to the
// emitter, where the same table module-level assembly uses decides which
// directives exist. `.hidden sym` in front of a PC-relative reference is what
// a libc's GETFUNCSYM writes.
//
// `reason_out`, when the caller asks for one, receives the rule's own words for
// the refusal instead of leaving the driver to report an opcode number (#831).
BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_register_only_source(Arena* arena, String8 source, String8* reason_out)
{
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
        String8 mnemonic = {
            .pointer = source.pointer + mnemonic_start,
            .length = index - mnemonic_start,
        };
        bool directive = mnemonic.length && mnemonic.pointer[0] == '.';
        if (!directive && !codegen_inline_assembly_mnemonic_allowed(mnemonic))
        {
            if (reason_out)
            {
                *reason_out = string_format(arena, S8("asm template names the instruction '{S8}', which is outside the mnemonics this emitter accepts"), mnemonic);
            }
            return false;
        }
        u64 line_end = index;
        while (line_end < source.length && source.pointer[line_end] != '\n' && source.pointer[line_end] != ';' && source.pointer[line_end] != '\r')
        {
            line_end += 1;
        }
        while (!directive && index < line_end)
        {
            while (index < line_end && (source.pointer[index] == ' ' || source.pointer[index] == '\t' || source.pointer[index] == ','))
            {
                index += 1;
            }
            if (index >= line_end)
            {
                break;
            }
            // An operand is a substituted register, a memory reference the
            // emitter or the cleared template produced, or a '$' immediate. A
            // bare leading digit or sign is an Intel-syntax immediate that no
            // pass above has seen, and stays out.
            if ((source.pointer[index] >= '0' && source.pointer[index] <= '9') || source.pointer[index] == '+' || source.pointer[index] == '-')
            {
                if (reason_out)
                {
                    *reason_out = string_format(arena, S8("asm template writes a bare immediate operand to '{S8}', which no pass here has seen"), mnemonic);
                }
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

// A GNU template separates statements with a semicolon, while this assembler
// reads one as the start of a comment. Rewriting the separators is what makes
// the two dialects the same text, and it is done here rather than in the
// assembler because the semicolon means what the assembler says it means
// everywhere else, including in a global assembly block.
//
// The one pair that is joined instead of split is `lock ; insn`: a bare LOCK
// prefixes the statement that follows it, and this assembler wants the prefix
// and its instruction in a single statement. Neither rewrite can lengthen the
// text, so both happen in place.
BUSTER_GLOBAL_LOCAL void codegen_inline_assembly_normalize_statements(String8* source)
{
    String8 text = *source;
    u64 write = 0;
    u64 read = 0;
    while (read < text.length)
    {
        u64 previous = write;
        while (previous && (text.pointer[previous - 1] == ' ' || text.pointer[previous - 1] == '\t'))
        {
            previous -= 1;
        }
        bool at_statement_start = !previous || text.pointer[previous - 1] == '\n';
        u64 word_end = read;
        while (word_end < text.length && text.pointer[word_end] >= 'a' && text.pointer[word_end] <= 'z')
        {
            word_end += 1;
        }
        String8 word = {.pointer = text.pointer + read, .length = word_end - read};
        if (at_statement_start && string_equal(word, S8("lock")))
        {
            u64 scan = word_end;
            while (scan < text.length && (text.pointer[scan] == ' ' || text.pointer[scan] == '\t'))
            {
                scan += 1;
            }
            if (scan < text.length && text.pointer[scan] == ';')
            {
                write = previous;
                memmove(text.pointer + write, word.pointer, word.length);
                write += word.length;
                text.pointer[write++] = ' ';
                read = scan + 1;
                continue;
            }
        }
        if (word.length)
        {
            memmove(text.pointer + write, word.pointer, word.length);
            write += word.length;
            read = word_end;
            continue;
        }
        text.pointer[write] = text.pointer[read] == ';' ? '\n' : text.pointer[read];
        write += 1;
        read += 1;
    }
    source->length = write;
}

bool codegen_inline_assembly_resolve_template(Arena* arena, IrProgram* program, IrFunction* function, IrInstruction* instruction,
                                              IrInstructionExtra extra, X64Register* registers, u32* vector_registers,
                                              AssemblySyntax syntax, String8* source_out, String8* reason_out)
{
    String8 template_source = extra.literal;
    // Validated assembly has one constraint and value for every operand.
    BUSTER_CHECK(!instruction->operand_count || (instruction->immediates && instruction->operands));
    // The registers this asm has already committed to, which is what licenses a
    // template to name one of them literally. Only the pinned operand classes
    // and the clobbers are here: a generically allocated `r` operand is a
    // register the emitter chose, and a template naming it by hand is exactly
    // the collision the refusal exists for.
    bool reserved_registers[16] = {0};
    bool reserved_vector_registers[16] = {0};
    for (u32 operand_index = 0; operand_index < instruction->operand_count; operand_index += 1)
    {
        X64Register pinned = X64_REGISTER_RAX;
        if (instruction->immediates && codegen_inline_assembly_constraint_register(instruction->immediates[operand_index], &pinned) &&
            (u32)pinned < BUSTER_ARRAY_LENGTH(reserved_registers))
        {
            reserved_registers[pinned] = true;
        }
    }
    for (u32 clobber_index = 0; clobber_index < extra.clobber_count; clobber_index += 1)
    {
        X64Register clobbered = X64_REGISTER_RAX;
        u32 clobbered_vector = 0;
        if (codegen_inline_assembly_clobber_register(extra.clobbers[clobber_index], &clobbered) &&
            (u32)clobbered < BUSTER_ARRAY_LENGTH(reserved_registers))
        {
            reserved_registers[clobbered] = true;
        }
        else if (codegen_inline_assembly_clobber_vector_register(extra.clobbers[clobber_index], &clobbered_vector) &&
                 clobbered_vector < BUSTER_ARRAY_LENGTH(reserved_vector_registers))
        {
            reserved_vector_registers[clobbered_vector] = true;
        }
    }
    if (!codegen_inline_assembly_template_literal_valid(arena, template_source, codegen_inline_assembly_transfers_control(template_source),
                                                       reserved_registers, reserved_vector_registers, reason_out))
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
            if (reason_out)
            {
                *reason_out = index + 1 < template_source.length && template_source.pointer[index + 1] == 'l'
                    ? S8("inline assembly label references (%l) are unsupported in this template form")
                    : S8("inline assembly template contains an unsupported operand reference");
            }
            return false;
        }
        IrValueId value = instruction->operands[operand_index];
        if (value.value >= function->value_count)
        {
            return false;
        }
        IrType* type = ir_type_from_id(&program->types, function->values[value.value].canonical_type);
        u32 type_class = codegen_inline_assembly_type_class(type);
        // A memory operand's register holds an address, so it is always spelled
        // at pointer width and wrapped in the syntax's memory reference; every
        // other operand is spelled at the width of its own type.
        bool memory_operand =
            IR_INLINE_ASSEMBLY_CONSTRAINT_IS_MEMORY(instruction->immediates[operand_index] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK);
        // A vector operand is spelled by the register alone: the SSE file has
        // one name per register rather than a name per access width, and the
        // instruction the template wrote is what says how much of it is read.
        String8 register_name = codegen_inline_assembly_vector_register_name(instruction->immediates[operand_index], vector_registers, operand_index);
        if (!register_name.length)
        {
            register_name = codegen_x64_asm_register_name(registers[operand_index],
                                                          memory_operand                                              ? 8
                                                          : type_class == IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID ? 0
                                                                                                                    : (u32)type->layout.size);
        }
        if (!register_name.length || (syntax != ASSEMBLY_SYNTAX_ATT && syntax != ASSEMBLY_SYNTAX_INTEL))
        {
            return false;
        }
        u64 decoration = syntax == ASSEMBLY_SYNTAX_ATT ? 1 : 0;
        decoration += memory_operand ? 2 : 0;
        if (register_name.length > UINT64_MAX - output_length - decoration)
        {
            return false;
        }
        output_length += register_name.length + decoration;
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
        bool memory_operand =
            IR_INLINE_ASSEMBLY_CONSTRAINT_IS_MEMORY(instruction->immediates[operand_index] & IR_INLINE_ASSEMBLY_CONSTRAINT_CLASS_MASK);
        String8 register_name = codegen_inline_assembly_vector_register_name(instruction->immediates[operand_index], vector_registers, operand_index);
        if (!register_name.length)
        {
            register_name = codegen_x64_asm_register_name(registers[operand_index], memory_operand ? 8 : (u32)type->layout.size);
        }
        if (memory_operand)
        {
            output[output_index++] = syntax == ASSEMBLY_SYNTAX_ATT ? '(' : '[';
        }
        if (syntax == ASSEMBLY_SYNTAX_ATT)
        {
            output[output_index++] = '%';
        }
        if (register_name.length) memcpy(output + output_index, register_name.pointer, register_name.length);
        output_index += register_name.length;
        if (memory_operand)
        {
            output[output_index++] = syntax == ASSEMBLY_SYNTAX_ATT ? ')' : ']';
        }
        index = end;
    }
    *source_out = (String8){.pointer = output, .length = output_index};
    // The shape check runs before the prefix is folded in, so LOCK is still a
    // statement of its own there and is checked against the mnemonic list like
    // every other one.
    if (!codegen_inline_assembly_register_only_source(arena, *source_out, reason_out))
    {
        return false;
    }
    codegen_inline_assembly_normalize_statements(source_out);
    return true;
}

BUSTER_GLOBAL_LOCAL bool codegen_inline_assembly_clobber_is_rbx(String8 clobber)
{
    return string_equal(clobber, S8("rbx")) || string_equal(clobber, S8("ebx")) || string_equal(clobber, S8("bx")) || string_equal(clobber, S8("bl"));
}

bool codegen_inline_assembly_clobber_register(String8 clobber, X64Register* register_out)
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

bool codegen_inline_assembly_clobber_vector_register(String8 clobber, u32* register_out)
{
    bool result = false;
    if (clobber.length >= 4 && clobber.pointer[0] == 'x' && clobber.pointer[1] == 'm' && clobber.pointer[2] == 'm')
    {
        u64 number = 0;
        String8 suffix = {
            .pointer = clobber.pointer + 3,
            .length = clobber.length - 3,
        };
        result = codegen_decimal_number(suffix, &number) && number <= 15 &&
                 (suffix.length == 1 || (suffix.length == 2 && suffix.pointer[0] == '1'));
        if (result)
        {
            *register_out = (u32)number;
        }
    }
    return result;
}

bool codegen_inline_assembly_constraint_register(u64 constraint, X64Register* register_out)
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
    case IR_INLINE_ASSEMBLY_CONSTRAINT_SI:
        *register_out = X64_REGISTER_RSI;
        return true;
    case IR_INLINE_ASSEMBLY_CONSTRAINT_DI:
        *register_out = X64_REGISTER_RDI;
        return true;
    case IR_INLINE_ASSEMBLY_CONSTRAINT_R8:
        *register_out = X64_REGISTER_R8;
        return true;
    case IR_INLINE_ASSEMBLY_CONSTRAINT_R9:
        *register_out = X64_REGISTER_R9;
        return true;
    case IR_INLINE_ASSEMBLY_CONSTRAINT_R10:
        *register_out = X64_REGISTER_R10;
        return true;
    case IR_INLINE_ASSEMBLY_CONSTRAINT_R11:
        *register_out = X64_REGISTER_R11;
        return true;
    case IR_INLINE_ASSEMBLY_CONSTRAINT_R:
    case IR_INLINE_ASSEMBLY_CONSTRAINT_COUNT:
        break;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL u32 codegen_inline_assembly_type_class(IrType* type)
{
    if (type && type->layout.resolved && type->layout.size && type->layout.size <= 8)
    {
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
    }

    return IR_INLINE_ASSEMBLY_OPERAND_CLASS_INVALID;
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

// One x87 stack slot, ST(index).  The metadata tables carry ST0..ST7 as
// SPECIAL registers of architectural width 80, which is also how the
// assembler's own crosswalk spells ASSEMBLY_REGISTER_X87.

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

// The metadata-owned TLS recipe preserves the ABI envelope and supplies both
// field offsets. Model/symbol policy remains here, not in the ISA encoder.

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

// The metadata cache and its template array, from `arena`, or null when the
// arena cannot hold them: a caller may pass a deliberately small arena to
// exercise a capacity-limited module, and emission treats an absent cache as
// a miss on every query, so the bytes it emits do not depend on the answer.
BUSTER_GLOBAL_LOCAL CodegenX64MetadataCache* codegen_canonical_x64_metadata_cache_allocate(Arena* arena, u64 function_count)
{
    CodegenX64MetadataCache* result = 0;
    u32 template_capacity = codegen_canonical_x64_template_capacity(function_count);
    u64 cache_size = sizeof(CodegenX64MetadataCache) + (u64)template_capacity * sizeof(CodegenX64TemplateCacheEntry);
    if (function_count && cache_size <= arena->reserved_size - BUSTER_MIN(arena->position, arena->reserved_size))
    {
        u64 template_dirty_position = arena_dirty_position(arena);
        result = arena_allocate(arena, CodegenX64MetadataCache, 1);
        memset(result, 0, sizeof(*result));
        result->templates = arena_allocate(arena, CodegenX64TemplateCacheEntry, template_capacity);
        // `dirty_position` is the allocation high-water mark, not the
        // logical position: a temporal rewind and a pooled arena can put
        // old bytes ahead of the current cursor.  Only that overlap can
        // carry a prior template; the suffix was never allocated in this
        // arena generation and is therefore already zero from the OS.
        // Clearing through the watermark also handles a boundary that
        // falls inside an entry, leaving every field after it zero.
        u64 template_bytes = (u64)template_capacity * sizeof(*result->templates);
        u64 template_end = arena->position;
        u64 template_start = template_end - template_bytes;
        u64 clear_end = BUSTER_MIN(template_dirty_position, template_end);
        if (clear_end > template_start)
        {
            memset(result->templates, 0, clear_end - template_start);
        }
        result->template_mask = template_capacity - 1u;
    }
    return result;
}

// The cache a module under a register allocator allocates on first need; see
// codegen_generate_canonical_module. One try per attempt: an arena too small
// for it stays too small.
BUSTER_GLOBAL_LOCAL void codegen_buffer_ensure_x64_metadata_cache(CodegenBuffer* buffer, bool* tried, Arena* arena, u64 function_count)
{
    if (!*tried)
    {
        *tried = true;
        buffer->x64_metadata_cache = codegen_canonical_x64_metadata_cache_allocate(arena, function_count);
    }
}

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
    u8 result;
    if (!operand.has_value)
    {
        result = 0;
    }
    else if (operand.value == 1)
    {
        result = 7;
    }
    else if (operand.value >= INT8_MIN && operand.value <= INT8_MAX)
    {
        result = 8;
    }
    else if (operand.value >= INT32_MIN && operand.value <= INT32_MAX)
    {
        result = 9;
    }
    else
    {
        result = 10;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL u8 codegen_canonical_x64_metadata_displacement_class(BusterX86MetadataPhysicalMemory memory)
{
    u8 result;
    if (!memory.has_displacement)
    {
        result = 0;
    }
    else if (memory.displacement == 0)
    {
        result = 1;
    }
    else if (memory.displacement >= INT8_MIN && memory.displacement <= INT8_MAX)
    {
        result = 2;
    }
    else if (memory.displacement >= INT32_MIN && memory.displacement <= INT32_MAX)
    {
        result = 3;
    }
    else
    {
        result = 4;
    }

    return result;
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
    if (cache)
    {
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
    bool result = false;
    for (u32 operand_index = 0; operand_index < physical.operand_count && !result; operand_index += 1)
    {
        BusterX86MetadataPhysicalOperand operand = physical.operands[operand_index];
        result = operand.has_symbol || operand.symbol.length || operand.memory.has_symbol || operand.memory.symbol.length;
    }

    return result;
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

// RAX shifted by a constant. The x86-64 vocabulary this emitter speaks takes
// its shift count in CL, so the count is materialized first; every caller
// shifts the accumulator it just loaded, which is always RAX.

// Emit one symbol-bearing instruction through the checked metadata bridge.
// Canonical codegen keeps module relocations in its own format, while the
// metadata encoder owns the instruction shape and the exact displacement
// field.  Give the physical query a private non-empty symbol solely to force
// metadata to materialize its relocation record; callers translate the
// returned field offset into their CodegenModuleRelocation entry.

// One scalar SSE move between a vector register and memory, for an operand in
// the 'x' class. codegen_canonical_x64_float_memory does the same job for the
// ABI paths but only against the frame pointer; an asm operand reached through
// a pointer loads from the register that pointer was loaded into, so the base
// is a parameter here.

// The address of a frame slot, for a memory operand whose storage is the slot
// itself rather than something the slot points at.

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
    bool result;
    if (!descriptor || !descriptor->epilog_offsets || descriptor->epilog_count >= capacity)
    {
        result = false;
    }
    else
    {
        descriptor->epilog_offsets[descriptor->epilog_count++] = code_offset;
        result = true;
    }

    return result;
}

// Counters are additive except the maximum frame size. Keep the complete
// merge here so multi-input drivers retain the same census as single inputs.
void codegen_statistics_add(CodegenStatistics* total, CodegenStatistics* unit)
{
    total->instruction_count += unit->instruction_count;
    total->value_count += unit->value_count;
    total->stack_value_bytes += unit->stack_value_bytes;
    total->stack_frame_bytes += unit->stack_frame_bytes;
    total->code_bytes += unit->code_bytes;
    total->native_vector_operation_count += unit->native_vector_operation_count;
    total->split_vector_operation_count += unit->split_vector_operation_count;
    total->vzeroupper_count += unit->vzeroupper_count;
    total->forwarded_wide_vector_load_count += unit->forwarded_wide_vector_load_count;
    total->simd_operation_count += unit->simd_operation_count;
    total->function_count += unit->function_count;
    total->maximum_stack_frame_bytes = BUSTER_MAX(total->maximum_stack_frame_bytes, unit->maximum_stack_frame_bytes);
    total->fallback_function_count += unit->fallback_function_count;
    total->fallback_verify_count += unit->fallback_verify_count;
    total->fallback_placement_count += unit->fallback_placement_count;
    total->fallback_encode_count += unit->fallback_encode_count;
    total->allocator_reload_count += unit->allocator_reload_count;
    total->allocator_spill_count += unit->allocator_spill_count;
    total->allocator_copy_count += unit->allocator_copy_count;
    total->allocator_boundary_spill_count += unit->allocator_boundary_spill_count;
    total->allocator_boundary_reload_count += unit->allocator_boundary_reload_count;
    total->allocator_boundary_copy_count += unit->allocator_boundary_copy_count;
    total->allocator_rematerialize_count += unit->allocator_rematerialize_count;
    total->allocator_pinned_register_count += unit->allocator_pinned_register_count;
    total->allocator_split_register_count += unit->allocator_split_register_count;
    total->allocator_scheduled_function_count += unit->allocator_scheduled_function_count;
    total->allocator_schedule_kept_count += unit->allocator_schedule_kept_count;
    total->exact_attempts += unit->exact_attempts;
    total->exact_successes += unit->exact_successes;
    total->exact_failures += unit->exact_failures;
    total->mutable_virtual_register_count += unit->mutable_virtual_register_count;
    total->verified_ir_module_count += unit->verified_ir_module_count;
    total->verified_mir_function_count += unit->verified_mir_function_count;
    total->verified_scheduled_function_count += unit->verified_scheduled_function_count;
    for (u32 index = 0; index < IR_OPCODE_COUNT + 1; index += 1)
    {
        total->fallback_opcode_counts[index] += unit->fallback_opcode_counts[index];
    }
    for (u32 index = 0; index < CODEGEN_FALLBACK_REASON_COUNT; index += 1)
    {
        total->fallback_reason_counts[index] += unit->fallback_reason_counts[index];
    }
}

String8 codegen_fallback_reason_string(CodegenFallbackReason reason)
{
    String8 result = S8("invalid");
    switch (reason)
    {
        break; case CODEGEN_FALLBACK_TARGET_EXCLUDED: result = S8("target-excluded");
        break; case CODEGEN_FALLBACK_SIGNATURE: result = S8("signature");
        break; case CODEGEN_FALLBACK_OPCODE: result = S8("opcode");
        break; case CODEGEN_FALLBACK_SELECTION_OTHER: result = S8("selection-other");
        break; case CODEGEN_FALLBACK_VERIFICATION: result = S8("verification");
        break; case CODEGEN_FALLBACK_PLACEMENT: result = S8("placement");
        break; case CODEGEN_FALLBACK_ENCODING: result = S8("encoding");
        break; case CODEGEN_FALLBACK_OUTPUT_CAPACITY: result = S8("output-capacity");
        break; case CODEGEN_FALLBACK_UNWIND: result = S8("unwind");
        break; case CODEGEN_FALLBACK_REASON_COUNT: break;
    }
    return result;
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

// The narrowest thread-local model that can be right for one reference, which
// is what clang picks and for the same two reasons.  Position-independent
// code may be linked into a shared object, and a shared object may be
// dlopened after the initial thread-local block is laid out, so nothing it
// defines has a link-time offset from the thread pointer: general-dynamic.
// Otherwise the object is an executable, its own definitions are in the
// initial block at a link-time constant offset -- local-exec, which is the
// common case and the one every non-shared program takes -- while a
// declaration it does not define may live in a library, whose offset only
// the loader knows: initial-exec, through a GOT slot.
CodegenThreadLocalModel codegen_thread_local_model(bool position_independent, bool symbol_is_definition)
{
    return position_independent  ? CODEGEN_THREAD_LOCAL_GENERAL_DYNAMIC
           : symbol_is_definition ? CODEGEN_THREAD_LOCAL_LOCAL_EXEC
                                  : CODEGEN_THREAD_LOCAL_INITIAL_EXEC;
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
        case OPERATING_SYSTEM_WASI:
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
        case OPERATING_SYSTEM_WASI:
        case OPERATING_SYSTEM_COUNT:
            return CODEGEN_ABI_COUNT;
        }
    }
    break;
        break;
    case CPU_ARCH_WASM32:
    case CPU_ARCH_WASM64:
    case CPU_ARCH_BPFEL:
        // WebAssembly and eBPF are emitted directly from canonical IR and do not
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
    // The opcode row-facts projection is target-independent: it covers both
    // machine backends' opcode ranges and every allocator reads it.
    machine_opcode_rows_prewarm();
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

// The native emitter no longer writes raw 64-bit scalars. Retain the
// byte-writer boundary probe only in test-enabled builds.
#if BUSTER_INCLUDE_TESTS
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

bool codegen_x64_emit_windows_stack_allocate(CodegenBuffer* buffer, u32 size, CodegenFunctionDescriptor* descriptor, u32 action_capacity,
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
    if (codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), move_r10_operands, BUSTER_ARRAY_LENGTH(move_r10_operands)) &&
        codegen_canonical_x64_metadata_emit(buffer, S8("SUB"), sub_r10_operands, BUSTER_ARRAY_LENGTH(sub_r10_operands)) &&
        codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), move_r11_operands, BUSTER_ARRAY_LENGTH(move_r11_operands)))
    {
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
        if (codegen_canonical_x64_metadata_emit(buffer, S8("JMP"), &loop_branch_operand, 1))
        {
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
        }
    }

    return true;
}

// `source_limit` is the first source id the record cannot name: at most
// UINT16_MAX + 1, and the program's source count when the caller knows it, so
// a stored source is always an index into the source table and the object
// writer can hand the array to the DWARF and CodeView builders as it is.
BUSTER_GLOBAL_LOCAL BUSTER_INLINE void codegen_record_line_hot(CodegenLineEntry* entries, u32* count, u32 capacity, u32 code_offset, u32 source,
                                                               u32 source_limit, u32 line, u32 column)
{
    if (entries && line && *count < capacity)
    {
        // The 12-byte record stores these as u16; saturate rather than truncate
        // so an overflowing source cannot alias an unrelated file.
        u16 stored_source = source < source_limit ? (u16)source : 0;
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
}

void codegen_record_line(CodegenLineEntry* entries, u32* count, u32 capacity, u32 code_offset, u32 source, u32 line, u32 column)
{
    codegen_record_line_hot(entries, count, capacity, code_offset, source, (u32)UINT16_MAX + 1, line, column);
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

// Where recorded debug-location seeds land. Seeds are appended one at a time
// and the array grows by doubling from its arena, so the storage a function
// needs is its emitted seeds, never a worst case derived from its shape. A
// null arena keeps a caller-owned fixed array instead: an overflow then
// reports CODEGEN_ERROR_CAPACITY rather than reallocating storage the caller
// owns.
#define CODEGEN_DEBUG_LOCATION_SEED_MINIMUM 256u

typedef struct CodegenDebugLocationSink CodegenDebugLocationSink;
struct CodegenDebugLocationSink
{
    Arena* arena;
    u32 capacity;
    u8 reserved[4];
};

BUSTER_GLOBAL_LOCAL bool codegen_debug_locations_reserve(CodegenModule* result, CodegenDebugLocationSink* sink, u64 additional)
{
    u64 required = (u64)result->debug_location_count + additional;
    bool reserved = required <= UINT32_MAX;
    if (reserved && required > sink->capacity)
    {
        reserved = sink->arena != 0;
        if (reserved)
        {
            u64 doubled = BUSTER_MAX((u64)sink->capacity * 2u, (u64)CODEGEN_DEBUG_LOCATION_SEED_MINIMUM);
            u32 grown_capacity = (u32)BUSTER_MIN(BUSTER_MAX(required, doubled), (u64)UINT32_MAX);
            DebugLocationSeed* grown = arena_allocate(sink->arena, DebugLocationSeed, grown_capacity);
            if (result->debug_location_count)
            {
                memcpy(grown, result->debug_locations, (u64)result->debug_location_count * sizeof(*grown));
            }
            result->debug_locations = grown;
            sink->capacity = grown_capacity;
        }
    }
    if (!reserved)
    {
        result->error = CODEGEN_ERROR_CAPACITY;
    }
    return reserved;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_location_append(CodegenModule* result, CodegenDebugLocationSink* sink, IrSymbolId symbol,
                                                            IrLocalId local, u32 start, u32 end, DebugLocation location)
{
    bool appended = result && end > start;
    if (appended && result->debug_location_count >= sink->capacity)
    {
        appended = codegen_debug_locations_reserve(result, sink, 1);
    }
    if (appended)
    {
        result->debug_locations[result->debug_location_count++] = (DebugLocationSeed){
            .function_symbol = symbol,
            .local = local,
            .start = start,
            .end = end,
            .location = location,
        };
    }
    return appended;
}

// Block IDs are graph identities, not an execution order. Keep the entry
// first and every other block in ID order; debug ranges use this same layout.

typedef struct A64Relocation A64Relocation;
struct A64Relocation
{
    A64Relocation* next;
    IrBlockId target;
    u32 instruction_offset;
    bool conditional;
    u8 reserved[3];
};

BUSTER_GLOBAL_LOCAL void a64_emit_instruction_word(CodegenBuffer* buffer, u32 instruction)
{
    codegen_emit_u32(buffer, instruction);
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

u32 codegen_a64_windows_save_area_size(u32 saved_register_count)
{
    // FP/LR form the chain at the bottom, followed by allocator saves and
    // the reserved X28 frame base. The final padding keeps SP aligned.
    return (16u + 8u * (saved_register_count + 1u) + 15u) & ~15u;
}

bool codegen_a64_windows_large_stack_adjust(CodegenBuffer* buffer, u32 size, bool subtract,
                                           CodegenFunctionDescriptor* descriptor, u32 action_capacity)
{
    bool handled = size > A64_SP_ADJUST_CHUNK && size % 16 == 0;
    if (handled)
    {
        u32 units = size / 16;
        if (!subtract)
        {
            a64_emit_constant(buffer, 15, units);
            a64_emit_instruction_word(buffer, 0x8b2f73ff);
        }
        else
        {
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
            }
            else
            {
                u32 final_words = (final_offset - final_branch) / 4;
                s32 loop_words = ((s32)loop_offset - (s32)loop_branch) / 4;
                u32 final_instruction = 0x54000009 | ((final_words & 0x7ffff) << 5);
                u32 loop_instruction = 0x14000000 | ((u32)loop_words & 0x03ffffff);
                memcpy(buffer->bytes + final_branch, &final_instruction, sizeof(final_instruction));
                memcpy(buffer->bytes + loop_branch, &loop_instruction, sizeof(loop_instruction));
                if (descriptor)
                {
                    for (u32 instruction_index = 0; instruction_index < BUSTER_ARRAY_LENGTH(instruction_offsets) && buffer->error == CODEGEN_ERROR_NONE; instruction_index += 1)
                    {
                        if (!codegen_unwind_action_append(descriptor, action_capacity, instruction_offsets[instruction_index] - descriptor->code_offset,
                                                          CODEGEN_UNWIND_ACTION_NOP, 0, 0))
                        {
                            buffer->error = CODEGEN_ERROR_CAPACITY;
                        }
                    }
                    if (buffer->error == CODEGEN_ERROR_NONE && !codegen_unwind_action_append(descriptor, action_capacity, (u32)buffer->count - descriptor->code_offset,
                                                      CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, size))
                    {
                        buffer->error = CODEGEN_ERROR_CAPACITY;
                    }
                }
            }
        }
    }
    return handled;
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

BUSTER_GLOBAL_LOCAL IrAbiConvention codegen_canonical_ir_abi_convention(CodegenAbi abi)
{
    return ir_abi_convention_for_target(codegen_target_for_abi(abi));
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_abi_part_is_float(IrAbiClass abi_class)
{
    return abi_class == IR_ABI_CLASS_FLOAT || abi_class == IR_ABI_CLASS_VECTOR;
}

// Whether a Win64 argument rides the positional XMM register the way a scalar
// float does. A single-lane float vector has the same shape as its element on
// this convention (clang's de-facto ABI; see the Win64 vector branch of
// ir_classify_abi_value), so both spellings take the float path at call sites
// and function entries.

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
    if (result.capacity)
    {
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

// The complex counterpart of the predicate above.  System V returns a
// `long double _Complex` under its COMPLEX_X87 class: the real half in ST(0)
// and the imaginary half in ST(1), where the same two f80 fields spelled as a
// plain struct go to memory through a hidden pointer.  The classifier owns
// that distinction; this only reads its answer back.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_abi_is_f80_complex_result(IrProgram* program, IrTypeId type_id)
{
    return ir_abi_value_is_complex_x87_result(program, type_id, IR_ABI_CONVENTION_SYSTEMV_X86_64);
}

// How many consecutive vector registers one ABI part occupies on this target,
// and how much of it each one carries. The IR ABI classifies a vector by the
// psABI rule alone -- a 512-bit vector is one vector part whatever the machine
// -- so a part can be wider than any register the target owns. It then travels
// in as many registers as it takes, which is the lowering clang emits for the
// same declaration: xmm0 through xmm3 for a 64-byte vector without AVX, ymm0
// and ymm1 with it. Zero means the target cannot carry the part at all.
u32 codegen_canonical_x64_vector_part_registers(Target const* target, u32 size, u32* register_size)
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

typedef struct CodegenCanonicalX64NonPowerVector CodegenCanonicalX64NonPowerVector;
struct CodegenCanonicalX64NonPowerVector
{
    u32 lane_count;
    u32 lane_size;
    u32 storage_size;
    bool floating;
};

// A GNU vector keeps its written lane count while its object image is rounded
// up to the next power of two. Power-of-two vectors have no padding and stay
// on the established paths; this describes only the padded shape.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_non_power_vector(IrProgram* program, IrType* type,
                                                                 CodegenCanonicalX64NonPowerVector* info)
{
    bool result = false;
    IrType* element = program && type && type->kind == IR_TYPE_VECTOR ? ir_type_from_id(&program->types, type->element_type) : 0;
    if (program && info && type && type->layout.resolved && type->layout.size && type->layout.size <= UINT32_MAX &&
        type->element_count && type->element_count <= UINT32_MAX && element && element->layout.resolved && element->layout.size &&
        element->layout.size <= 8 && (element->kind == IR_TYPE_INTEGER || element->kind == IR_TYPE_FLOAT) &&
        (element->kind != IR_TYPE_FLOAT || element->layout.size == 2 || element->layout.size == 4 || element->layout.size == 8))
    {
        u64 logical_size = type->element_count * element->layout.size;
        result = logical_size / element->layout.size == type->element_count && logical_size < type->layout.size &&
                 type->layout.size == next_power_of_two(logical_size);
        if (result)
        {
            *info = (CodegenCanonicalX64NonPowerVector){
                .lane_count = (u32)type->element_count,
                .lane_size = (u32)element->layout.size,
                .storage_size = (u32)type->layout.size,
                .floating = element->kind == IR_TYPE_FLOAT,
            };
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 codegen_canonical_x64_native_vector_width(Target const* target)
{
    return BUSTER_MAX(16u, target_vector_register_size(*target));
}

// Win64 passes a padded vector by one reference when the object fits a native
// vector register. Half-precision vectors use that vector contract even below
// sixteen bytes; integer vectors below sixteen bytes scalarize instead.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_windows_non_power_vector_indirect(
    Target const* target, CodegenCanonicalX64NonPowerVector const* info)
{
    return info && ((info->floating && info->lane_size == 2) ||
                    (info->storage_size >= 16 && info->storage_size <= codegen_canonical_x64_native_vector_width(target)));
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_non_power_vector_three_part_result(
    CodegenAbi abi, Target const* target, CodegenCanonicalX64NonPowerVector const* info)
{
    bool result = false;
    if (info && info->lane_count == 3)
    {
        bool native = info->storage_size <= codegen_canonical_x64_native_vector_width(target);
        if (abi == CODEGEN_ABI_X86_64_WINDOWS)
        {
            native = codegen_canonical_x64_windows_non_power_vector_indirect(target, info);
            result = !native && ((!info->floating && info->lane_size <= 8) || (info->floating && info->lane_size == 8));
        }
        else if (abi == CODEGEN_ABI_X86_64_SYSTEM_V)
        {
            result = info->storage_size > 16 && !native && info->lane_size == 8;
        }
    }
    return result;
}

// How many indirect references a Win64 vector argument travels as on this
// target: one per register-sized piece when the value is wider than the
// model's widest register, one for the whole value otherwise (including
// wrapping aggregates and the sub-eightbyte widths whose reference both
// sides of a buster build already agree on). piece_size is the register
// width exactly when the count is more than one. Zero pieces means the model
// cannot split the width evenly, which no power-of-two signature type
// produces.
u32 codegen_canonical_x64_windows_vector_argument_pieces(Target const* target, IrType* type, u32* piece_size)
{
    *piece_size = 0;
    if (!type || type->kind != IR_TYPE_VECTOR || !type->layout.resolved || type->layout.size < 8 || type->layout.size > UINT32_MAX ||
        (type->layout.size & (type->layout.size - 1)))
    {
        return 1;
    }
    u32 register_size = 0;
    u32 count = codegen_canonical_x64_vector_part_registers(target, (u32)type->layout.size, &register_size);
    if (count > 1)
    {
        *piece_size = register_size;
    }
    return count;
}

// The backend's half of the Win64 wide-vector result contract. The
// classification keeps a vector result past 64 bytes behind the reference
// because whether it comes back directly depends on the CPU model, which a
// classification keyed on convention alone cannot see: clang splits the value
// into registers of the widest width the model owns and returns it directly
// while at most four suffice (two zmm for 128 bytes on znver5, four ymm on
// haswell), and through the caller's hidden pointer past that (128 bytes at
// baseline is eight xmm-sized pieces, so it stays indirect). Every canonical
// read of a result classification funnels through this rewrite so the caller
// and callee sides of one build, and clang across the boundary, agree.
BUSTER_GLOBAL_LOCAL CodegenCanonicalAbiValue codegen_canonical_x64_vector_result(IrProgram* program, IrTypeId type_id, CodegenAbi abi,
                                                                                   Target const* target, CodegenCanonicalAbiValue value)
{
    IrType* type = ir_type_from_id(&program->types, type_id);
    CodegenCanonicalX64NonPowerVector non_power = {0};
    if (codegen_canonical_x64_non_power_vector(program, type, &non_power))
    {
        bool native = non_power.storage_size <= codegen_canonical_x64_native_vector_width(target);
        if (abi == CODEGEN_ABI_X86_64_WINDOWS)
        {
            native = codegen_canonical_x64_windows_non_power_vector_indirect(target, &non_power);
        }
        bool generic_system_v = abi == CODEGEN_ABI_X86_64_SYSTEM_V && non_power.storage_size <= 16;
        if (codegen_canonical_x64_non_power_vector_three_part_result(abi, target, &non_power))
        {
            value = (CodegenCanonicalAbiValue){.part_count = 3};
            for (u32 lane = 0; lane < 3; lane += 1)
            {
                value.parts[lane] = (CodegenCanonicalAbiPart){
                    .abi_class = non_power.floating ? lane == 2 ? IR_ABI_CLASS_X87 : IR_ABI_CLASS_FLOAT : IR_ABI_CLASS_INTEGER,
                    .value_offset = lane * non_power.lane_size,
                    .size = non_power.lane_size,
                };
            }
        }
        else if (!native && !generic_system_v)
        {
            value = (CodegenCanonicalAbiValue){.part_count = 1, .indirect = true};
            value.parts[0] = (CodegenCanonicalAbiPart){.abi_class = IR_ABI_CLASS_POINTER, .size = 8};
        }
    }
    else if (abi == CODEGEN_ABI_X86_64_WINDOWS && value.indirect && type && type->kind == IR_TYPE_VECTOR && type->layout.resolved &&
             type->layout.size > 64 && type->layout.size <= UINT32_MAX && !(type->layout.size & (type->layout.size - 1)))
    {
        u32 register_size = 0;
        u32 register_count = codegen_canonical_x64_vector_part_registers(target, (u32)type->layout.size, &register_size);
        if (register_count && register_count <= 4)
        {
            value = (CodegenCanonicalAbiValue){.part_count = 1};
            value.parts[0] = (CodegenCanonicalAbiPart){
                .abi_class = IR_ABI_CLASS_VECTOR,
                .size = (u32)type->layout.size,
            };
        }
    }
    return value;
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

// Whether a result travels in the registers its classification named, rather
// than in the single register the scalar path below loads it into. SystemV
// names every part of every result that way. Win64 names one part, and only a
// vector one has to come through here: its integer and floating-point results
// already have a path that carries their width, and a vector part is the only
// one that can be wider than the register the psABI picked for it.

BUSTER_GLOBAL_LOCAL CodegenCanonicalAbiValue codegen_canonical_aggregate_abi(IrProgram* program, IrTypeId type_id, CodegenAbi abi, bool is_result,
                                                                             bool variadic_argument)
{
    BUSTER_CHECK(abi < CODEGEN_ABI_COUNT);
    IrAbiConvention convention = codegen_canonical_ir_abi_convention(abi);
    IrAbiUse use = is_result ? IR_ABI_USE_RESULT : variadic_argument ? IR_ABI_USE_VARIADIC_ARGUMENT : IR_ABI_USE_ARGUMENT;
    return ir_type_abi_value(program, type_id, convention, use);
}

// AAPCS64's C.8 rule rounds the next general-purpose argument register up to
// an even number for a 16-byte integer pair.  The same type is sixteen-byte
// aligned when it spills to the incoming or outgoing stack area.  The IR ABI
// classifier owns the shape test; this helper only converts the convention's
// alignment requirement into the eightbyte cursors used by the canonical
// emitter.

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

// The counterpart to the shape predicate above: a type that carries an f80
// payload the classifier resolved without any x87 class.  System V's merger
// gives INTEGER precedence over x87, so musl's `union ldshape` -- a
// `long double` overlaid with `struct { uint64_t m; uint16_t se; }` --
// becomes two INTEGER eightbytes in general-purpose registers, while a union
// the merger cannot reconcile goes to memory whole.  Both are ordinary
// aggregates here: their bytes are copied, never pushed onto the x87 stack,
// so every gate below lets them fall through to the aggregate paths.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_type_is_f80_opaque_cached(CodegenCanonicalX64F80Cache const* cache, IrProgram* program,
                                                                          IrTypeId type_id)
{
    if (!cache || cache->allocation_failed || !program || !codegen_canonical_x64_type_contains_f80_cached(cache, program, type_id))
    {
        return false;
    }
    IrType* type = ir_type_from_id(&program->types, type_id);
    if (!type || !type->layout.resolved || (type->kind != IR_TYPE_STRUCT && type->kind != IR_TYPE_UNION && type->kind != IR_TYPE_ARRAY))
    {
        return false;
    }
    return !ir_abi_value_has_x87_part(program, type_id, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_ARGUMENT) &&
           !ir_abi_value_has_x87_part(program, type_id, IR_ABI_CONVENTION_SYSTEMV_X86_64, IR_ABI_USE_RESULT);
}

// A `long double _Complex`: an f80-carrying aggregate whose result really is
// an x87 pair, but whose argument, load, store and copy directions are the
// plain 32-byte memory image the size rule already gives it.  Only the two
// result sites -- the RETURN emitter and the call-result reader -- push or
// pop its halves; every other gate treats it like the opaque aggregates
// above.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_type_is_f80_complex_cached(CodegenCanonicalX64F80Cache const* cache, IrProgram* program,
                                                                            IrTypeId type_id)
{
    if (!cache || cache->allocation_failed || !program || !codegen_canonical_x64_type_contains_f80_cached(cache, program, type_id))
    {
        return false;
    }
    return codegen_canonical_x64_abi_is_f80_complex_result(program, type_id);
}

// The union the non-result gates want: an f80 payload that crosses this
// boundary as bytes, whether because the classifier resolved it without any
// x87 class or because it is a complex value away from its result position.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_type_is_f80_bytes_cached(CodegenCanonicalX64F80Cache const* cache, IrProgram* program,
                                                                          IrTypeId type_id)
{
    return codegen_canonical_x64_type_is_f80_opaque_cached(cache, program, type_id) ||
           codegen_canonical_x64_type_is_f80_complex_cached(cache, program, type_id);
}

bool codegen_canonical_x64_type_is_f80_x87_shape(IrProgram* program, IrTypeId type_id)
{
    TemporalArena temporary = scratch_begin(0, 0);
    CodegenCanonicalX64F80Cache cache = codegen_canonical_x64_f80_cache_initialize(temporary.arena, program);
    bool result = codegen_canonical_x64_type_is_f80_x87_shape_cached(&cache, program, type_id);
    scratch_end(temporary);
    return result;
}

bool codegen_canonical_integer_aggregate_parts(IrProgram* program, IrTypeId type_id, u32* part_count)
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
    // Any resolved vector is a copyable run of eightbytes to the canonical
    // emitter; the width cap that used to sit here only protected paths that
    // now size their copies from this count. How a wide vector crosses a call
    // boundary is the ABI classification's question, not this one.
    if (type && type->kind == IR_TYPE_VECTOR && type->layout.resolved && type->layout.size && type->layout.size <= (u64)UINT32_MAX * 8)
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
    u32 result;
    if (alignment < 8 || alignment > INT32_MAX || (alignment & (alignment - 1)))
    {
        result = 8;
    }
    else
    {
        result = (u32)alignment;
    }

    return result;
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
    layout->return_abi = codegen_canonical_x64_vector_result(
        program, instruction->canonical_type, abi, &target, codegen_canonical_aggregate_abi(program, instruction->canonical_type, abi, true, false));
    bool return_contains_f80 = codegen_canonical_x64_type_contains_f80_cached(f80_cache, program, callee_type->return_type);
    if (return_contains_f80 && (abi != CODEGEN_ABI_X86_64_SYSTEM_V ||
                                (!codegen_canonical_x64_type_is_f80_x87_shape_cached(f80_cache, program, callee_type->return_type) &&
                                 !codegen_canonical_x64_type_is_f80_complex_cached(f80_cache, program, callee_type->return_type) &&
                                 !codegen_canonical_x64_type_is_f80_opaque_cached(f80_cache, program, callee_type->return_type))))
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
        if (contains_f80 && (abi != CODEGEN_ABI_X86_64_SYSTEM_V ||
                             (!f80_x87_shape && !codegen_canonical_x64_type_is_f80_bytes_cached(f80_cache, program, type_id))))
        {
            return CODEGEN_ERROR_UNSUPPORTED_ABI;
        }
        // SysV puts both a scalar f80 and the canonical single-f80 aggregate
        // in a sixteen-byte, sixteen-aligned memory slot.  Any memory-class
        // aggregate (an f80 wrapper, or one whose walk hit a MEMORY field such
        // as a single-lane double vector) is marked here so the normal
        // stack-copy path does not mistake it for an unsupported register
        // aggregate.
        bool f80_memory = f80_x87_shape && argument_abi.memory && type->layout.size == 16;
        if (argument_abi.memory && (type->kind == IR_TYPE_STRUCT || type->kind == IR_TYPE_UNION || type->kind == IR_TYPE_ARRAY))
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
        CodegenCanonicalX64NonPowerVector non_power_vector = {0};
        bool windows_scalar_vector =
            abi == CODEGEN_ABI_X86_64_WINDOWS && codegen_canonical_x64_non_power_vector(program, type, &non_power_vector) &&
            !codegen_canonical_x64_windows_non_power_vector_indirect(&target, &non_power_vector);
        bool windows_indirect = abi == CODEGEN_ABI_X86_64_WINDOWS && argument_abi.indirect && !windows_scalar_vector;
        if (windows_scalar_vector)
        {
            aggregate = true;
            part_count = non_power_vector.lane_count;
        }
        else if (aggregate && abi == CODEGEN_ABI_X86_64_WINDOWS)
        {
            part_count = 1;
        }
        u32 windows_piece_size = 0;
        if (windows_indirect)
        {
            // A bare vector wider than the model's widest register legalizes
            // into one reference per register-sized piece, the way clang and
            // MSVC pass the same declaration: each piece is its own argument
            // slot, so a 128-byte vector is two references on znver5 and
            // eight at baseline. Wrapping aggregates keep the single
            // reference, as do vectors the model carries whole.
            part_count = codegen_canonical_x64_windows_vector_argument_pieces(&target, type, &windows_piece_size);
            if (!part_count)
            {
                return CODEGEN_ERROR_UNSUPPORTED_ABI;
            }
        }
        CodegenCanonicalCallArgument call_argument = {
            .abi = argument_abi,
            .type = type,
            .part_count = part_count,
            .stack_part_count = windows_scalar_vector ? part_count : (u32)((type->layout.size + 7) / 8),
            .windows_piece_size = windows_piece_size,
            .windows_scalar_lane_size = windows_scalar_vector ? non_power_vector.lane_size : 0,
            .float_register = UINT8_MAX,
            .aggregate = aggregate,
            .windows_indirect = windows_indirect,
            .windows_scalar_float = windows_scalar_vector && non_power_vector.floating,
            .system_v_aggregate = abi == CODEGEN_ABI_X86_64_SYSTEM_V && argument_abi.part_count && !argument_abi.memory && argument_in_registers,
        };
        u64 argument_stack_parts = 0;
        if (abi == CODEGEN_ABI_X86_64_SYSTEM_V && argument_abi.memory)
        {
            // The classifier already sent this argument to memory — an f80
            // slot, a single-lane double vector, or an aggregate the SysV
            // walk gave a MEMORY class — so it always lives in the outgoing
            // area regardless of how many registers remain.
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
        else if (windows_scalar_vector)
        {
            // Scalarized lanes are ordinary positional argument slots. The
            // leading lanes occupy the remaining register positions and the
            // tail continues in eightbyte stack slots in this same call.
            u32 available = layout->simulated_registers < register_count ? register_count - layout->simulated_registers : 0;
            u32 register_lanes = BUSTER_MIN(part_count, available);
            call_argument.windows_register_lane_count = register_lanes;
            layout->simulated_registers += register_lanes;
            if (register_lanes < part_count)
            {
                call_argument.on_stack = true;
                argument_stack_parts = part_count - register_lanes;
            }
        }
        else if (windows_piece_size)
        {
            // Pieces are ordinary argument slots, so unlike every other
            // multi-part shape they straddle: the leading pieces take the
            // registers that remain and the tail continues on the stack in
            // the same call, exactly as clang assigns them.
            u32 available = layout->simulated_registers < register_count ? register_count - layout->simulated_registers : 0;
            u32 register_pieces = BUSTER_MIN(part_count, available);
            call_argument.windows_register_piece_count = register_pieces;
            layout->simulated_registers += register_pieces;
            if (register_pieces < part_count)
            {
                call_argument.on_stack = true;
                argument_stack_parts = part_count - register_pieces;
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
    // A System V hidden result pointer has the same alignment contract as a
    // stack argument. Canonical frame slots are addressed from RBP, which is
    // only sixteen-aligned, so an over-aligned result must live in the
    // explicitly aligned outgoing area for the duration of the call. Reserve
    // it after the ABI-visible stack arguments; the callee never observes the
    // private tail of the area.
    if (abi == CODEGEN_ABI_X86_64_SYSTEM_V && layout->indirect_return)
    {
        IrType* bounce_type = ir_type_from_id(&program->types, instruction->canonical_type);
        u32 bounce_alignment = bounce_type && bounce_type->layout.resolved
                                   ? codegen_canonical_x64_stack_argument_alignment(bounce_type)
                                   : 0;
        if (bounce_type && bounce_type->layout.resolved && bounce_alignment > CODEGEN_X64_STACK_ALIGNMENT)
        {
            u64 bounce_size = bounce_type->layout.size;
            u64 bounce_offset = codegen_canonical_x64_stack_argument_offset(stack_part_count * 8, bounce_alignment);
            if (!bounce_size || bounce_size > UINT32_MAX || bounce_offset > UINT32_MAX ||
                bounce_size > UINT32_MAX - bounce_offset)
            {
                return CODEGEN_ERROR_CAPACITY;
            }
            layout->result_copy_offset = (u32)bounce_offset;
            layout->result_copy_size = (u32)bounce_size;
            layout->result_copy_alignment = bounce_alignment;
            stack_part_count = (bounce_offset + bounce_size + 7) / 8;
            layout->stack_alignment = BUSTER_MAX(layout->stack_alignment, bounce_alignment);
        }
    }
    if (stack_part_count > UINT32_MAX)
    {
        return CODEGEN_ERROR_CAPACITY;
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
        // Win64 keeps its outgoing area sixteen-aligned rather than aligning
        // RSP to the result type, so reserve slack around this private slot and
        // round the address itself. The bytes come home to the result's ordinary
        // frame slot immediately after the call.
        if (layout->windows_indirect_return && !layout->result_copy_size)
        {
            IrType* bounce_type = ir_type_from_id(&program->types, instruction->canonical_type);
            u64 bounce_alignment = bounce_type && bounce_type->layout.resolved ? codegen_canonical_x64_stack_argument_alignment(bounce_type) : 0;
            if (bounce_type && bounce_type->layout.resolved && bounce_alignment > CODEGEN_X64_STACK_ALIGNMENT)
            {
                u64 bounce_size = bounce_type->layout.size;
                u64 bounce_slack = bounce_alignment - CODEGEN_X64_STACK_ALIGNMENT;
                u64 bounce_remainder = copy_cursor & (CODEGEN_X64_STACK_ALIGNMENT - 1);
                if (bounce_remainder)
                {
                    copy_cursor += CODEGEN_X64_STACK_ALIGNMENT - bounce_remainder;
                }
                if (!bounce_size || bounce_size > UINT32_MAX || copy_cursor > UINT32_MAX || bounce_size > UINT32_MAX - copy_cursor ||
                    bounce_slack > UINT32_MAX - copy_cursor - bounce_size)
                {
                    return CODEGEN_ERROR_CAPACITY;
                }
                layout->result_copy_offset = (u32)copy_cursor;
                layout->result_copy_size = (u32)bounce_size;
                layout->result_copy_alignment = (u32)bounce_alignment;
                copy_cursor += bounce_size + bounce_slack;
            }
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
    CodegenError result;
    if (cache.allocation_failed)
    {
        result = CODEGEN_ERROR_CAPACITY;
    }
    else
    {
        result = codegen_canonical_x64_call_layout_cached(arena, program, &cache, function, instruction, abi, target, layout);
    }

    return result;
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
    bool result;
    if (!codegen_global_assembly_unsigned(operand, &exponent) || exponent > 12)
    {
        result = false;
    }
    else
    {
        *alignment = UINT64_C(1) << exponent;
        result = true;
    }

    return result;
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

// Names one symbol a module-level assembly block refers to, creating it when
// the translation unit never declared it in C. A crt's entry point is defined
// by the block and by nothing else, so requiring a C declaration would fail
// the very block that defines `_start` on the label that names it. `kind`
// decides only what a newly created symbol becomes; an existing one keeps the
// kind its declaration gave it.
IrSymbolId codegen_global_assembly_symbol(IrProgram* program, String8 name, Target target, IrSymbolKind kind)
{
    String8 alternate = name;
    if ((target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS) && alternate.length && alternate.pointer[0] == '_')
    {
        alternate.pointer += 1;
        alternate.length -= 1;
    }
    IrSymbolId result = IR_SYMBOL_ID_INVALID;
    for (u32 symbol_index = 0; symbol_index < program->symbols.count && result.value == IR_ID_UNDERLYING_INVALID; symbol_index += 1)
    {
        IrSymbol* symbol = &program->symbols.symbols[symbol_index];
        String8 link_name = symbol->link_name.length ? symbol->link_name : symbol->name;
        // A type symbol shares its spelling with a struct tag, which is not a
        // linker name; only code and data symbols can answer here.
        if ((symbol->kind == IR_SYMBOL_FUNCTION || symbol->kind == IR_SYMBOL_DATA) && (string_equal(link_name, name) || string_equal(link_name, alternate)))
        {
            result = (IrSymbolId){
                .value = symbol_index,
            };
        }
    }
    if (result.value == IR_ID_UNDERLYING_INVALID)
    {
        // The name points into the block's own source text, which outlives
        // every code-generation attempt: the retry that doubles the code
        // buffer rewinds the attempt arena, and a symbol added here survives
        // it and is found by the search above on the next pass rather than
        // being added twice.
        result = ir_program_add_symbol(program, (IrSymbol){
                                                    .name = name,
                                                    .type = IR_TYPE_ID_INVALID,
                                                    .kind = kind,
                                                    .linkage = IR_LINKAGE_EXTERNAL,
                                                });
    }

    return result;
}

// Where a symbol name the assembler reported has to live before it is written
// into a record. A symbol created during code generation outlives the attempt
// that created it -- the retry that grows the code buffer rewinds the attempt
// arena and finds the record again by name on the next pass -- so a name in
// memory that attempt owns would be read after it was released.
//
// A module-level block hands the assembler a line of the module's own text, so
// the reported name already points somewhere durable and `durable` is empty.
// An inline template hands over a substituted copy the attempt arena owns, and
// names the instruction's IR literal here: every symbol spelling reaches the
// substitution verbatim, so the same bytes sit in the literal, which nothing
// rewinds. A name that is not there is refused rather than recorded, because
// the only thing left to record would be the copy.
bool codegen_assembly_durable_name(String8 durable, String8* name)
{
    if (!durable.length)
    {
        return true;
    }
    for (u64 index = 0; index + name->length <= durable.length; index += 1)
    {
        if (memcmp(durable.pointer + index, name->pointer, name->length) == 0)
        {
            name->pointer = durable.pointer + index;
            return true;
        }
    }

    return false;
}

// True when every byte can appear in an assembler symbol name. It is what
// separates a label from a colon inside an operand: `%fs:0` is a segment
// override, not a definition of `%fs`.
BUSTER_GLOBAL_LOCAL bool codegen_global_assembly_name(String8 name)
{
    bool result = name.length != 0;
    for (u64 index = 0; index < name.length && result; index += 1)
    {
        char8 character = name.pointer[index];
        result = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') ||
                 character == '_' || character == '.' || character == '$';
    }

    return result;
}

// Defines the label chain at the head of a line -- `a: b: insn` names both at
// the current offset -- and advances `line` past it.
BUSTER_GLOBAL_LOCAL bool codegen_global_assembly_define_labels(IrProgram* program, Target target, CodegenBuffer* buffer, CodegenModule* result, String8* line)
{
    bool valid = true;
    bool done = false;
    while (valid && !done)
    {
        u64 colon = UINT64_MAX;
        for (u64 index = 0; index < line->length && colon == UINT64_MAX; index += 1)
        {
            colon = line->pointer[index] == ':' ? index : colon;
        }
        String8 name = codegen_global_assembly_trim((String8){
            .pointer = line->pointer,
            .length = colon == UINT64_MAX ? 0 : colon,
        });
        if (colon == UINT64_MAX || !codegen_global_assembly_name(name))
        {
            done = true;
        }
        else
        {
            IrSymbolId symbol = codegen_global_assembly_symbol(program, name, target, IR_SYMBOL_FUNCTION);
            if (symbol.value == IR_ID_UNDERLYING_INVALID)
            {
                valid = false;
            }
            else
            {
                program->symbols.symbols[symbol.value].is_definition = true;
                // A label in this block names an address in the text section,
                // so the definition is a function however the name was first
                // declared.
                program->symbols.symbols[symbol.value].kind = IR_SYMBOL_FUNCTION;
                result->entries[result->entry_count++] = (CodegenModuleEntry){
                    .symbol = symbol,
                    .offset = (u32)buffer->count,
                };
                line->pointer += colon + 1;
                line->length -= colon + 1;
                *line = codegen_global_assembly_trim(*line);
                done = line->length == 0;
            }
        }
    }

    return valid;
}

// Make module-level assembly definitions visible before function rows choose
// their relocations. Assembly is emitted after functions, so discovering a
// label only during emission would make a repeated codegen pass see a
// different `is_definition` state for calls to that label.
BUSTER_GLOBAL_LOCAL bool codegen_global_assembly_predeclare_labels(IrProgram* program, Target target, String8 source)
{
    bool valid = true;
    u64 line_start = 0;
    while (line_start < source.length && valid)
    {
        u64 line_end = line_start;
        while (line_end < source.length && source.pointer[line_end] != '\n')
        {
            line_end += 1;
        }
        String8 line = codegen_global_assembly_trim((String8){
            .pointer = source.pointer + line_start,
            .length = line_end - line_start,
        });
        line_start = line_end < source.length ? line_end + 1 : source.length;
        if (line.length && line.pointer[0] != '#')
        {
            bool done = false;
            while (valid && !done)
            {
                u64 colon = UINT64_MAX;
                for (u64 index = 0; index < line.length && colon == UINT64_MAX; index += 1)
                {
                    colon = line.pointer[index] == ':' ? index : colon;
                }
                String8 name = codegen_global_assembly_trim((String8){
                    .pointer = line.pointer,
                    .length = colon == UINT64_MAX ? 0 : colon,
                });
                if (colon == UINT64_MAX || !codegen_global_assembly_name(name))
                {
                    done = true;
                }
                else
                {
                    IrSymbolId symbol = codegen_global_assembly_symbol(program, name, target, IR_SYMBOL_FUNCTION);
                    if (symbol.value == IR_ID_UNDERLYING_INVALID)
                    {
                        valid = false;
                    }
                    else
                    {
                        program->symbols.symbols[symbol.value].is_definition = true;
                        program->symbols.symbols[symbol.value].kind = IR_SYMBOL_FUNCTION;
                        line.pointer += colon + 1;
                        line.length -= colon + 1;
                        line = codegen_global_assembly_trim(line);
                        done = line.length == 0;
                    }
                }
            }
        }
    }

    return valid;
}

// `.byte 1, 2, 0x03`.
BUSTER_GLOBAL_LOCAL bool codegen_global_assembly_emit_bytes(CodegenBuffer* buffer, String8 values)
{
    bool valid = true;
    bool done = values.length == 0;
    while (valid && !done)
    {
        u64 comma = values.length;
        for (u64 index = 0; index < values.length && comma == values.length; index += 1)
        {
            comma = values.pointer[index] == ',' ? index : comma;
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
            valid = false;
        }
        else
        {
            codegen_emit_u8(buffer, (u8)value);
            done = comma == values.length;
            values.pointer += done ? 0 : comma + 1;
            values.length -= done ? 0 : comma + 1;
            // A list ending in a separator has no value after it to parse,
            // and never had one to reject.
            done = done || values.length == 0;
        }
    }

    return valid;
}

// `.p2align <exponent>`, padded with the target's one-instruction no-op.
BUSTER_GLOBAL_LOCAL bool codegen_global_assembly_emit_alignment(CodegenBuffer* buffer, Target target, String8 operand)
{
    u64 alignment = 0;
    bool valid = codegen_global_assembly_alignment(operand, &alignment);
    // The emit helpers refuse a byte the buffer cannot hold without advancing
    // its count, so a reserve too small for this padding has to end the loop
    // here instead of asking forever.
    while (valid && (buffer->count & (alignment - 1)) && buffer->error == CODEGEN_ERROR_NONE)
    {
        if (target.cpu_arch == CPU_ARCH_X86_64)
        {
            valid = codegen_canonical_x64_metadata_emit(buffer, S8("NOP"), 0, 0);
        }
        else if (buffer->count & 3)
        {
            valid = false;
        }
        else
        {
            codegen_emit_u32(buffer, 0xd503201f);
        }
    }

    return valid;
}

// The directives that speak about a symbol instead of about bytes. `.size` is
// accepted and dropped: the emitter already computes every entry's size from
// the next entry's offset.
typedef struct CodegenGlobalAssemblySymbolDirective CodegenGlobalAssemblySymbolDirective;
struct CodegenGlobalAssemblySymbolDirective
{
    String8 spelling;
    bool external;
    bool weak;
    bool hidden;
    bool typed;
};

BUSTER_GLOBAL_LOCAL CodegenGlobalAssemblySymbolDirective const codegen_global_assembly_symbol_directives[] = {
    {S8_INITIALIZER(".globl"), true, false, false, false},  {S8_INITIALIZER(".global"), true, false, false, false},
    {S8_INITIALIZER(".weak"), true, true, false, false},    {S8_INITIALIZER(".hidden"), false, false, true, false},
    {S8_INITIALIZER(".type"), false, false, false, true},   {S8_INITIALIZER(".size"), false, false, false, false},
};

// True when `line` starts with `directive` and the directive name ends there,
// so `.globl` does not also answer for a hypothetical `.globlfoo`.
BUSTER_GLOBAL_LOCAL bool codegen_global_assembly_directive(String8 line, String8 directive)
{
    bool result = line.length >= directive.length && memcmp(line.pointer, directive.pointer, directive.length) == 0;
    if (result && line.length > directive.length)
    {
        char8 next = line.pointer[directive.length];
        result = next == ' ' || next == '\t';
    }

    return result;
}

// Applies one symbol directive. `recognized` stays false for a line no entry
// in the table claims, which the caller reports as unsupported. `durable_names`
// is where a name a new symbol record keeps has to live; see
// codegen_assembly_durable_name.
bool codegen_global_assembly_apply_symbol_directive(IrProgram* program, Target target, String8 line, String8 durable_names,
                                                    bool* recognized)
{
    bool valid = true;
    for (u32 directive_index = 0; directive_index < BUSTER_ARRAY_LENGTH(codegen_global_assembly_symbol_directives) && !*recognized; directive_index += 1)
    {
        CodegenGlobalAssemblySymbolDirective directive = codegen_global_assembly_symbol_directives[directive_index];
        if (!codegen_global_assembly_directive(line, directive.spelling))
        {
            continue;
        }
        *recognized = true;
        String8 operands = codegen_global_assembly_trim((String8){
            .pointer = line.pointer + directive.spelling.length,
            .length = line.length - directive.spelling.length,
        });
        String8 name = operands;
        for (u64 index = 0; index < operands.length; index += 1)
        {
            if (operands.pointer[index] == ',')
            {
                name.length = index;
                break;
            }
        }
        name = codegen_global_assembly_trim(name);
        // A directive alone never says "function": `.weak` on a data symbol is
        // what a position-independent crt writes about `_DYNAMIC`. Only a
        // label definition, or an explicit `.type`, promotes the kind.
        // The record keeps `durable`, while `name` stays pointing into the line
        // so the `.type` operand after it can still be found.
        String8 durable = name;
        bool named = codegen_global_assembly_name(name) && codegen_assembly_durable_name(durable_names, &durable);
        IrSymbolId symbol = named ? codegen_global_assembly_symbol(program, durable, target, IR_SYMBOL_DATA) : IR_SYMBOL_ID_INVALID;
        if (symbol.value == IR_ID_UNDERLYING_INVALID)
        {
            valid = false;
        }
        else
        {
            IrSymbol* record = &program->symbols.symbols[symbol.value];
            record->linkage = directive.external ? IR_LINKAGE_EXTERNAL : record->linkage;
            record->is_weak = record->is_weak || directive.weak;
            record->is_hidden = record->is_hidden || directive.hidden;
            if (directive.typed && !record->is_definition)
            {
                // `@function` in GAS, `%function` on the targets where `@`
                // starts a comment, and the spelled-out `STT_FUNC` are the
                // forms a libc's startup assembly uses.
                String8 type_operand = codegen_global_assembly_trim((String8){
                    .pointer = name.pointer + name.length,
                    .length = operands.length - (u64)(name.pointer - operands.pointer) - name.length,
                });
                bool function_type = string_ends_with_sequence(type_operand, S8("function")) || string_ends_with_sequence(type_operand, S8("STT_FUNC"));
                bool object_type = string_ends_with_sequence(type_operand, S8("object")) || string_ends_with_sequence(type_operand, S8("STT_OBJECT"));
                record->kind = function_type ? IR_SYMBOL_FUNCTION : object_type ? IR_SYMBOL_DATA : record->kind;
            }
        }
    }

    return valid;
}

// The instruction forms this emitter encodes without the assembler. They are
// kept because they predate the assembler path and because the two `-masm`
// dialects spell them identically; `emitted` stays false for everything else.
BUSTER_GLOBAL_LOCAL bool codegen_global_assembly_emit_fixed_instruction(CodegenBuffer* buffer, Target target, String8 instruction, bool* emitted)
{
    bool valid = true;
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        String8 mnemonic = {0};
        if (string_equal(instruction, S8("ret")) || string_equal(instruction, S8("retq")))
        {
            mnemonic = S8("RET");
        }
        else if (string_equal(instruction, S8("nop")))
        {
            mnemonic = S8("NOP");
        }
        else if (string_equal(instruction, S8("ud2")))
        {
            mnemonic = S8("UD2");
        }
        if (mnemonic.length)
        {
            valid = codegen_canonical_x64_metadata_emit(buffer, mnemonic, 0, 0);
            *emitted = valid;
        }
        else
        {
            String8 prefixes[] = {
                S8("mov$"),
                S8("movl$"),
            };
            for (u32 prefix_index = 0; prefix_index < BUSTER_ARRAY_LENGTH(prefixes) && !*emitted && valid; prefix_index += 1)
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
                    valid = false;
                }
                else
                {
                    BusterX86MetadataPhysicalOperand operands[2] = {
                        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32),
                        codegen_canonical_x64_metadata_unsigned_immediate(immediate, 32),
                    };
                    valid = codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), operands, BUSTER_ARRAY_LENGTH(operands));
                    *emitted = valid;
                }
            }
        }
    }
    else
    {
        String8 movw0 = S8("movw0,#");
        if (string_equal(instruction, S8("ret")))
        {
            codegen_emit_u32(buffer, 0xd65f03c0);
            *emitted = true;
        }
        else if (string_equal(instruction, S8("nop")))
        {
            codegen_emit_u32(buffer, 0xd503201f);
            *emitted = true;
        }
        else if (string_equal(instruction, S8("brk#0")))
        {
            codegen_emit_u32(buffer, 0xd4200000);
            *emitted = true;
        }
        else if (instruction.length > movw0.length && memcmp(instruction.pointer, movw0.pointer, movw0.length) == 0)
        {
            u64 immediate = 0;
            if (!codegen_global_assembly_unsigned(
                    (String8){
                        .pointer = instruction.pointer + movw0.length,
                        .length = instruction.length - movw0.length,
                    },
                    &immediate) ||
                immediate > UINT16_MAX)
            {
                valid = false;
            }
            else
            {
                codegen_emit_u32(buffer, 0x52800000 | ((u32)immediate << 5));
                *emitted = true;
            }
        }
    }

    return valid;
}

// Translates one relocation the assembler reported against an undefined name
// into the module vocabulary. False means the module cannot express that
// family, which the caller reports as an unsupported instruction rather than
// emitting an unrelocated reference.
BUSTER_GLOBAL_LOCAL bool codegen_global_assembly_relocation_kind(AssemblyRelocationKind kind, CodegenModuleRelocationKind* module_kind)
{
    bool result = true;
    switch (kind)
    {
        case ASSEMBLY_RELOCATION_X86_PC32:
            *module_kind = CODEGEN_MODULE_RELOCATION_X86_64_PC32;
            break;
        case ASSEMBLY_RELOCATION_X86_ABSOLUTE32:
            *module_kind = CODEGEN_MODULE_RELOCATION_ABSOLUTE32;
            break;
        case ASSEMBLY_RELOCATION_X86_ABSOLUTE64:
            *module_kind = CODEGEN_MODULE_RELOCATION_ABSOLUTE64;
            break;
        case ASSEMBLY_RELOCATION_AARCH64_CALL26:
            *module_kind = CODEGEN_MODULE_RELOCATION_AARCH64_CALL26;
            break;
        case ASSEMBLY_RELOCATION_AARCH64_BRANCH26:
            *module_kind = CODEGEN_MODULE_RELOCATION_AARCH64_BRANCH26;
            break;
        default:
            result = false;
            break;
    }

    return result;
}

// Assembles one instruction line through the same assembler the inline
// assembly path uses, and records the relocations it reports against names
// the block does not define. `call sym` and `lea sym(%rip),%reg` -- what a
// crt's entry point is made of -- are the two shapes this exists for.
BUSTER_GLOBAL_LOCAL bool codegen_global_assembly_encode_instruction(Arena* arena, IrProgram* program, Target target, CodegenModuleOptions options, String8 line,
                                                                     String8 durable_names, CodegenBuffer* buffer, CodegenModule* result,
                                                                     u32 relocation_capacity)
{
    u32 instruction_offset = (u32)buffer->count;
    AssemblyEncodeResult encoded = assembly_encode(arena, line,
                                                    (AssemblyEncodeOptions){
                                                        .target = target,
                                                        // The AT&T/Intel distinction is x86-only, and the
                                                        // assembler rejects either spelling for another
                                                        // target rather than ignoring it.
                                                        .syntax = target.cpu_arch == CPU_ARCH_X86_64 ? codegen_inline_assembly_syntax(options)
                                                                                                     : ASSEMBLY_SYNTAX_DEFAULT,
                                                    });
    u8* encoded_bytes = 0;
    bool valid = !encoded.diagnostic_count && result->relocation_count <= relocation_capacity &&
                 encoded.relocation_count <= relocation_capacity - result->relocation_count &&
                 codegen_buffer_reserve(buffer, encoded.bytes.length, &encoded_bytes);
    if (valid && encoded.bytes.length)
    {
        memcpy(encoded_bytes, encoded.bytes.pointer, encoded.bytes.length);
    }
    for (u32 relocation_index = 0; relocation_index < encoded.relocation_count && valid; relocation_index += 1)
    {
        AssemblyRelocation relocation = encoded.relocations[relocation_index];
        CodegenModuleRelocationKind kind = CODEGEN_MODULE_RELOCATION_X86_64_PC32;
        IrSymbolId symbol = IR_SYMBOL_ID_INVALID;
        valid = relocation.symbol < encoded.symbol_count && codegen_global_assembly_relocation_kind(relocation.kind, &kind) &&
                relocation.offset <= UINT32_MAX - instruction_offset;
        // The assembler's PC-relative addend already carries the distance from
        // the relocated field to the end of its instruction. The module
        // vocabulary's does not: the object writer subtracts four on the way
        // out, which is that distance only for a field that ends the
        // instruction. Adding those four back here leaves one addend that is
        // right whatever follows the field.
        s64 addend = relocation.addend;
        if (kind == CODEGEN_MODULE_RELOCATION_X86_64_PC32)
        {
            valid = valid && addend <= INT64_MAX - 4;
            addend += 4;
        }
        if (valid)
        {
            String8 name = encoded.symbols[relocation.symbol].name;
            valid = codegen_assembly_durable_name(durable_names, &name);
            if (valid)
            {
                symbol = codegen_global_assembly_symbol(program, name, target, IR_SYMBOL_DATA);
                valid = symbol.value != IR_ID_UNDERLYING_INVALID;
            }
        }
        if (valid)
        {
            result->relocations[result->relocation_count++] = (CodegenModuleRelocation){
                .addend = addend,
                .symbol = symbol,
                .offset = instruction_offset + (u32)relocation.offset,
                .source = CODEGEN_MODULE_RELOCATION_CODE,
                .kind = (u8)kind,
            };
        }
    }

    return valid;
}

// Emits one module-level `__asm__` block into the module's text.
//
// Directives are interpreted here: `.text` is the only section this can emit
// into, `.byte` and `.p2align` write bytes directly, and the table above
// speaks about symbols. Everything else on a line is an instruction: the few
// forms with a fixed encoding go through the canonical metadata bridge, and
// the rest go to the real assembler with relocation support.
//
// `failed_line` receives the one-based line inside `assembly.source` that
// stopped the block, so the diagnostic can name the assembly rather than the
// next C function in the file. It is zero when the block was emitted whole.
BUSTER_GLOBAL_LOCAL bool codegen_emit_global_assembly(Arena* arena, IrProgram* program, IrModuleAssembly assembly, Target target, CodegenModuleOptions options,
                                                      CodegenBuffer* buffer, CodegenModule* result, u32 relocation_capacity, u32* failed_line)
{
    bool valid = true;
    u64 line_start = 0;
    u32 line_number = 0;
    while (line_start < assembly.source.length && valid)
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
        line_number += 1;
        *failed_line = line_number;
        if (line.length && line.pointer[0] != '#')
        {
            valid = codegen_global_assembly_define_labels(program, target, buffer, result, &line);
        }
        else
        {
            line.length = 0;
        }
        if (valid && line.length && line.pointer[0] == '.')
        {
            bool recognized = codegen_global_assembly_directive(line, S8(".text"));
            if (line.length >= 5 && memcmp(line.pointer, ".byte", 5) == 0)
            {
                recognized = true;
                valid = codegen_global_assembly_emit_bytes(buffer, (String8){
                                                                       .pointer = line.pointer + 5,
                                                                       .length = line.length - 5,
                                                                   });
            }
            else if (line.length >= 8 && memcmp(line.pointer, ".p2align", 8) == 0)
            {
                recognized = true;
                valid = codegen_global_assembly_emit_alignment(buffer, target,
                                                                (String8){
                                                                    .pointer = line.pointer + 8,
                                                                    .length = line.length - 8,
                                                                });
            }
            else if (!recognized)
            {
                valid = codegen_global_assembly_apply_symbol_directive(program, target, line, (String8){0}, &recognized);
            }
            valid = valid && recognized;
            line.length = 0;
        }
        if (valid && line.length)
        {
            char8* normalized = arena_allocate(arena, char8, line.length);
            u64 normalized_length = 0;
            for (u64 index = 0; index < line.length; index += 1)
            {
                if (line.pointer[index] != ' ' && line.pointer[index] != '\t')
                {
                    normalized[normalized_length++] = line.pointer[index];
                }
            }
            bool emitted = false;
            valid = codegen_global_assembly_emit_fixed_instruction(buffer, target,
                                                                    (String8){
                                                                        .pointer = normalized,
                                                                        .length = normalized_length,
                                                                    },
                                                                    &emitted);
            // The assembler sees the untouched line rather than the
            // whitespace-stripped spelling the comparisons above want.
            valid = valid &&
                    (emitted ||
                     codegen_global_assembly_encode_instruction(arena, program, target, options, line, (String8){0}, buffer, result, relocation_capacity));
        }
        valid = valid && buffer->error == CODEGEN_ERROR_NONE;
    }
    *failed_line = valid ? 0 : *failed_line;

    return valid;
}

// Emits one already-resolved inline-assembly template into the function's text.
//
// It is codegen_emit_global_assembly's arm for a block that sits inside a
// function body, and it recognizes the same two kinds of line. A leading-dot
// line speaks about a symbol and goes through the same directive table, which
// is how `.hidden sym` in front of a PC-relative reference reaches the object;
// every other line is an instruction the assembler encodes, with the
// relocations it reports recorded against the module. That last part is the
// difference from what this path did before: a template naming a symbol was
// refused outright rather than relocated, which is what kept a libc's
// GETFUNCSYM out.
//
// Labels are deliberately not defined. A module-level block is emitted once
// per file, while a template is emitted once per instruction that carries it,
// so a label here would collide with itself the second time the same block
// appears; codegen_inline_assembly_register_only_source has already refused
// one for that reason.
//
// `source` is the substituted text, which the attempt arena owns; `literal` is
// the template it came from, which the IR owns. Both are needed because a
// symbol outlives the attempt -- see codegen_assembly_durable_name.

// Keep the low `bytes` bytes of a register and zero the rest. An atomic
// aggregate narrower than the width it is accessed through -- a three-byte
// record stored through four bytes (#731) -- has to write its padding
// deterministically, and Clang writes zero there; the shift pair needs neither
// a second register nor a 64-bit immediate. A full or empty span is a no-op:
// the shift amount would be out of range and there is nothing to clear.

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

BUSTER_GLOBAL_LOCAL bool codegen_canonical_a64_memory_operation_base(CodegenBuffer* buffer, u32 register_number, u32 offset, u32 size, bool store,
                                                                     bool sign_extend, u32 base_register)
{
    u32 scale = size == 8 ? 8 : size == 4 ? 4 : size == 2 ? 2 : size == 1 ? 1 : 0;
    if (!scale || register_number > 31)
    {
        return false;
    }
    // The scaled unsigned-immediate form addresses only multiples of the
    // access width, and a bit-field's storage unit lands wherever packing put
    // it: `struct __attribute__((packed)) { char a; int v : 32; }` is read
    // through the four bytes at offset one, and the pieces of a field with no
    // single unit land at every offset in turn. The unscaled form -- LDUR and
    // STUR -- takes any byte offset in a nine-bit field, which is exactly the
    // offsets the scaled one cannot reach. Beyond that field the address is
    // materialized, and then the offset is zero and scaled again.
    bool unscaled = offset % scale != 0;
    bool indirect = unscaled ? offset > A64_UNSCALED_IMM_MAX : offset / scale > A64_IMM12_MAX;
    if (indirect)
    {
        codegen_canonical_a64_base_address(buffer, 16, base_register, offset);
        offset = 0;
        unscaled = false;
    }
    if (unscaled)
    {
        u32 unscaled_instruction = store         ? (size == 8 ? 0xf80003e0 : size == 4 ? 0xb80003e0 : size == 2 ? 0x780003e0 : 0x380003e0)
                                   : sign_extend ? (size == 4 ? 0xb88003e0 : size == 2 ? 0x788003e0 : size == 1 ? 0x388003e0 : 0xf84003e0)
                                                 : (size == 8 ? 0xf84003e0 : size == 4 ? 0xb84003e0 : size == 2 ? 0x784003e0 : 0x384003e0);
        codegen_emit_u32(buffer, (unscaled_instruction & ~(31u << 5)) | (offset << 12) | (base_register << 5) | register_number);
        return true;
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

bool codegen_canonical_a64_frame_memory_operation(CodegenBuffer* buffer, u32 register_number, u32 offset, u32 size, bool store,
                                                                      bool sign_extend)
{
    return codegen_canonical_a64_memory_operation_base(buffer, register_number, offset, size, store, sign_extend, 28);
}

u32 codegen_canonical_a64_remainder_divide_instruction(bool signed_remainder, bool wide)
{
    return (signed_remainder ? 0x1aca0d2b : 0x1aca092b) | (wide ? 0x80000000 : 0);
}

// The address of an outgoing-argument slot. The stack pointer is only sixteen
// aligned through the body, so a slot an argument needs more alignment than
// that is reserved with room to spare and rounded up here, the same lea/add/and
// an over-aligned local is given.

// The caller-owned copy of one indirectly passed argument, moved from its frame
// slot into the outgoing area an eightbyte at a time. An argument that wants
// more than the stack pointer's sixteen bytes of alignment is written through
// r11 instead, holding the rounded-up address of its over-reserved slot: r11 is
// volatile, carries no argument, and every copy re-materializes it.

// The reverse move: bytes a callee stored through an over-aligned
// outgoing-area slot come home to a value's frame slot an eightbyte at a
// time. R11 re-derives the rounded-up address the callee wrote through, the
// same way the slot's address was staged before the call.

// Reconstruct an ELF va_arg from the ABI image named by X12. Indirect
// composites must read exactly their object bytes, including a short tail.

// A branch whose target block was not placed when the branch was emitted. The
// generator records the field to overwrite and fills every one of them once
// the block offsets are known.
// Which data section a global's bytes belong in. `const` is the frontend's
// answer and the read-only section is its usual home, but an object that
// carries a relocation cannot stay there: those bytes are written when the
// program is relocated, and in a shared object that write lands on a page the
// loader mapped read-only, which is a DT_TEXTREL the loader has to undo or
// refuse. Clang answers the same question with a fourth data section,
// `.data.rel.ro`; this writer has three, so a read-only object holding a
// relocation is laid out with the writable ones. Nothing in the C object model
// moves with it -- writing through a `const` lvalue is undefined either way --
// and a static link, whose relocations are all resolved before the program
// runs, cannot tell the difference.
BUSTER_GLOBAL_LOCAL bool codegen_global_is_read_only(IrGlobal* global)
{
    return global->is_read_only && !global->relocation_count && global->initializer_kind != IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS;
}

// What one canonical value of a type costs the frame, read by the capacity
// estimate's walk over every value of every function in place of the type
// record itself: the record is ~152 bytes and the walk wants 13 of them, so
// a million random reads of it were the walk's whole cost. One row per
// program type, built once per module beside the f80 cache -- it depends on
// nothing an attempt produces, so it survives a retry -- and eight rows share
// a cache line. `size` is the slot rounded up to eightbytes and never under
// one; `alignment` is the type's, never under eight. A zero `size` marks a
// type the walk cannot size, an unresolved layout or one past the u32 frame
// the emitter addresses, which is an error for every value but a global place.
typedef struct CodegenSlotCost CodegenSlotCost;
struct CodegenSlotCost
{
    u32 size;
    u32 alignment;
};
BUSTER_CT_CHECK(sizeof(CodegenSlotCost) == 8);

BUSTER_GLOBAL_LOCAL CodegenSlotCost* codegen_slot_costs_build(Arena* arena, IrTypeTable const* types)
{
    CodegenSlotCost* result = arena_allocate(arena, CodegenSlotCost, types->count ? types->count : 1);
    for (u32 type_index = 0; type_index < types->count; type_index += 1)
    {
        IrTypeLayout layout = types->types[type_index].layout;
        bool sized = layout.resolved && layout.size <= UINT32_MAX - 7;
        u64 slot_size = (layout.size + 7) & ~(u64)7;
        result[type_index] = (CodegenSlotCost){
            .size = sized ? (u32)BUSTER_MAX(slot_size, 8u) : 0,
            .alignment = BUSTER_MAX(layout.alignment, 8u),
        };
    }
    return result;
}

// The line rows of one machine-emitted function, from the selector's marks --
// one per lowered instruction, in row order -- and the encoder's row offsets.
// A position is recovered here rather than carried through selection, so a
// row that never reaches the line table costs nothing to resolve, and which
// rows those are is known before any position is: the record helper drops a
// row at the last recorded offset (a row that emitted no bytes, or one behind
// a row that did) and drops everything once the table is full, so those marks
// are not resolved at all -- three marks in ten on the self-host stage. A mark
// that repeats its predecessor's range, which every instruction of one
// expression does, takes the position already in hand instead of the program
// cursor's memo behind two calls. The rows appended, and their order, are
// exactly those of resolving every mark: a position only ever decides whether
// a row is appended, never where, and a mark skipped here would have been
// dropped on its offset alone.
BUSTER_GLOBAL_LOCAL void codegen_record_machine_line_marks(IrProgram* program, IrFunction* ir_function, CodegenModule* result,
                                                            u32 line_entry_capacity, u32 line_source_limit, MachineFunction const* function,
                                                            u32 const* row_offsets, u32 code_base)
{
    CodegenLineEntry* entries = result->line_entries;
    if (entries)
    {
        // The mark names its IR row; its source range is recovered here
        // rather than carried through selection, and a row without one is
        // skipped before the last-entry test so the entry sequence is the
        // one the ranged-only marks produced. A memo source of
        // IR_ID_UNDERLYING_INVALID reads as "no position yet".
        u32 memo_source = IR_ID_UNDERLYING_INVALID;
        u32 memo_offset = 0;
        IrSourcePosition memo_position = {0};
        for (u32 mark_index = 0; mark_index < function->line_mark_count; mark_index += 1)
        {
            MachineLineMark mark = function->line_marks[mark_index];
            IrSourceRange mark_source = ir_instruction_canonical_source(ir_function, (IrInstructionId){.value = mark.instruction});
            if (mark.row < function->instruction_count && mark_source.source.value != IR_ID_UNDERLYING_INVALID)
            {
                u32 code_offset = code_base + row_offsets[mark.row];
                u32 count = result->line_entry_count;
                bool dropped = count >= line_entry_capacity || (count && entries[count - 1].code_offset == code_offset);
                if (!dropped)
                {
                    if (mark_source.source.value != memo_source || mark_source.offset != memo_offset)
                    {
                        memo_position = ir_source_position(program, mark_source);
                        memo_source = mark_source.source.value;
                        memo_offset = mark_source.offset;
                    }
                    codegen_record_line_hot(entries, &result->line_entry_count, line_entry_capacity, code_offset, mark_source.source.value,
                                            line_source_limit, memo_position.line, memo_position.column);
                }
            }
        }
    }
}

typedef struct CodegenMachineDebugReference CodegenMachineDebugReference;
struct CodegenMachineDebugReference
{
    s32 physical_register;
    u32 epoch;
    bool frame_valid;
    bool prefer_frame;
    u8 reserved[2];
};

BUSTER_GLOBAL_LOCAL DebugRegister codegen_machine_debug_register(MachineFunction const* function, u32 virtual_register, u32 physical_register,
                                                                  u32 value_size, Target target)
{
    DebugRegister result = DEBUG_REGISTER_NONE;
    MachineRegisterClass register_class = virtual_register < function->virtual_register_count
                                              ? (MachineRegisterClass)function->virtual_registers[virtual_register].register_class
                                              : MACHINE_REGISTER_CLASS_NONE;
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        if (register_class == MACHINE_REGISTER_CLASS_GENERAL && physical_register < 16)
        {
            result = (DebugRegister)((u32)DEBUG_REGISTER_X86_RAX + physical_register);
        }
        else if (register_class == MACHINE_REGISTER_CLASS_VECTOR && value_size && value_size <= 16 && physical_register >= MACHINE_X64_ZMM0 &&
                 physical_register <= MACHINE_X64_ZMM15)
        {
            result = (DebugRegister)((u32)DEBUG_REGISTER_X86_XMM0 + physical_register - MACHINE_X64_ZMM0);
        }
    }
    else if (target.cpu_arch == CPU_ARCH_AARCH64 && physical_register < 32)
    {
        if (register_class == MACHINE_REGISTER_CLASS_GENERAL)
        {
            result = (DebugRegister)((u32)DEBUG_REGISTER_AARCH64_X0 + physical_register);
        }
        else if (register_class == MACHINE_REGISTER_CLASS_VECTOR)
        {
            result = (DebugRegister)((u32)DEBUG_REGISTER_AARCH64_V0 + physical_register);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool codegen_machine_debug_edit_writes_register(MachineEdit const* edit)
{
    return edit->kind == MACHINE_EDIT_RELOAD || edit->kind == MACHINE_EDIT_COPY || edit->kind == MACHINE_EDIT_TEMP_RELOAD ||
           edit->kind == MACHINE_EDIT_REMATERIALIZE || edit->kind == MACHINE_EDIT_FRAME_RELOAD;
}

BUSTER_GLOBAL_LOCAL bool codegen_machine_debug_frame_offset(u32 placement_offset, u32 frame_base_offset, Target target, s32* result)
{
    s64 offset = target.cpu_arch == CPU_ARCH_X86_64 ? (s64)frame_base_offset - placement_offset : (s64)placement_offset;
    if (target.cpu_arch == CPU_ARCH_AARCH64 && !target_uses_pe_unwind(target))
    {
        offset += 16;
    }
    if (target.cpu_arch == CPU_ARCH_AARCH64)
    {
        offset = -offset;
    }
    if (offset < INT32_MIN || offset > INT32_MAX)
    {
        return false;
    }
    *result = (s32)offset;
    return true;
}

BUSTER_GLOBAL_LOCAL void codegen_machine_debug_edit_state(MachineFunction const* function, MachineStackPlacement const* placement,
                                                           u32 virtual_register, MachineEdit const* edit,
                                                           CodegenMachineDebugReference* state, bool selected_frame, s32 selected_register,
                                                           bool* selected_invalid)
{
    bool own = edit->subject == virtual_register;
    if (edit->kind == MACHINE_EDIT_SPILL)
    {
        u32 offset = edit->subject < function->virtual_register_count ? placement->virtual_register_offsets[edit->subject] : UINT32_MAX;
        if (own)
        {
            state->frame_valid = true;
            state->prefer_frame = true;
            state->epoch += 1;
        }
        else if (state->frame_valid && virtual_register < function->virtual_register_count && offset != MACHINE_VIRTUAL_REGISTER_NO_HOME &&
                 offset == placement->virtual_register_offsets[virtual_register])
        {
            state->frame_valid = false;
            *selected_invalid |= selected_frame;
        }
    }
    if (codegen_machine_debug_edit_writes_register(edit))
    {
        bool rematerializes_own = false;
        if (edit->kind == MACHINE_EDIT_REMATERIALIZE)
        {
            MachinePoint definition = function->virtual_registers[virtual_register].definition_point;
            u32 row = definition == MACHINE_POINT_INVALID ? UINT32_MAX : machine_point_instruction(definition);
            if (row < function->instruction_count)
            {
                MachineInstruction const* instruction = function->instructions + row;
                MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
                rematerializes_own = info && info->operand_count >= 2 &&
                    machine_ref_kind(instruction->operands[0]) == MACHINE_REF_VIRTUAL_REGISTER &&
                    machine_ref_payload(instruction->operands[0]) == virtual_register &&
                    machine_ref_kind(instruction->operands[1]) == MACHINE_REF_IMMEDIATE &&
                    machine_ref_payload(instruction->operands[1]) == edit->subject;
            }
        }
        bool writes_own = (edit->kind == MACHINE_EDIT_RELOAD && own) || rematerializes_own;
        if (selected_register == (s32)edit->location && !writes_own)
        {
            *selected_invalid = true;
        }
        if (state->physical_register == (s32)edit->location && !writes_own)
        {
            state->physical_register = -1;
        }
        if (writes_own)
        {
            state->physical_register = (s32)edit->location;
            state->prefer_frame = false;
            state->epoch += 1;
        }
        else if (edit->kind == MACHINE_EDIT_COPY && state->physical_register == (s32)edit->subject)
        {
            state->physical_register = (s32)edit->location;
            state->prefer_frame = false;
            state->epoch += 1;
        }
    }
}

// MIR debug-location recording. Recording turns MIR debug values into native
// location ranges. The work is event-driven: one pass over the finished
// function builds the indexes below, and each referenced virtual register is
// then replayed only at the rows that can change the location it is tracking.
// Between those rows the sampled location is constant, so a replay produces a
// change-point timeline instead of a row-sized array, and emission clips those
// timelines to each value's span.
//
// Map:
//   codegen_machine_debug_groups_build       u32 key -> ascending rows
//   codegen_machine_debug_index_build        per-function row/edit/mark facts
//   codegen_machine_debug_span               one value's span -> MIR row range
//   codegen_machine_debug_reference_timeline one reference's change points
//   codegen_record_machine_locations         clip timelines, emit seeds
//
// Physical register identities fit the allocator's u64 clobber masks, so the
// register index only has to span that width. The one extra bucket collects
// every identity outside it -- an edit location outside the architectural
// file, and the -1 a frame-selected sample compares against -- so a malformed
// edit stream still reaches the same answer as a whole-function replay instead
// of silently skipping the row that carries it.
#define CODEGEN_MACHINE_DEBUG_PHYSICAL_LIMIT 64u
BUSTER_CT_CHECK(MACHINE_X64_REGISTER_COUNT <= CODEGEN_MACHINE_DEBUG_PHYSICAL_LIMIT);
BUSTER_CT_CHECK(MACHINE_A64_REGISTER_COUNT <= CODEGEN_MACHINE_DEBUG_PHYSICAL_LIMIT);
// The predicate bank numbers above the unified file and reaches recording
// through the merged edit stream. Keeping it inside the range gives it real
// buckets; past the range it would still be correct through the extra bucket,
// but every predicate edit would become an event for every tracked register.
BUSTER_CT_CHECK(MACHINE_PREDICATE_REGISTER_BASE + MACHINE_PREDICATE_REGISTER_COUNT <= CODEGEN_MACHINE_DEBUG_PHYSICAL_LIMIT);

typedef struct CodegenMachineDebugRowGroups CodegenMachineDebugRowGroups;
struct CodegenMachineDebugRowGroups
{
    // Open-addressed key -> group, then group -> ascending row range. Keys are
    // frame home offsets and immediate indexes: sparse u32s with no useful
    // dense range, and every lookup is a single exact key.
    u32* slot_keys;
    u32* slot_groups;
    u32* offsets;
    u32* rows;
    u32 slot_mask;
    u32 group_count;
};

typedef struct CodegenMachineDebugIndex CodegenMachineDebugIndex;
struct CodegenMachineDebugIndex
{
    // Rows that can change one virtual register's own tracked state: its
    // operand occurrences and the SPILL/RELOAD edits naming it as subject.
    u32* subject_offsets;
    u32* subject_rows;
    // Rows that write, clobber, or copy out of one physical register.
    u32* physical_offsets;
    u32* physical_rows;
    // Rows whose SPILL edits publish one frame home, keyed by the home offset.
    CodegenMachineDebugRowGroups homes;
    // Rows whose REMATERIALIZE edits name one immediate subject.
    CodegenMachineDebugRowGroups remats;
    // First edit at each row; the last entry is one past every edit.
    u32* row_edits;
    // Line marks in IR-instruction order, with the smallest and largest mark
    // index over every suffix of that order.
    u32* mark_order;
    u32* mark_suffix_first;
    u32* mark_suffix_last;
    bool edits_valid;
    bool marks_valid;
    // Strictly ascending block starts. A replay skipping a run of block starts
    // that cannot change its state then jumps the cursor instead of stepping.
    bool blocks_ascending;
    u8 reserved;
};

typedef enum CodegenMachineDebugSelectionKind
{
    CODEGEN_MACHINE_DEBUG_SELECTION_NONE,
    CODEGEN_MACHINE_DEBUG_SELECTION_FRAME,
    CODEGEN_MACHINE_DEBUG_SELECTION_REGISTER,
} CodegenMachineDebugSelectionKind;

// One change point: the sampled location from `row` until the next entry's
// row. The physical register is kept unmapped because the architectural
// mapping depends on the piece size the value asks for, not on the reference.
typedef struct CodegenMachineDebugSelection CodegenMachineDebugSelection;
struct CodegenMachineDebugSelection
{
    u32 row;
    s32 frame_offset;
    s32 physical_register;
    u8 kind;
    u8 reserved[3];
};

typedef struct CodegenMachineDebugTimeline CodegenMachineDebugTimeline;
struct CodegenMachineDebugTimeline
{
    CodegenMachineDebugSelection const* entries;
    u32 entry_count;
    bool built;
    bool valid;
    u8 reserved[2];
};

BUSTER_GLOBAL_LOCAL u32 codegen_machine_debug_group_slot(u32 key, u32 slot_mask)
{
    // Fibonacci-ratio multiply-shift. Home offsets are frame-size multiples,
    // so their low bits alone collide in every slot table.
    return ((key * UINT32_C(2654435761)) >> 8) & slot_mask;
}

BUSTER_GLOBAL_LOCAL u32 codegen_machine_debug_group_find(CodegenMachineDebugRowGroups const* groups, u32 key)
{
    u32 slot = codegen_machine_debug_group_slot(key, groups->slot_mask);
    u32 group = groups->slot_groups[slot];
    while (group != UINT32_MAX && groups->slot_keys[slot] != key)
    {
        slot = (slot + 1u) & groups->slot_mask;
        group = groups->slot_groups[slot];
    }
    return group;
}

// `keys` and `rows` are parallel and `rows` ascends, so filling each group in
// input order leaves every group's rows ascending for the replay's cursors.
BUSTER_GLOBAL_LOCAL void codegen_machine_debug_groups_build(Arena* arena, CodegenMachineDebugRowGroups* groups, u32 const* keys, u32 const* rows,
                                                             u32 count)
{
    u32 slot_count = 8u;
    while (slot_count / 2u < count && slot_count < (1u << 30))
    {
        slot_count *= 2u;
    }
    groups->slot_mask = slot_count - 1u;
    groups->slot_keys = arena_allocate(arena, u32, slot_count);
    groups->slot_groups = arena_allocate(arena, u32, slot_count);
    memset(groups->slot_groups, 0xff, sizeof(u32) * (u64)slot_count);
    u32* entry_groups = arena_allocate(arena, u32, count ? count : 1u);
    u32* group_cursors = arena_allocate(arena, u32, count ? count : 1u);
    groups->group_count = 0;
    for (u32 entry = 0; entry < count; entry += 1)
    {
        u32 key = keys[entry];
        u32 slot = codegen_machine_debug_group_slot(key, groups->slot_mask);
        while (groups->slot_groups[slot] != UINT32_MAX && groups->slot_keys[slot] != key)
        {
            slot = (slot + 1u) & groups->slot_mask;
        }
        if (groups->slot_groups[slot] == UINT32_MAX)
        {
            groups->slot_keys[slot] = key;
            groups->slot_groups[slot] = groups->group_count;
            group_cursors[groups->group_count] = 0;
            groups->group_count += 1;
        }
        entry_groups[entry] = groups->slot_groups[slot];
        group_cursors[entry_groups[entry]] += 1;
    }
    groups->offsets = arena_allocate(arena, u32, (u64)groups->group_count + 1u);
    u32 cursor = 0;
    for (u32 group = 0; group < groups->group_count; group += 1)
    {
        u32 group_count = group_cursors[group];
        groups->offsets[group] = cursor;
        group_cursors[group] = cursor;
        cursor += group_count;
    }
    groups->offsets[groups->group_count] = cursor;
    groups->rows = arena_allocate(arena, u32, count ? count : 1u);
    for (u32 entry = 0; entry < count; entry += 1)
    {
        u32 group = entry_groups[entry];
        groups->rows[group_cursors[group]] = rows[entry];
        group_cursors[group] += 1;
    }
}

BUSTER_GLOBAL_LOCAL u32 codegen_machine_debug_physical_bucket(s32 physical_register)
{
    return physical_register >= 0 && (u32)physical_register < CODEGEN_MACHINE_DEBUG_PHYSICAL_LIMIT ? (u32)physical_register
                                                                                                   : CODEGEN_MACHINE_DEBUG_PHYSICAL_LIMIT;
}

typedef struct CodegenMachineDebugIndexEvent CodegenMachineDebugIndexEvent;
struct CodegenMachineDebugIndexEvent
{
    u32 bucket;
    u32 row;
};

BUSTER_CT_CHECK(CODEGEN_MACHINE_DEBUG_PHYSICAL_LIMIT == 64u);

BUSTER_GLOBAL_LOCAL void codegen_machine_debug_index_event_append(Arena* arena, MachineBuilderStream* events, u32* counts, u32 bucket, u32 row)
{
    CodegenMachineDebugIndexEvent* event = (CodegenMachineDebugIndexEvent*)machine_stream_append(arena, events);
    *event = (CodegenMachineDebugIndexEvent){.bucket = bucket, .row = row};
    counts[bucket] += 1u;
}

BUSTER_GLOBAL_LOCAL void codegen_machine_debug_index_subject_event(Arena* arena, MachineBuilderStream* events, u32* counts, u32* last_rows,
                                                                    u32 subject, u32 row)
{
    if (last_rows[subject] != row)
    {
        last_rows[subject] = row;
        codegen_machine_debug_index_event_append(arena, events, counts, subject, row);
    }
}

BUSTER_GLOBAL_LOCAL void codegen_machine_debug_index_physical_event(Arena* arena, MachineBuilderStream* events, u32* counts,
                                                                     u64* seen, bool* unmapped_seen, s32 physical_register, u32 row)
{
    u32 bucket = codegen_machine_debug_physical_bucket(physical_register);
    bool append = false;
    if (bucket < 64u)
    {
        u64 bit = UINT64_C(1) << bucket;
        append = (*seen & bit) == 0;
        *seen |= bit;
    }
    else
    {
        append = !*unmapped_seen;
        *unmapped_seen = true;
    }
    if (append)
    {
        codegen_machine_debug_index_event_append(arena, events, counts, bucket, row);
    }
}

// Decode each MIR row once. Compact events retain source order, so a stable
// bucket scatter below produces the same ascending row lists as the old
// count-and-fill pair of whole-function scans. A row appears only once in a
// subject or physical bucket: replay processes the complete row and all of its
// edits at that stop, so duplicate stops carried no information.
BUSTER_GLOBAL_LOCAL void codegen_machine_debug_index_collect(Arena* arena, MachineFunction const* function,
                                                              MachineStackPlacement const* placement,
                                                              CodegenMachineDebugIndex const* index, u8 const* referenced,
                                                              u32* subject_last_rows, u32* subject_counts, u32* physical_counts,
                                                              MachineBuilderStream* subject_events, MachineBuilderStream* physical_events)
{
    for (u32 row = 0; row < function->instruction_count; row += 1)
    {
        u64 physical_seen = 0;
        bool unmapped_seen = false;
        MachineInstruction const* instruction = function->instructions + row;
        MachineOpcodeRow opcode_row = machine_instruction_opcode_row(function, instruction);
        u64 clobbers = opcode_row.clobber_mask;
        while (clobbers)
        {
            u32 physical = trailing_zeroes_u64(clobbers);
            clobbers &= clobbers - 1u;
            codegen_machine_debug_index_physical_event(arena, physical_events, physical_counts, &physical_seen, &unmapped_seen,
                                                        (s32)physical, row);
        }
        MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
        for (u32 operand_index = 0; info && operand_index < info->operand_count; operand_index += 1)
        {
            u32 role = info->operand_info[operand_index] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
            MachineRef operand = instruction->operands[operand_index];
            if (role != MACHINE_OPERAND_ROLE_NONE && machine_ref_kind(operand) == MACHINE_REF_VIRTUAL_REGISTER &&
                machine_ref_payload(operand) < function->virtual_register_count && referenced[machine_ref_payload(operand)])
            {
                codegen_machine_debug_index_subject_event(arena, subject_events, subject_counts, subject_last_rows,
                                                           machine_ref_payload(operand), row);
            }
            if (role == MACHINE_OPERAND_ROLE_DEFINE || role == MACHINE_OPERAND_ROLE_USE_DEFINE)
            {
                u32 physical = placement->operand_registers[(u64)row * MACHINE_INSTRUCTION_OPERAND_COUNT + operand_index];
                codegen_machine_debug_index_physical_event(arena, physical_events, physical_counts, &physical_seen, &unmapped_seen,
                                                            (s32)physical, row);
            }
        }
        for (u32 edit_index = index->row_edits[row]; edit_index < index->row_edits[row + 1u]; edit_index += 1)
        {
            MachineEdit const* edit = placement->edits + edit_index;
            if ((edit->kind == MACHINE_EDIT_SPILL || edit->kind == MACHINE_EDIT_RELOAD) && edit->subject < function->virtual_register_count &&
                referenced[edit->subject])
            {
                codegen_machine_debug_index_subject_event(arena, subject_events, subject_counts, subject_last_rows, edit->subject, row);
            }
            if (codegen_machine_debug_edit_writes_register(edit))
            {
                codegen_machine_debug_index_physical_event(arena, physical_events, physical_counts, &physical_seen, &unmapped_seen,
                                                            (s32)edit->location, row);
            }
            if (edit->kind == MACHINE_EDIT_COPY)
            {
                codegen_machine_debug_index_physical_event(arena, physical_events, physical_counts, &physical_seen, &unmapped_seen,
                                                            (s32)edit->subject, row);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void codegen_machine_debug_index_scatter(MachineBuilderStream const* events, u32* cursors, u32* rows)
{
    for (MachineBuilderChunk const* chunk = events->first; chunk; chunk = chunk->next)
    {
        CodegenMachineDebugIndexEvent const* entries = (CodegenMachineDebugIndexEvent const*)(chunk + 1);
        for (u32 entry = 0; entry < chunk->count; entry += 1)
        {
            CodegenMachineDebugIndexEvent event = entries[entry];
            rows[cursors[event.bucket]++] = event.row;
        }
    }
}

BUSTER_GLOBAL_LOCAL void codegen_machine_debug_index_build(Arena* arena, MachineFunction const* function, MachineStackPlacement const* placement,
                                                            CodegenMachineDebugIndex* index)
{
    u32 row_count = function->instruction_count;
    u32 edit_count = placement->edit_count;
    u32 spill_edits = 0;
    u32 remat_edits = 0;
    // A whole-function replay consumes every edit as the BEFORE or AFTER part
    // of the row it walks, and rejects the stream when any edit is left over.
    // The same fact is a sorted, in-range, two-phase check made once here.
    index->edits_valid = true;
    MachinePoint previous_point = 0;
    for (u32 edit_index = 0; edit_index < edit_count; edit_index += 1)
    {
        MachineEdit const* edit = placement->edits + edit_index;
        MachinePoint point = edit->point;
        spill_edits += edit->kind == MACHINE_EDIT_SPILL;
        remat_edits += edit->kind == MACHINE_EDIT_REMATERIALIZE;
        MachinePointPhase phase = machine_point_phase(point);
        index->edits_valid = index->edits_valid && (phase == MACHINE_POINT_BEFORE || phase == MACHINE_POINT_AFTER) &&
                             machine_point_instruction(point) < row_count && point >= previous_point;
        previous_point = point;
    }
    index->row_edits = arena_allocate(arena, u32, (u64)row_count + 1u);
    u32 edit_cursor = 0;
    for (u32 row = 0; row < row_count; row += 1)
    {
        while (edit_cursor < edit_count && machine_point_instruction(placement->edits[edit_cursor].point) < row)
        {
            edit_cursor += 1;
        }
        index->row_edits[row] = edit_cursor;
    }
    index->row_edits[row_count] = index->edits_valid ? edit_count : edit_cursor;
    // Only virtual registers a debug value names are ever replayed. Indexing
    // the rest would scatter several writes per row across the whole register
    // file for lists nothing reads.
    TemporalArena event_scratch = scratch_begin(&arena, 1);
    u8* referenced = arena_allocate(event_scratch.arena, u8, function->virtual_register_count ? function->virtual_register_count : 1u);
    memset(referenced, 0, sizeof(u8) * (u64)(function->virtual_register_count ? function->virtual_register_count : 1u));
    u32 referenced_count = 0;
    for (u32 value_index = 0; value_index < function->debug_value_count; value_index += 1)
    {
        MachineDebugValue const* value = function->debug_values + value_index;
        for (u32 piece_index = 0; piece_index < BUSTER_MIN(value->piece_count, (u8)BUSTER_ARRAY_LENGTH(value->pieces)); piece_index += 1)
        {
            MachineRef piece = value->pieces[piece_index];
            u32 payload = machine_ref_payload(piece);
            if (machine_ref_kind(piece) == MACHINE_REF_VIRTUAL_REGISTER && payload < function->virtual_register_count && !referenced[payload])
            {
                referenced[payload] = 1u;
                referenced_count += 1u;
            }
        }
    }
    u32 subject_bucket_count = function->virtual_register_count ? function->virtual_register_count : 1u;
    u32 physical_bucket_count = CODEGEN_MACHINE_DEBUG_PHYSICAL_LIMIT + 1u;
    index->subject_offsets = arena_allocate(arena, u32, (u64)subject_bucket_count + 1u);
    index->physical_offsets = arena_allocate(arena, u32, (u64)physical_bucket_count + 1u);
    u32* subject_cursors = arena_allocate(event_scratch.arena, u32, subject_bucket_count);
    u32* physical_cursors = arena_allocate(event_scratch.arena, u32, physical_bucket_count);
    memset(subject_cursors, 0, sizeof(u32) * (u64)subject_bucket_count);
    memset(physical_cursors, 0, sizeof(u32) * (u64)physical_bucket_count);
    MachineBuilderStream subject_events;
    MachineBuilderStream physical_events;
    machine_stream_initialize(&subject_events, sizeof(CodegenMachineDebugIndexEvent));
    machine_stream_initialize(&physical_events, sizeof(CodegenMachineDebugIndexEvent));
    if (referenced_count)
    {
        u32* subject_last_rows = arena_allocate(event_scratch.arena, u32, subject_bucket_count);
        memset(subject_last_rows, 0xff, sizeof(*subject_last_rows) * (u64)subject_bucket_count);
        codegen_machine_debug_index_collect(event_scratch.arena, function, placement, index, referenced, subject_last_rows, subject_cursors,
                                            physical_cursors, &subject_events, &physical_events);
    }
    u32 subject_total = 0;
    for (u32 bucket = 0; bucket < subject_bucket_count; bucket += 1)
    {
        u32 bucket_count = subject_cursors[bucket];
        index->subject_offsets[bucket] = subject_total;
        subject_cursors[bucket] = subject_total;
        subject_total += bucket_count;
    }
    index->subject_offsets[subject_bucket_count] = subject_total;
    u32 physical_total = 0;
    for (u32 bucket = 0; bucket < physical_bucket_count; bucket += 1)
    {
        u32 bucket_count = physical_cursors[bucket];
        index->physical_offsets[bucket] = physical_total;
        physical_cursors[bucket] = physical_total;
        physical_total += bucket_count;
    }
    index->physical_offsets[physical_bucket_count] = physical_total;
    index->subject_rows = arena_allocate(arena, u32, subject_total ? subject_total : 1u);
    index->physical_rows = arena_allocate(arena, u32, physical_total ? physical_total : 1u);
    codegen_machine_debug_index_scatter(&subject_events, subject_cursors, index->subject_rows);
    codegen_machine_debug_index_scatter(&physical_events, physical_cursors, index->physical_rows);
    scratch_end(event_scratch);
    u32* home_keys = arena_allocate(arena, u32, spill_edits ? spill_edits : 1u);
    u32* home_rows = arena_allocate(arena, u32, spill_edits ? spill_edits : 1u);
    u32* remat_keys = arena_allocate(arena, u32, remat_edits ? remat_edits : 1u);
    u32* remat_rows = arena_allocate(arena, u32, remat_edits ? remat_edits : 1u);
    u32 home_count = 0;
    u32 remat_count = 0;
    for (u32 edit_index = 0; edit_index < edit_count; edit_index += 1)
    {
        MachineEdit const* edit = placement->edits + edit_index;
        u32 row = machine_point_instruction(edit->point);
        if (edit->kind == MACHINE_EDIT_SPILL)
        {
            home_keys[home_count] = edit->subject < function->virtual_register_count ? placement->virtual_register_offsets[edit->subject]
                                                                                    : UINT32_MAX;
            home_rows[home_count] = row;
            home_count += 1;
        }
        else if (edit->kind == MACHINE_EDIT_REMATERIALIZE)
        {
            remat_keys[remat_count] = edit->subject;
            remat_rows[remat_count] = row;
            remat_count += 1;
        }
    }
    codegen_machine_debug_groups_build(arena, &index->homes, home_keys, home_rows, home_count);
    codegen_machine_debug_groups_build(arena, &index->remats, remat_keys, remat_rows, remat_count);
    index->blocks_ascending = true;
    for (u32 block_index = 1; block_index < function->block_count; block_index += 1)
    {
        index->blocks_ascending = index->blocks_ascending &&
                                  function->blocks[block_index - 1u].first_instruction < function->blocks[block_index].first_instruction;
    }
    index->marks_valid = true;
    for (u32 mark_index = 0; mark_index < function->line_mark_count; mark_index += 1)
    {
        MachineLineMark mark = function->line_marks[mark_index];
        index->marks_valid = index->marks_valid && mark.row <= row_count &&
                             (!mark_index || mark.row >= function->line_marks[mark_index - 1u].row);
    }
    u32 mark_count = function->line_mark_count;
    index->mark_order = arena_allocate(arena, u32, mark_count ? mark_count : 1u);
    u32* mark_scratch = arena_allocate(arena, u32, mark_count ? mark_count : 1u);
    for (u32 mark_index = 0; mark_index < mark_count; mark_index += 1)
    {
        index->mark_order[mark_index] = mark_index;
    }
    u32 mark_instruction_limit = 0;
    for (u32 mark_index = 0; mark_index < mark_count; mark_index += 1)
    {
        mark_instruction_limit = BUSTER_MAX(mark_instruction_limit, function->line_marks[mark_index].instruction);
    }
    // Least-significant-digit radix sort on the IR instruction. It is stable,
    // so marks sharing an instruction keep ascending mark order, and it never
    // allocates against the key range the way a counting sort would. Digits
    // above the largest instruction cannot reorder anything, and a function
    // small enough to index in one or two bytes is the common case.
    u32 mark_shift_end = 8u;
    while (mark_shift_end < 32u && (mark_instruction_limit >> mark_shift_end))
    {
        mark_shift_end += 8u;
    }
    for (u32 shift = 0; mark_count && shift < mark_shift_end; shift += 8u)
    {
        u32 digit_counts[256] = {0};
        for (u32 order_index = 0; order_index < mark_count; order_index += 1)
        {
            digit_counts[(function->line_marks[index->mark_order[order_index]].instruction >> shift) & 0xffu] += 1u;
        }
        u32 digit_cursor = 0;
        for (u32 digit = 0; digit < BUSTER_ARRAY_LENGTH(digit_counts); digit += 1)
        {
            u32 digit_count = digit_counts[digit];
            digit_counts[digit] = digit_cursor;
            digit_cursor += digit_count;
        }
        for (u32 order_index = 0; order_index < mark_count; order_index += 1)
        {
            u32 mark_index = index->mark_order[order_index];
            u32 digit = (function->line_marks[mark_index].instruction >> shift) & 0xffu;
            mark_scratch[digit_counts[digit]] = mark_index;
            digit_counts[digit] += 1u;
        }
        memcpy(index->mark_order, mark_scratch, sizeof(u32) * (u64)mark_count);
    }
    index->mark_suffix_first = arena_allocate(arena, u32, (u64)mark_count + 1u);
    index->mark_suffix_last = arena_allocate(arena, u32, (u64)mark_count + 1u);
    index->mark_suffix_first[mark_count] = UINT32_MAX;
    index->mark_suffix_last[mark_count] = 0;
    for (u32 order_index = mark_count; order_index; order_index -= 1)
    {
        index->mark_suffix_first[order_index - 1u] = BUSTER_MIN(index->mark_order[order_index - 1u], index->mark_suffix_first[order_index]);
        index->mark_suffix_last[order_index - 1u] = BUSTER_MAX(index->mark_order[order_index - 1u], index->mark_suffix_last[order_index]);
    }
}

BUSTER_GLOBAL_LOCAL u32 codegen_machine_debug_mark_lower_bound(MachineFunction const* function, CodegenMachineDebugIndex const* index,
                                                                u32 instruction)
{
    u32 low = 0;
    u32 high = function->line_mark_count;
    while (low < high)
    {
        u32 middle = low + (high - low) / 2u;
        if (function->line_marks[index->mark_order[middle]].instruction < instruction)
        {
            low = middle + 1u;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

// The value's IR instruction span, resolved to the MIR row range the line
// marks place it in. Definition-to-end spans reach the end of the mark order
// and answer from the suffix summaries; a block range answers from its own
// slice of that order, which holds only the marks inside the block.
BUSTER_GLOBAL_LOCAL bool codegen_machine_debug_span(MachineFunction const* function, CodegenMachineDebugIndex const* index,
                                                     MachineDebugValue const* value, u32* first_row, u32* end_row, bool* malformed)
{
    bool result = value->first_instruction == UINT32_MAX;
    *first_row = 0;
    *end_row = function->instruction_count;
    if (!result)
    {
        bool rejected = value->first_instruction > UINT32_MAX - value->instruction_count || !index->marks_valid;
        u32 instruction_end = rejected ? 0 : value->first_instruction + value->instruction_count;
        u32 low = rejected ? 0 : codegen_machine_debug_mark_lower_bound(function, index, value->first_instruction);
        u32 high = rejected ? 0 : codegen_machine_debug_mark_lower_bound(function, index, instruction_end);
        if (!rejected && low < high)
        {
            u32 first_mark = index->mark_suffix_first[low];
            u32 last_mark = index->mark_suffix_last[low];
            if (high < function->line_mark_count)
            {
                first_mark = UINT32_MAX;
                last_mark = 0;
                for (u32 order_index = low; order_index < high; order_index += 1)
                {
                    first_mark = BUSTER_MIN(first_mark, index->mark_order[order_index]);
                    last_mark = BUSTER_MAX(last_mark, index->mark_order[order_index]);
                }
            }
            *first_row = function->line_marks[first_mark].row;
            *end_row = last_mark + 1u < function->line_mark_count ? function->line_marks[last_mark + 1u].row : function->instruction_count;
            result = *first_row < *end_row;
        }
        else
        {
            *malformed = true;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CodegenMachineDebugSelection codegen_machine_debug_sample(CodegenMachineDebugReference const* state, u32 row, s32 frame_offset,
                                                                              bool has_home)
{
    CodegenMachineDebugSelection result = {.row = row, .physical_register = -1, .kind = CODEGEN_MACHINE_DEBUG_SELECTION_NONE};
    bool selected_frame = state->frame_valid && (state->prefer_frame || state->physical_register < 0);
    if (selected_frame && has_home)
    {
        result.kind = CODEGEN_MACHINE_DEBUG_SELECTION_FRAME;
        result.frame_offset = frame_offset;
    }
    else if (!selected_frame && state->physical_register >= 0)
    {
        result.kind = CODEGEN_MACHINE_DEBUG_SELECTION_REGISTER;
        result.physical_register = state->physical_register;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void codegen_machine_debug_selection_push(CodegenMachineDebugSelection* entries, u32* entry_count, u32 capacity,
                                                               CodegenMachineDebugSelection selection, u32 row_count)
{
    // A sample taken at a row supersedes the steady state the row before it
    // published for that same row, so the superseded entry goes before the
    // comparison rather than after it. Comparing against an entry that is
    // about to be overwritten answers for the wrong neighbour: a row whose
    // sample differs from its own steady state then keeps one entry per row,
    // all carrying the same location, and every value that names the register
    // walks them. That is the whole function again, once per value.
    u32 count = *entry_count && entries[*entry_count - 1u].row == selection.row ? *entry_count - 1u : *entry_count;
    bool same = count && entries[count - 1u].kind == selection.kind && entries[count - 1u].frame_offset == selection.frame_offset &&
                entries[count - 1u].physical_register == selection.physical_register;
    if (selection.row < row_count && count < capacity)
    {
        if (!same)
        {
            entries[count] = selection;
            count += 1u;
        }
        *entry_count = count;
    }
}

// The first row at or after `row` that writes the register, with a one-entry
// cursor cache: a replay usually asks about the same register at successive
// rows, and walking forward from the last answer beats searching again.
BUSTER_GLOBAL_LOCAL u32 codegen_machine_debug_physical_next(CodegenMachineDebugIndex const* index, s32 physical_register, u32 row,
                                                             u32* cached_bucket, u32* cached_cursor)
{
    u32 bucket = codegen_machine_debug_physical_bucket(physical_register);
    u32 end = index->physical_offsets[bucket + 1u];
    u32 low = index->physical_offsets[bucket];
    if (bucket == *cached_bucket && *cached_cursor >= low && *cached_cursor <= end)
    {
        low = *cached_cursor;
        while (low < end && index->physical_rows[low] < row)
        {
            low += 1u;
        }
    }
    else
    {
        u32 high = end;
        while (low < high)
        {
            u32 middle = low + (high - low) / 2u;
            if (index->physical_rows[middle] < row)
            {
                low = middle + 1u;
            }
            else
            {
                high = middle;
            }
        }
    }
    *cached_bucket = bucket;
    *cached_cursor = low;
    return low < end ? index->physical_rows[low] : UINT32_MAX;
}

// The row the block cursor can still match, or none once it has fallen behind:
// a whole-function replay consumes one block per row, so a block start the
// walk has passed never matches again.
BUSTER_GLOBAL_LOCAL u32 codegen_machine_debug_block_row(MachineFunction const* function, u32 block_cursor, u32 row)
{
    return block_cursor < function->block_count && function->blocks[block_cursor].first_instruction >= row
               ? function->blocks[block_cursor].first_instruction
               : UINT32_MAX;
}

BUSTER_GLOBAL_LOCAL u32 codegen_machine_debug_block_lower_bound(MachineFunction const* function, u32 row)
{
    u32 low = 0;
    u32 high = function->block_count;
    while (low < high)
    {
        u32 middle = low + (high - low) / 2u;
        if (function->blocks[middle].first_instruction < row)
        {
            low = middle + 1u;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

BUSTER_GLOBAL_LOCAL u32 codegen_machine_debug_group_next(CodegenMachineDebugRowGroups const* groups, u32 group, u32* cursor, u32 row)
{
    u32 next = UINT32_MAX;
    if (group != UINT32_MAX)
    {
        u32 end = groups->offsets[group + 1u];
        while (*cursor < end && groups->rows[*cursor] < row)
        {
            *cursor += 1u;
        }
        next = *cursor < end ? groups->rows[*cursor] : UINT32_MAX;
    }
    return next;
}

// One virtual register's sampled location over the whole function, as change
// points. The row body below is the whole-function replay's, run only at rows
// the indexes say can change this register's state; between them the row-start
// sample is unchanged and no invalidation can happen, so the location holds.
BUSTER_GLOBAL_LOCAL bool codegen_machine_debug_reference_timeline(MachineFunction const* function, MachineStackPlacement const* placement,
                                                                   CodegenMachineDebugIndex const* index, u32 payload, u32 frame_base_offset,
                                                                   Target target, CodegenMachineDebugSelection* entries, u32 capacity,
                                                                   u32* entry_count)
{
    s32 frame_offset = 0;
    // A homeless register is a value without a frame location, not invalid IR:
    // a sample that would select its frame copy stays unavailable instead. The
    // dense reference clips the same way. An offset that really is out of
    // range is still rejected.
    u32 home = payload < function->virtual_register_count ? placement->virtual_register_offsets[payload] : MACHINE_VIRTUAL_REGISTER_NO_HOME;
    bool has_home = home != MACHINE_VIRTUAL_REGISTER_NO_HOME;
    bool result = payload < function->virtual_register_count && index->edits_valid &&
                  (!has_home || codegen_machine_debug_frame_offset(home, frame_base_offset, target, &frame_offset));
    *entry_count = 0;
    if (result)
    {
        u32 remat_group = UINT32_MAX;
        MachinePoint definition = function->virtual_registers[payload].definition_point;
        u32 definition_row = definition == MACHINE_POINT_INVALID ? UINT32_MAX : machine_point_instruction(definition);
        if (definition_row < function->instruction_count)
        {
            MachineInstruction const* instruction = function->instructions + definition_row;
            MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
            if (info && info->operand_count >= 2 && machine_ref_kind(instruction->operands[0]) == MACHINE_REF_VIRTUAL_REGISTER &&
                machine_ref_payload(instruction->operands[0]) == payload &&
                machine_ref_kind(instruction->operands[1]) == MACHINE_REF_IMMEDIATE)
            {
                remat_group = codegen_machine_debug_group_find(&index->remats, machine_ref_payload(instruction->operands[1]));
            }
        }
        // Every homeless register carries the marker as its key, and a spill of
        // one of the others never invalidates this value, so there is no row
        // here worth stopping at.
        u32 home_group = has_home ? codegen_machine_debug_group_find(&index->homes, home) : UINT32_MAX;
        u32 home_cursor = home_group == UINT32_MAX ? 0 : index->homes.offsets[home_group];
        u32 remat_cursor = remat_group == UINT32_MAX ? 0 : index->remats.offsets[remat_group];
        u32 subject_cursor = index->subject_offsets[payload];
        u32 subject_end = index->subject_offsets[payload + 1u];
        CodegenMachineDebugReference state = {.physical_register = -1};
        u32 physical_bucket = UINT32_MAX;
        u32 physical_cursor = 0;
        u32 unmapped_cursor = index->physical_offsets[CODEGEN_MACHINE_DEBUG_PHYSICAL_LIMIT];
        u32 block_cursor = 0;
        u32 row = 0;
        codegen_machine_debug_selection_push(entries, entry_count, capacity, codegen_machine_debug_sample(&state, 0, frame_offset, has_home),
                                             function->instruction_count);
        while (row < function->instruction_count)
        {
            while (subject_cursor < subject_end && index->subject_rows[subject_cursor] < row)
            {
                subject_cursor += 1u;
            }
            u32 next = subject_cursor < subject_end ? index->subject_rows[subject_cursor] : UINT32_MAX;
            next = BUSTER_MIN(next, codegen_machine_debug_group_next(&index->remats, remat_group, &remat_cursor, row));
            if (state.frame_valid)
            {
                next = BUSTER_MIN(next, codegen_machine_debug_group_next(&index->homes, home_group, &home_cursor, row));
            }
            next = BUSTER_MIN(next, codegen_machine_debug_physical_next(index, state.physical_register, row, &physical_bucket, &physical_cursor));
            if (unmapped_cursor < index->physical_offsets[CODEGEN_MACHINE_DEBUG_PHYSICAL_LIMIT + 1u])
            {
                // Identities outside the architectural file share one bucket,
                // which a frame-selected sample also compares against. It is
                // empty for everything an allocator emits.
                while (unmapped_cursor < index->physical_offsets[CODEGEN_MACHINE_DEBUG_PHYSICAL_LIMIT + 1u] &&
                       index->physical_rows[unmapped_cursor] < row)
                {
                    unmapped_cursor += 1u;
                }
                next = BUSTER_MIN(next, unmapped_cursor < index->physical_offsets[CODEGEN_MACHINE_DEBUG_PHYSICAL_LIMIT + 1u]
                                            ? index->physical_rows[unmapped_cursor]
                                            : UINT32_MAX);
            }
            // A block start clears any held register and changes nothing
            // else. The first block changes nothing at all -- the replay this
            // stands in for skips it -- and neither does any other while
            // nothing is held. Skip those without stopping: the cursor still
            // advances, because the replay consumes exactly one block per row.
            u32 block_row = codegen_machine_debug_block_row(function, block_cursor, row);
            if (!block_cursor && block_row < next)
            {
                row = block_row + 1u;
                block_cursor += 1u;
                block_row = codegen_machine_debug_block_row(function, block_cursor, row);
            }
            if (block_row < next && state.physical_register < 0)
            {
                if (index->blocks_ascending)
                {
                    block_cursor = codegen_machine_debug_block_lower_bound(function, next);
                    row = BUSTER_MIN(next, function->instruction_count);
                }
                else
                {
                    while (block_row < next)
                    {
                        row = block_row + 1u;
                        block_cursor += 1u;
                        block_row = codegen_machine_debug_block_row(function, block_cursor, row);
                    }
                }
                block_row = codegen_machine_debug_block_row(function, block_cursor, row);
            }
            {
                next = BUSTER_MIN(next, block_row);
                if (next >= function->instruction_count)
                {
                    row = function->instruction_count;
                }
                else
                {
                    if (block_cursor < function->block_count && function->blocks[block_cursor].first_instruction == next)
                    {
                        if (block_cursor)
                        {
                            // Physical ownership is edge-specific. A published
                            // home is path-independent: every executing
                            // definition spilled the same vreg there, and reuse
                            // writes invalidate it explicitly. The home
                            // preference needs no republication beside it: with
                            // no register held it does not enter the selection,
                            // and every transition back to holding one rewrites
                            // it.
                            state.physical_register = -1;
                        }
                        block_cursor += 1u;
                    }
                    bool selected_frame = state.frame_valid && (state.prefer_frame || state.physical_register < 0);
                    s32 selected_register = selected_frame ? -1 : state.physical_register;
                    bool selected_invalid = false;
                    u32 edit_cursor = index->row_edits[next];
                    MachinePoint before = machine_point_make(next, MACHINE_POINT_BEFORE);
                    while (edit_cursor < placement->edit_count && placement->edits[edit_cursor].point == before)
                    {
                        codegen_machine_debug_edit_state(function, placement, payload, placement->edits + edit_cursor, &state, selected_frame,
                                                         selected_register, &selected_invalid);
                        edit_cursor += 1u;
                    }
                    MachineInstruction const* instruction = function->instructions + next;
                    MachineOpcodeRow opcode_row = machine_instruction_opcode_row(function, instruction);
                    if (selected_register >= 0 && selected_register < 64 && (opcode_row.clobber_mask & (UINT64_C(1) << selected_register)))
                    {
                        selected_invalid = true;
                    }
                    if (state.physical_register >= 0 && state.physical_register < 64 &&
                        (opcode_row.clobber_mask & (UINT64_C(1) << state.physical_register)))
                    {
                        state.physical_register = -1;
                    }
                    MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
                    for (u32 operand_index = 0; info && operand_index < info->operand_count; operand_index += 1)
                    {
                        u32 role = info->operand_info[operand_index] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
                        MachineRef operand = instruction->operands[operand_index];
                        bool own = machine_ref_kind(operand) == MACHINE_REF_VIRTUAL_REGISTER && machine_ref_payload(operand) == payload;
                        u32 physical = placement->operand_registers[(u64)next * MACHINE_INSTRUCTION_OPERAND_COUNT + operand_index];
                        if ((role == MACHINE_OPERAND_ROLE_USE || role == MACHINE_OPERAND_ROLE_USE_DEFINE) && own && state.physical_register < 0)
                        {
                            // Entry/CFG parameters intentionally have no
                            // definition row. Their first allocated use is
                            // nevertheless a certified read of the incoming
                            // value; publish it only after that row.
                            state.physical_register = (s32)physical;
                            state.prefer_frame = false;
                            state.epoch += 1;
                        }
                        if (role == MACHINE_OPERAND_ROLE_DEFINE || role == MACHINE_OPERAND_ROLE_USE_DEFINE)
                        {
                            if (selected_register == (s32)physical)
                            {
                                selected_invalid = true;
                            }
                            if (state.physical_register == (s32)physical)
                            {
                                state.physical_register = -1;
                            }
                            if (own)
                            {
                                state.physical_register = (s32)physical;
                                state.prefer_frame = false;
                                state.epoch += 1;
                            }
                        }
                    }
                    MachinePoint after = machine_point_make(next, MACHINE_POINT_AFTER);
                    while (edit_cursor < placement->edit_count && placement->edits[edit_cursor].point == after)
                    {
                        codegen_machine_debug_edit_state(function, placement, payload, placement->edits + edit_cursor, &state, selected_frame,
                                                         selected_register, &selected_invalid);
                        edit_cursor += 1u;
                    }
                    CodegenMachineDebugSelection sampled = {.row = next, .physical_register = -1,
                                                            .kind = CODEGEN_MACHINE_DEBUG_SELECTION_NONE};
                    if (!selected_invalid && selected_frame && has_home)
                    {
                        sampled.kind = CODEGEN_MACHINE_DEBUG_SELECTION_FRAME;
                        sampled.frame_offset = frame_offset;
                    }
                    else if (!selected_invalid && selected_register >= 0)
                    {
                        sampled.kind = CODEGEN_MACHINE_DEBUG_SELECTION_REGISTER;
                        sampled.physical_register = selected_register;
                    }
                    codegen_machine_debug_selection_push(entries, entry_count, capacity, sampled, function->instruction_count);
                    codegen_machine_debug_selection_push(entries, entry_count, capacity,
                                                         codegen_machine_debug_sample(&state, next + 1u, frame_offset, has_home),
                                                         function->instruction_count);
                    row = next + 1u;
                }
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 codegen_machine_debug_timeline_seek(CodegenMachineDebugTimeline const* timeline, u32 row)
{
    u32 low = 0;
    u32 high = timeline->entry_count;
    while (low < high)
    {
        u32 middle = low + (high - low) / 2u;
        if (timeline->entries[middle].row <= row)
        {
            low = middle + 1u;
        }
        else
        {
            high = middle;
        }
    }
    return low ? low - 1u : 0;
}

BUSTER_GLOBAL_LOCAL DebugLocationPiece codegen_machine_debug_piece(MachineFunction const* function, MachineRef reference, u32 value_size,
                                                                    Target target, CodegenMachineDebugSelection selection, bool* available)
{
    DebugLocationPiece piece = {0};
    bool present = false;
    if (selection.kind == CODEGEN_MACHINE_DEBUG_SELECTION_FRAME)
    {
        piece.kind = DEBUG_LOCATION_FRAME;
        piece.frame_offset = selection.frame_offset;
        present = true;
    }
    else if (selection.kind == CODEGEN_MACHINE_DEBUG_SELECTION_REGISTER)
    {
        DebugRegister reg = codegen_machine_debug_register(function, machine_ref_payload(reference), (u32)selection.physical_register, value_size,
                                                           target);
        piece.kind = DEBUG_LOCATION_REGISTER;
        piece.reg = reg;
        present = reg != DEBUG_REGISTER_NONE;
    }
    *available = present;
    return piece;
}

// One reference's timeline, built on first use and reused by every value that
// names the same virtual register. A stack slot has no state to replay: its
// location is the same frame offset for the whole function.
BUSTER_GLOBAL_LOCAL bool codegen_machine_debug_timeline_for(Arena* arena, MachineFunction const* function, MachineStackPlacement const* placement,
                                                              CodegenMachineDebugIndex const* index, CodegenMachineDebugTimeline* timelines,
                                                              CodegenMachineDebugTimeline* slot_timelines, CodegenMachineDebugSelection* scratch,
                                                              u32 scratch_capacity, MachineRef reference, u32 frame_base_offset, Target target,
                                                              CodegenMachineDebugTimeline const** timeline_out)
{
    MachineRefKind kind = machine_ref_kind(reference);
    u32 payload = machine_ref_payload(reference);
    bool stack_slot = kind == MACHINE_REF_STACK_SLOT;
    CodegenMachineDebugTimeline* timeline = 0;
    if (stack_slot && payload < function->stack_slot_count)
    {
        timeline = slot_timelines + payload;
    }
    else if (kind == MACHINE_REF_VIRTUAL_REGISTER && payload < function->virtual_register_count)
    {
        timeline = timelines + payload;
    }
    if (timeline && !timeline->built)
    {
        u32 entry_count = 0;
        if (stack_slot)
        {
            s32 frame_offset = 0;
            timeline->valid =
                codegen_machine_debug_frame_offset(placement->stack_slot_offsets[payload], frame_base_offset, target, &frame_offset);
            scratch[0] = (CodegenMachineDebugSelection){
                .row = 0, .frame_offset = frame_offset, .physical_register = -1, .kind = CODEGEN_MACHINE_DEBUG_SELECTION_FRAME};
            entry_count = timeline->valid && function->instruction_count ? 1u : 0;
        }
        else
        {
            timeline->valid = codegen_machine_debug_reference_timeline(function, placement, index, payload, frame_base_offset, target, scratch,
                                                                       scratch_capacity, &entry_count);
        }
        CodegenMachineDebugSelection* entries = arena_allocate(arena, CodegenMachineDebugSelection, entry_count ? entry_count : 1u);
        memcpy(entries, scratch, sizeof(*entries) * (u64)entry_count);
        timeline->entries = entries;
        timeline->entry_count = entry_count;
        timeline->built = true;
    }
    *timeline_out = timeline;
    return timeline != 0 && timeline->valid;
}

BUSTER_GLOBAL_LOCAL bool codegen_machine_debug_locations_equal(DebugLocation const* left, DebugLocation const* right)
{
    bool equal = left->kind == right->kind && left->piece_count == right->piece_count;
    if (equal && left->kind == DEBUG_LOCATION_REGISTER)
    {
        equal = left->reg == right->reg;
    }
    else if (equal && left->kind == DEBUG_LOCATION_FRAME)
    {
        equal = left->frame_offset == right->frame_offset;
    }
    else if (equal && left->kind == DEBUG_LOCATION_CONSTANT)
    {
        equal = left->constant == right->constant;
    }
    else if (equal && left->kind == DEBUG_LOCATION_PIECEWISE)
    {
        for (u32 index = 0; index < left->piece_count; index += 1)
        {
            DebugLocationPiece a = left->pieces[index];
            DebugLocationPiece b = right->pieces[index];
            equal = equal && a.kind == b.kind && a.reg == b.reg && a.frame_offset == b.frame_offset && a.value_offset == b.value_offset &&
                    a.size == b.size;
        }
    }
    return equal;
}

BUSTER_GLOBAL_LOCAL bool codegen_record_machine_locations(Arena* arena, CodegenModule* result, CodegenDebugLocationSink* sink,
                                                           IrFunction* ir_function, MachineFunction const* function,
                                                           MachineStackPlacement const* placement, u32 const* row_offsets, u32 function_start,
                                                           u32 function_end, u32 frame_base_offset, Target target)
{
    bool recorded = result && result->debug_locations && function && placement && row_offsets && function_end >= function_start;
    for (u32 row = 0; recorded && row < function->instruction_count; row += 1)
    {
        if (row_offsets[row] > function_end - function_start || (row && row_offsets[row] < row_offsets[row - 1u]))
        {
            result->error = CODEGEN_ERROR_INVALID_IR;
            recorded = false;
        }
    }
    if (recorded && function->debug_value_count)
    {
        TemporalArena scratch = scratch_begin(&arena, 1);
        CodegenMachineDebugIndex index = {0};
        codegen_machine_debug_index_build(scratch.arena, function, placement, &index);
        u32 register_timeline_count = function->virtual_register_count ? function->virtual_register_count : 1u;
        u32 slot_timeline_count = function->stack_slot_count ? function->stack_slot_count : 1u;
        CodegenMachineDebugTimeline* register_timelines = arena_allocate(scratch.arena, CodegenMachineDebugTimeline, register_timeline_count);
        CodegenMachineDebugTimeline* slot_timelines = arena_allocate(scratch.arena, CodegenMachineDebugTimeline, slot_timeline_count);
        memset(register_timelines, 0, sizeof(*register_timelines) * (u64)register_timeline_count);
        memset(slot_timelines, 0, sizeof(*slot_timelines) * (u64)slot_timeline_count);
        // A replay records at most the row it stops at and the steady state
        // that follows it, and it stops at most once per row.
        u32 scratch_capacity = 2u * function->instruction_count + 2u;
        CodegenMachineDebugSelection* selection_scratch = arena_allocate(scratch.arena, CodegenMachineDebugSelection, scratch_capacity);
        for (u32 value_index = 0; recorded && value_index < function->debug_value_count; value_index += 1)
        {
            MachineDebugValue const* value = function->debug_values + value_index;
            u32 first_row = 0;
            u32 end_row = 0;
            bool malformed = false;
            if (!codegen_machine_debug_span(function, &index, value, &first_row, &end_row, &malformed))
            {
                if (malformed)
                {
                    result->error = CODEGEN_ERROR_INVALID_IR;
                    recorded = false;
                }
                else if (function_end > function_start &&
                         !codegen_canonical_location_append(result, sink, ir_function->symbol, value->local, function_start, function_end,
                                                            (DebugLocation){.kind = DEBUG_LOCATION_UNAVAILABLE}))
                {
                    recorded = false;
                }
                continue;
            }
            u32 piece_count = value->kind == MACHINE_DEBUG_VALUE_REFERENCE || value->kind == MACHINE_DEBUG_VALUE_PIECEWISE
                                  ? BUSTER_MIN(value->piece_count, (u8)BUSTER_ARRAY_LENGTH(value->pieces))
                                  : 0;
            CodegenMachineDebugTimeline const* piece_timelines[BUSTER_ARRAY_LENGTH(value->pieces)] = {0};
            u32 piece_cursors[BUSTER_ARRAY_LENGTH(value->pieces)] = {0};
            for (u32 piece_index = 0; recorded && piece_index < piece_count; piece_index += 1)
            {
                if (!codegen_machine_debug_timeline_for(scratch.arena, function, placement, &index, register_timelines, slot_timelines,
                                                        selection_scratch, scratch_capacity, value->pieces[piece_index], frame_base_offset,
                                                        target, piece_timelines + piece_index))
                {
                    result->error = CODEGEN_ERROR_INVALID_IR;
                    recorded = false;
                }
            }
            for (u32 piece_index = 0; recorded && piece_index < piece_count; piece_index += 1)
            {
                piece_cursors[piece_index] = codegen_machine_debug_timeline_seek(piece_timelines[piece_index], first_row);
            }
            DebugLocation pending = {.kind = DEBUG_LOCATION_UNAVAILABLE};
            DebugLocationPiece pending_pieces[BUSTER_ARRAY_LENGTH(value->pieces)] = {0};
            u32 pending_start = first_row;
            u32 row = first_row;
            bool done = !recorded;
            while (!done)
            {
                for (u32 piece_index = 0; piece_index < piece_count; piece_index += 1)
                {
                    CodegenMachineDebugTimeline const* timeline = piece_timelines[piece_index];
                    while (piece_cursors[piece_index] + 1u < timeline->entry_count &&
                           timeline->entries[piece_cursors[piece_index] + 1u].row <= row)
                    {
                        piece_cursors[piece_index] += 1u;
                    }
                }
                DebugLocation location = {.kind = DEBUG_LOCATION_UNAVAILABLE};
                DebugLocationPiece pieces[BUSTER_ARRAY_LENGTH(value->pieces)] = {0};
                bool available[BUSTER_ARRAY_LENGTH(value->pieces)] = {false};
                for (u32 piece_index = 0; row < end_row && piece_index < piece_count; piece_index += 1)
                {
                    CodegenMachineDebugTimeline const* timeline = piece_timelines[piece_index];
                    pieces[piece_index] = timeline->entry_count
                                              ? codegen_machine_debug_piece(function, value->pieces[piece_index], value->piece_sizes[piece_index],
                                                                            target, timeline->entries[piece_cursors[piece_index]],
                                                                            available + piece_index)
                                              : (DebugLocationPiece){0};
                }
                if (row < end_row && value->kind == MACHINE_DEBUG_VALUE_CONSTANT)
                {
                    location.kind = DEBUG_LOCATION_CONSTANT;
                    location.constant = value->constant;
                }
                else if (row < end_row && value->kind == MACHINE_DEBUG_VALUE_REFERENCE && piece_count && available[0])
                {
                    location.kind = pieces[0].kind;
                    location.reg = pieces[0].reg;
                    location.frame_offset = pieces[0].frame_offset;
                }
                else if (row < end_row && value->kind == MACHINE_DEBUG_VALUE_PIECEWISE)
                {
                    bool all = value->piece_count == 2;
                    u32 piece_offset = 0;
                    for (u32 piece_index = 0; piece_index < piece_count; piece_index += 1)
                    {
                        all = all && available[piece_index];
                        pieces[piece_index].value_offset = piece_offset;
                        pieces[piece_index].size = value->piece_sizes[piece_index];
                        piece_offset += value->piece_sizes[piece_index];
                    }
                    if (all)
                    {
                        location.kind = DEBUG_LOCATION_PIECEWISE;
                        location.pieces = pieces;
                        location.piece_count = value->piece_count;
                    }
                }
                if (row == end_row || !codegen_machine_debug_locations_equal(&pending, &location))
                {
                    u32 start = pending_start < function->instruction_count ? function_start + row_offsets[pending_start] : function_end;
                    u32 end = row < function->instruction_count ? function_start + row_offsets[row] : function_end;
                    if (end > start)
                    {
                        if (pending.kind == DEBUG_LOCATION_PIECEWISE)
                        {
                            DebugLocationPiece* stable = arena_allocate(arena, DebugLocationPiece, pending.piece_count);
                            memcpy(stable, pending.pieces, sizeof(*stable) * pending.piece_count);
                            pending.pieces = stable;
                        }
                        if (!codegen_canonical_location_append(result, sink, ir_function->symbol, value->local, start, end, pending))
                        {
                            recorded = false;
                            done = true;
                        }
                    }
                    pending = location;
                    if (location.kind == DEBUG_LOCATION_PIECEWISE)
                    {
                        memcpy(pending_pieces, pieces, sizeof(*pieces) * location.piece_count);
                        pending.pieces = pending_pieces;
                    }
                    pending_start = row;
                }
                if (row == end_row)
                {
                    done = true;
                }
                else if (!done)
                {
                    u32 next = end_row;
                    for (u32 piece_index = 0; piece_index < piece_count; piece_index += 1)
                    {
                        CodegenMachineDebugTimeline const* timeline = piece_timelines[piece_index];
                        if (piece_cursors[piece_index] + 1u < timeline->entry_count)
                        {
                            next = BUSTER_MIN(next, timeline->entries[piece_cursors[piece_index] + 1u].row);
                        }
                    }
                    row = next;
                }
            }
        }
        scratch_end(scratch);
    }
    return recorded;
}

#if BUSTER_INCLUDE_TESTS
// Whole-function reference recording, kept as the differential reference for
// the event-driven routine above. Every debug value replays every row of the
// function for each of its pieces; the event-driven routine must agree with it
// seed for seed. Production never calls it.
BUSTER_GLOBAL_LOCAL bool codegen_machine_debug_span_dense(MachineFunction const* function, MachineDebugValue const* value, u32* first_row, u32* end_row,
                                                     bool* malformed)
{
    bool result = value->first_instruction == UINT32_MAX;
    *first_row = 0;
    *end_row = function->instruction_count;
    if (!result)
    {
        if (value->first_instruction > UINT32_MAX - value->instruction_count)
        {
            *malformed = true;
            return false;
        }
        u32 instruction_end = value->first_instruction + value->instruction_count;
        u32 first_mark = UINT32_MAX;
        u32 last_mark = UINT32_MAX;
        for (u32 mark_index = 0; mark_index < function->line_mark_count; mark_index += 1)
        {
            MachineLineMark mark = function->line_marks[mark_index];
            if (mark.row > function->instruction_count || (mark_index && mark.row < function->line_marks[mark_index - 1].row))
            {
                *malformed = true;
                return false;
            }
            if (mark.instruction >= value->first_instruction && mark.instruction < instruction_end)
            {
                first_mark = BUSTER_MIN(first_mark, mark_index);
                last_mark = mark_index;
            }
        }
        if (first_mark != UINT32_MAX)
        {
            *first_row = function->line_marks[first_mark].row;
            *end_row = last_mark + 1 < function->line_mark_count ? function->line_marks[last_mark + 1].row : function->instruction_count;
            result = *first_row < *end_row;
        }
        else
        {
            *malformed = true;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool codegen_machine_debug_reference_rows_dense(MachineFunction const* function,
                                                               MachineStackPlacement const* placement, MachineRef reference, u32 value_size,
                                                               u32 frame_base_offset, Target target, DebugLocationPiece* rows, bool* available)
{
    MachineRefKind kind = machine_ref_kind(reference);
    u32 payload = machine_ref_payload(reference);
    if (kind == MACHINE_REF_STACK_SLOT)
    {
        s32 frame_offset = 0;
        if (payload >= function->stack_slot_count ||
            !codegen_machine_debug_frame_offset(placement->stack_slot_offsets[payload], frame_base_offset, target, &frame_offset))
        {
            return false;
        }
        for (u32 row = 0; row < function->instruction_count; row += 1)
        {
            rows[row] = (DebugLocationPiece){.kind = DEBUG_LOCATION_FRAME, .frame_offset = frame_offset};
            available[row] = true;
        }
        return true;
    }
    if (kind != MACHINE_REF_VIRTUAL_REGISTER || payload >= function->virtual_register_count)
    {
        return false;
    }
    // A homeless register is a value without a frame location, not invalid IR:
    // rows that would select its frame copy stay unavailable.
    u32 home = placement->virtual_register_offsets[payload];
    s32 frame_offset = 0;
    if (home != MACHINE_VIRTUAL_REGISTER_NO_HOME && !codegen_machine_debug_frame_offset(home, frame_base_offset, target, &frame_offset))
    {
        return false;
    }
    CodegenMachineDebugReference state = {.physical_register = -1};
    u32 edit_cursor = 0;
    u32 block_cursor = 0;
    for (u32 row = 0; row < function->instruction_count; row += 1)
    {
        if (block_cursor < function->block_count && function->blocks[block_cursor].first_instruction == row)
        {
            if (block_cursor)
            {
                // Physical ownership is edge-specific. A published home is
                // path-independent: every executing definition spilled the
                // same vreg there, and reuse writes invalidate it explicitly.
                // The home preference needs no republication beside it: with no
                // register held it does not enter the selection, and every
                // transition back to holding one rewrites it.
                state.physical_register = -1;
            }
            block_cursor += 1;
        }
        bool selected_frame = state.frame_valid && (state.prefer_frame || state.physical_register < 0);
        s32 selected_register = selected_frame ? -1 : state.physical_register;
        bool selected_invalid = false;
        MachinePoint before = machine_point_make(row, MACHINE_POINT_BEFORE);
        while (edit_cursor < placement->edit_count && placement->edits[edit_cursor].point == before)
        {
            codegen_machine_debug_edit_state(function, placement, payload, placement->edits + edit_cursor, &state, selected_frame,
                                             selected_register, &selected_invalid);
            edit_cursor += 1;
        }
        MachineInstruction const* instruction = function->instructions + row;
        MachineOpcodeRow opcode_row = machine_instruction_opcode_row(function, instruction);
        if (selected_register >= 0 && selected_register < 64 && (opcode_row.clobber_mask & (UINT64_C(1) << selected_register)))
        {
            selected_invalid = true;
        }
        if (state.physical_register >= 0 && state.physical_register < 64 &&
            (opcode_row.clobber_mask & (UINT64_C(1) << state.physical_register)))
        {
            state.physical_register = -1;
        }
        MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
        for (u32 operand_index = 0; info && operand_index < info->operand_count; operand_index += 1)
        {
            u32 role = info->operand_info[operand_index] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
            MachineRef operand = instruction->operands[operand_index];
            bool own = machine_ref_kind(operand) == MACHINE_REF_VIRTUAL_REGISTER && machine_ref_payload(operand) == payload;
            u32 physical = placement->operand_registers[(u64)row * MACHINE_INSTRUCTION_OPERAND_COUNT + operand_index];
            if ((role == MACHINE_OPERAND_ROLE_USE || role == MACHINE_OPERAND_ROLE_USE_DEFINE) && own && state.physical_register < 0)
            {
                // Entry/CFG parameters intentionally have no definition row.
                // Their first allocated use is nevertheless a certified read
                // of the incoming value; publish it only after that row.
                state.physical_register = (s32)physical;
                state.prefer_frame = false;
                state.epoch += 1;
            }
            if (role != MACHINE_OPERAND_ROLE_DEFINE && role != MACHINE_OPERAND_ROLE_USE_DEFINE)
            {
                continue;
            }
            if (selected_register == (s32)physical)
            {
                selected_invalid = true;
            }
            if (state.physical_register == (s32)physical)
            {
                state.physical_register = -1;
            }
            if (own)
            {
                state.physical_register = (s32)physical;
                state.prefer_frame = false;
                state.epoch += 1;
            }
        }
        MachinePoint after = machine_point_make(row, MACHINE_POINT_AFTER);
        while (edit_cursor < placement->edit_count && placement->edits[edit_cursor].point == after)
        {
            codegen_machine_debug_edit_state(function, placement, payload, placement->edits + edit_cursor, &state, selected_frame,
                                             selected_register, &selected_invalid);
            edit_cursor += 1;
        }
        if (!selected_invalid && selected_frame && home != MACHINE_VIRTUAL_REGISTER_NO_HOME)
        {
            rows[row] = (DebugLocationPiece){.kind = DEBUG_LOCATION_FRAME, .frame_offset = frame_offset};
            available[row] = true;
        }
        else if (!selected_invalid && selected_register >= 0)
        {
            DebugRegister reg = codegen_machine_debug_register(function, payload, (u32)selected_register, value_size, target);
            if (reg != DEBUG_REGISTER_NONE)
            {
                rows[row] = (DebugLocationPiece){.kind = DEBUG_LOCATION_REGISTER, .reg = reg};
                available[row] = true;
            }
        }
    }
    return edit_cursor == placement->edit_count;
}

BUSTER_GLOBAL_LOCAL bool codegen_record_machine_locations_dense(Arena* arena, CodegenModule* result, CodegenDebugLocationSink* sink,
                                                                 IrFunction* ir_function, MachineFunction const* function,
                                                                 MachineStackPlacement const* placement, u32 const* row_offsets,
                                                                 u32 function_start, u32 function_end, u32 frame_base_offset, Target target)
{
    if (!result || !result->debug_locations || !function || !placement || !row_offsets || function_end < function_start)
    {
        return false;
    }
    for (u32 row = 0; row < function->instruction_count; row += 1)
    {
        if (row_offsets[row] > function_end - function_start || (row && row_offsets[row] < row_offsets[row - 1]))
        {
            result->error = CODEGEN_ERROR_INVALID_IR;
            return false;
        }
    }
    u32 row_capacity = function->instruction_count ? function->instruction_count : 1;
    DebugLocationPiece* piece_rows[2] = {
        arena_allocate(arena, DebugLocationPiece, row_capacity), arena_allocate(arena, DebugLocationPiece, row_capacity)};
    bool* piece_available[2] = {arena_allocate(arena, bool, row_capacity), arena_allocate(arena, bool, row_capacity)};
    for (u32 value_index = 0; value_index < function->debug_value_count; value_index += 1)
    {
        MachineDebugValue const* value = function->debug_values + value_index;
        // The row buffers hold one entry per piece the layout can describe, so
        // a record claiming more pieces than that is clipped rather than read
        // past them. The event-driven routine clips the same way.
        u32 piece_count = BUSTER_MIN(value->piece_count, (u8)BUSTER_ARRAY_LENGTH(value->pieces));
        u32 first_row = 0;
        u32 end_row = 0;
        bool malformed = false;
        if (!codegen_machine_debug_span_dense(function, value, &first_row, &end_row, &malformed))
        {
            if (malformed)
            {
                result->error = CODEGEN_ERROR_INVALID_IR;
                return false;
            }
            if (function_end > function_start &&
                !codegen_canonical_location_append(result, sink, ir_function->symbol, value->local, function_start, function_end,
                                                   (DebugLocation){.kind = DEBUG_LOCATION_UNAVAILABLE}))
            {
                return false;
            }
            continue;
        }
        memset(piece_available[0], 0, sizeof(**piece_available) * row_capacity);
        memset(piece_available[1], 0, sizeof(**piece_available) * row_capacity);
        if (value->kind == MACHINE_DEBUG_VALUE_REFERENCE || value->kind == MACHINE_DEBUG_VALUE_PIECEWISE)
        {
            for (u32 piece_index = 0; piece_index < piece_count; piece_index += 1)
            {
                if (!codegen_machine_debug_reference_rows_dense(function, placement, value->pieces[piece_index], value->piece_sizes[piece_index],
                                                          frame_base_offset, target, piece_rows[piece_index], piece_available[piece_index]))
                {
                    result->error = CODEGEN_ERROR_INVALID_IR;
                    return false;
                }
            }
        }
        DebugLocation pending = {.kind = DEBUG_LOCATION_UNAVAILABLE};
        DebugLocationPiece pending_pieces[2] = {0};
        u32 pending_start = first_row;
        for (u32 row = first_row; row <= end_row; row += 1)
        {
            DebugLocation location = {.kind = DEBUG_LOCATION_UNAVAILABLE};
            DebugLocationPiece pieces[2] = {0};
            if (row < end_row && value->kind == MACHINE_DEBUG_VALUE_CONSTANT)
            {
                location.kind = DEBUG_LOCATION_CONSTANT;
                location.constant = value->constant;
            }
            else if (row < end_row && value->kind == MACHINE_DEBUG_VALUE_REFERENCE && piece_available[0][row])
            {
                location.kind = piece_rows[0][row].kind;
                location.reg = piece_rows[0][row].reg;
                location.frame_offset = piece_rows[0][row].frame_offset;
            }
            else if (row < end_row && value->kind == MACHINE_DEBUG_VALUE_PIECEWISE)
            {
                bool all = value->piece_count == 2;
                u32 piece_offset = 0;
                for (u32 piece_index = 0; piece_index < piece_count; piece_index += 1)
                {
                    all = all && piece_available[piece_index][row];
                    pieces[piece_index] = piece_rows[piece_index][row];
                    pieces[piece_index].value_offset = piece_offset;
                    pieces[piece_index].size = value->piece_sizes[piece_index];
                    piece_offset += value->piece_sizes[piece_index];
                }
                if (all)
                {
                    location.kind = DEBUG_LOCATION_PIECEWISE;
                    location.pieces = pieces;
                    location.piece_count = value->piece_count;
                }
            }
            if (row == end_row || !codegen_machine_debug_locations_equal(&pending, &location))
            {
                u32 start = pending_start < function->instruction_count ? function_start + row_offsets[pending_start] : function_end;
                u32 end = row < function->instruction_count ? function_start + row_offsets[row] : function_end;
                if (end > start)
                {
                    if (pending.kind == DEBUG_LOCATION_PIECEWISE)
                    {
                        DebugLocationPiece* stable = arena_allocate(arena, DebugLocationPiece, pending.piece_count);
                        memcpy(stable, pending.pieces, sizeof(*stable) * pending.piece_count);
                        pending.pieces = stable;
                    }
                    if (!codegen_canonical_location_append(result, sink, ir_function->symbol, value->local, start, end, pending))
                    {
                        return false;
                    }
                }
                pending = location;
                if (location.kind == DEBUG_LOCATION_PIECEWISE)
                {
                    memcpy(pending_pieces, pieces, sizeof(*pieces) * location.piece_count);
                    pending.pieces = pending_pieces;
                }
                pending_start = row;
            }
        }
    }
    return true;
}

bool codegen_test_record_machine_locations(Arena* arena, CodegenModule* result, u32 capacity, IrFunction* ir_function,
                                            MachineFunction const* function, MachineStackPlacement const* placement,
                                            u32 const* row_offsets, u32 function_start, u32 function_end, u32 frame_base_offset, Target target)
{
    CodegenDebugLocationSink sink = {.capacity = capacity};
    return codegen_record_machine_locations(arena, result, &sink, ir_function, function, placement, row_offsets, function_start, function_end,
                                            frame_base_offset, target);
}

// Recording into arena-owned seed storage, which grows with what it emits.
bool codegen_test_record_machine_locations_growing(Arena* arena, CodegenModule* result, u32* capacity, IrFunction* ir_function,
                                                    MachineFunction const* function, MachineStackPlacement const* placement,
                                                    u32 const* row_offsets, u32 function_start, u32 function_end, u32 frame_base_offset,
                                                    Target target)
{
    CodegenDebugLocationSink sink = {.arena = arena, .capacity = *capacity};
    bool recorded = codegen_record_machine_locations(arena, result, &sink, ir_function, function, placement, row_offsets, function_start,
                                                     function_end, frame_base_offset, target);
    *capacity = sink.capacity;
    return recorded;
}

// The widest change-point timeline the event-driven recording would build for
// this function. Sparsity is the whole point of the routine: a timeline that
// holds an entry per row is walked again by every value that names the
// register, which is the whole-function replay the routine replaced.
u32 codegen_test_machine_debug_widest_timeline(Arena* arena, MachineFunction const* function, MachineStackPlacement const* placement,
                                                u32 frame_base_offset, Target target)
{
    u32 widest = 0;
    TemporalArena scratch = scratch_begin(&arena, 1);
    CodegenMachineDebugIndex index = {0};
    codegen_machine_debug_index_build(scratch.arena, function, placement, &index);
    u32 register_timeline_count = function->virtual_register_count ? function->virtual_register_count : 1u;
    u32 slot_timeline_count = function->stack_slot_count ? function->stack_slot_count : 1u;
    CodegenMachineDebugTimeline* register_timelines = arena_allocate(scratch.arena, CodegenMachineDebugTimeline, register_timeline_count);
    CodegenMachineDebugTimeline* slot_timelines = arena_allocate(scratch.arena, CodegenMachineDebugTimeline, slot_timeline_count);
    memset(register_timelines, 0, sizeof(*register_timelines) * (u64)register_timeline_count);
    memset(slot_timelines, 0, sizeof(*slot_timelines) * (u64)slot_timeline_count);
    u32 scratch_capacity = 2u * function->instruction_count + 2u;
    CodegenMachineDebugSelection* selection_scratch = arena_allocate(scratch.arena, CodegenMachineDebugSelection, scratch_capacity);
    for (u32 value_index = 0; value_index < function->debug_value_count; value_index += 1)
    {
        MachineDebugValue const* value = function->debug_values + value_index;
        u32 piece_count = value->kind == MACHINE_DEBUG_VALUE_REFERENCE || value->kind == MACHINE_DEBUG_VALUE_PIECEWISE
                              ? BUSTER_MIN(value->piece_count, (u8)BUSTER_ARRAY_LENGTH(value->pieces))
                              : 0;
        for (u32 piece_index = 0; piece_index < piece_count; piece_index += 1)
        {
            CodegenMachineDebugTimeline const* timeline = 0;
            codegen_machine_debug_timeline_for(scratch.arena, function, placement, &index, register_timelines, slot_timelines,
                                               selection_scratch, scratch_capacity, value->pieces[piece_index], frame_base_offset, target,
                                               &timeline);
            widest = timeline && timeline->entry_count > widest ? timeline->entry_count : widest;
        }
    }
    scratch_end(scratch);
    return widest;
}

bool codegen_test_record_machine_locations_dense(Arena* arena, CodegenModule* result, u32 capacity, IrFunction* ir_function,
                                                  MachineFunction const* function, MachineStackPlacement const* placement,
                                                  u32 const* row_offsets, u32 function_start, u32 function_end, u32 frame_base_offset,
                                                  Target target)
{
    CodegenDebugLocationSink sink = {.capacity = capacity};
    return codegen_record_machine_locations_dense(arena, result, &sink, ir_function, function, placement, row_offsets, function_start,
                                                  function_end, frame_base_offset, target);
}
#endif

// One generation of the whole module -- globals, functions and global assembly
// -- into a code buffer reserved at `capacity_scale` times the flat estimate
// below. Everything it produces comes out of `arena`, so a caller that does not
// like the answer can rewind and ask again; the target, the program ABI and the
// IR validation are its caller's business and are not repeated per attempt.
BUSTER_GLOBAL_LOCAL CodegenModule codegen_generate_canonical_module_attempt(Arena* arena, IrProgram* program,
                                                                           CodegenSlotCost const* slot_costs, IrModule* module,
                                                                           Target target, CodegenModuleOptions options, u64 capacity_scale,
                                                                           bool* code_buffer_exhausted,
                                                                           CodegenX64MetadataCache* x64_metadata_cache,
                                                                           MachineSelectionModule* machine_module, BootstrapTrace* bootstrap_trace)
{
    CodegenModule result = {
        .ir_module = module,
        .abi = codegen_abi_for_target(target),
        .failed_function = IR_FUNCTION_ID_INVALID,
        .failed_instruction = IR_INSTRUCTION_ID_INVALID,
        .failed_opcode = IR_OPCODE_COUNT,
    };
    // The one place -fPIC is turned into a fact about this module. It is a
    // statement about which references `ld` will place in a shared object, so
    // it is scoped to the format and architecture whose relocations say that:
    // x86-64 ELF. Windows images relocate as a whole and Mach-O's model is
    // its own; neither reads this flag.
    bool position_independent =
        options.position_independent && target.cpu_arch == CPU_ARCH_X86_64 && object_format_for_target(target) == OBJECT_FORMAT_ELF64;
    result.position_independent = position_independent;
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
        bool read_only = codegen_global_is_read_only(global);
        bool zero_fill = !read_only && global->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO;
        u64* capacity = zero_fill && global->is_thread_local ? &thread_local_zero_capacity
                        : zero_fill                           ? &zero_fill_capacity
                        : global->is_thread_local ? &thread_local_capacity
                        : read_only               ? &read_only_capacity
                                                  : &writable_capacity;
        u64 remainder = *capacity % global_alignment;
        if (remainder)
        {
            *capacity += global_alignment - remainder;
        }
        *capacity += type->layout.size;
    }
    // The three data images are written initializer by initializer over a
    // zero background; a fresh arena mapping is that background already.
    u8* read_only_bytes = arena_allocate_zeroed(arena, u8, read_only_capacity);
    u8* writable_bytes = arena_allocate(arena, u8, writable_capacity);
    u8* thread_local_bytes = arena_allocate(arena, u8, thread_local_capacity);
    if (writable_capacity)
    {
        memset(writable_bytes, 0, writable_capacity);
    }
    if (thread_local_capacity)
    {
        memset(thread_local_bytes, 0, thread_local_capacity);
    }
    // Zero-fill offsets are planned ahead of the assignment loop below because
    // they are not taken in declaration order: globals under
    // CODEGEN_LARGE_ZERO_FILL_THRESHOLD come first and the large ones follow,
    // each group in declaration order so the layout stays deterministic.
    u64* zero_fill_offsets = arena_allocate(arena, u64, module->global_count ? module->global_count : 1);
    u64 zero_fill_count = 0;
    for (u32 layout_pass = 0; layout_pass < 2; layout_pass += 1)
    {
        for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
        {
            IrGlobal* global = module->globals + global_index;
            IrType* type = ir_type_from_id(&program->types, global->type);
            bool zero_fill = !codegen_global_is_read_only(global) && global->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO;
            bool large = type->layout.size >= CODEGEN_LARGE_ZERO_FILL_THRESHOLD;
            if (zero_fill && !global->is_thread_local && large == (layout_pass == 1))
            {
                u32 global_alignment = global->alignment ? global->alignment : type->layout.alignment;
                u64 remainder = zero_fill_count % global_alignment;
                if (remainder)
                {
                    zero_fill_count += global_alignment - remainder;
                }
                zero_fill_offsets[global_index] = zero_fill_count;
                zero_fill_count += type->layout.size;
            }
        }
    }
    u64 read_only_count = 0;
    u64 writable_count = 0;
    u64 thread_local_count = 0;
    u64 thread_local_zero_count = 0;
    for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
    {
        IrGlobal* global = module->globals + global_index;
        IrType* type = ir_type_from_id(&program->types, global->type);
        u32 global_alignment = global->alignment ? global->alignment : type->layout.alignment;
        bool read_only = codegen_global_is_read_only(global);
        bool zero_fill = !read_only && global->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO;
        u8* bytes = zero_fill ? 0 : global->is_thread_local ? thread_local_bytes : read_only ? read_only_bytes : writable_bytes;
        u32 offset;
        if (zero_fill && !global->is_thread_local)
        {
            offset = (u32)zero_fill_offsets[global_index];
        }
        else
        {
            u64* count = zero_fill                ? &thread_local_zero_count
                         : global->is_thread_local ? &thread_local_count
                         : read_only               ? &read_only_count
                                                   : &writable_count;
            u64 remainder = *count % global_alignment;
            if (remainder)
            {
                *count += global_alignment - remainder;
            }
            offset = (u32)*count;
            *count += type->layout.size;
        }
        result.globals[result.global_count++] = (CodegenModuleGlobal){
            .symbol = global->symbol,
            .offset = offset,
            .size = (u32)type->layout.size,
            .alignment = global_alignment,
            .read_only = read_only,
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
    // What the inline-assembly templates in this module can add to the
    // relocation array, on the same argument the module-level blocks are
    // bounded by: a template reports at most one relocation per symbol
    // reference and a reference costs at least one source byte. The opcode
    // summary answers for every function that carries no template, which is
    // all but a handful in any module.
    u64 inline_assembly_capacity = 0;
    u32 type_count = program->types.count;
    u64 slot_before_align = target.cpu_arch == CPU_ARCH_X86_64 ? ~(u64)0 : 0;
    u64 slot_after_align = ~slot_before_align;
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        instruction_count += function->instruction_count;
        if (ir_function_may_contain_opcodes(function, IR_OPCODE_BIT(IR_OPCODE_INLINE_ASSEMBLY)))
        {
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                if (function->instructions[instruction_index].opcode == IR_OPCODE_INLINE_ASSEMBLY)
                {
                    inline_assembly_capacity += ir_instruction_extra(function, (IrInstructionId){.value = instruction_index}).literal.length;
                }
            }
        }
        if (options.debug_info)
        {
            u64 local_capacity = function->debug_local_count ? function->debug_local_count : function->local_count;
            // Exact bound for the canonical producer. The machine producer
            // reserves its stronger post-selection local-record x row bound
            // once the scheduled MIR row count is known.
            debug_location_capacity_64 += local_capacity * ((u64)function->block_count + 1);
            if (debug_location_capacity_64 > UINT32_MAX)
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
        }
        // The frame the function's canonical slots would take, walked from the
        // per-type slot table (CodegenSlotCost) rather than the type records.
        // The instruction row is read for a place -- a GLOBAL's result is one,
        // which ir_validate_instruction_operation requires, and places are
        // under a fifth of the values -- and for the rare over-aligned local,
        // whose raw size the table does not carry. The two sums must be exactly
        // what the record-walking form computed: the code buffer is reserved
        // from them and an exhausted buffer regenerates the whole module.
        u64 function_value_bytes = 0;
        IrValue const* values = function->values;
        IrInstruction const* instructions = function->instructions;
        u32 function_instruction_count = function->instruction_count;
        for (u32 value_index = 0; value_index < function->value_count; value_index += 1)
        {
            IrValue value = values[value_index];
            if (value.canonical_type.value >= type_count)
            {
                result.error = CODEGEN_ERROR_INVALID_IR;
                return result;
            }
            CodegenSlotCost cost = slot_costs[value.canonical_type.value];
            // A global place occupies an eightbyte holding the object's
            // address, so it needs no layout for the object itself. That is
            // what lets `extern struct opaque object;` be addressed without
            // being completed, which C permits and musl's `src/include/stdio.h`
            // relies on -- it declares `__stderr_FILE` while suppressing the
            // definition of `struct _IO_FILE`, so every `stderr` in the tree
            // takes the address of an incomplete object.
            bool global_place = value.category == IR_VALUE_PLACE && value.definition.value < function_instruction_count &&
                                instructions[value.definition.value].opcode == IR_OPCODE_GLOBAL;
            u64 slot_size = global_place ? 8 : cost.size;
            u64 slot_alignment = global_place ? 8 : BUSTER_MAX(cost.alignment, value.alignment);
            if (!slot_size)
            {
                result.error = CODEGEN_ERROR_INVALID_IR;
                return result;
            }
            // x86-64 places the slot before aligning, AArch64 after; the two
            // masks are the target decided once outside the loop.
            function_value_bytes += slot_size & slot_before_align;
            // slot_alignment is always a power of two: type layout alignments
            // bottom out in target_data_layout's 1..16 table (aggregates take
            // a max of member alignments, vectors a power-of-two byte size),
            // and requested value alignments pass c_ir_alignment_evaluate's
            // power-of-two check. That licenses align_forward_unchecked's mask here and
            // in the offset-assignment loop below; a `%` compiles to a
            // hardware divide in a loop that visits every value of every
            // function.
            function_value_bytes = align_forward_unchecked(function_value_bytes, slot_alignment);
            function_value_bytes += slot_size & slot_after_align;
            if (value.alignment > 16 && value.definition.value < function_instruction_count &&
                instructions[value.definition.value].opcode == IR_OPCODE_LOCAL)
            {
                function_value_bytes += ir_type_from_id(&program->types, value.canonical_type)->layout.size + value.alignment - 1;
            }
            // A value this wide can be handed to a call on the stack, and an
            // area aligned for it is filled an eightbyte at a time rather than
            // pushed. That is more code than the flat per-instruction reserve
            // below carries, so the value pays for the copy it can provoke.
            if (slot_alignment > CODEGEN_X64_STACK_ALIGNMENT)
            {
                aligned_argument_capacity += ((slot_size / 8) * 15 + 32) & slot_before_align;
            }
        }
        u64 probe_count = (function_value_bytes + A64_SP_ADJUST_CHUNK - 1) / A64_SP_ADJUST_CHUNK;
        stack_probe_capacity += probe_count * 11;
    }
    CodegenDebugLocationSink debug_location_sink = {.arena = arena, .capacity = (u32)debug_location_capacity_64};
    u32 global_relocation_count = 0;
    for (u32 global_index = 0; global_index < module->global_count; global_index += 1)
    {
        IrGlobal* global = module->globals + global_index;
        global_relocation_count += global->relocation_count;
        global_relocation_count += global->initializer_kind == IR_GLOBAL_INITIALIZER_SYMBOL_ADDRESS;
    }
    // An assembly block reports at most one relocation per symbol reference,
    // and a reference costs at least one source byte, so the source's own
    // length bounds what it can add on top of the per-instruction and
    // per-global terms -- for a module-level block and for an inline template
    // alike. The total is carried into both emitters, which are the only
    // producers that append after this array is sized.
    u32 relocation_capacity = instruction_count * 3 + global_relocation_count +
                              (u32)BUSTER_MIN(assembly_capacity + inline_assembly_capacity, UINT32_MAX - global_relocation_count);
    result.relocations = arena_allocate(arena, CodegenModuleRelocation, relocation_capacity);
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
    // Whether this attempt has asked for the x86 cache it was not handed;
    // see codegen_buffer_ensure_x64_metadata_cache.
    bool x64_metadata_cache_tried = x64_metadata_cache != 0;
    // Every function contributes a row for its own declaration on top of the
    // per-instruction rows.
    u32 line_entry_capacity = instruction_count + module->function_count;
    // A source the program's table does not hold is recorded as file 0, the
    // clamp the object writer once applied to every row on its way to the
    // DWARF builder; doing it here is what lets the writer alias the array.
    u32 line_source_limit = BUSTER_MIN(program->sources.count, (u32)UINT16_MAX + 1);
    result.line_entries = options.debug_info ? arena_allocate(arena, CodegenLineEntry, line_entry_capacity) : 0;
    result.debug_locations = options.debug_info ? arena_allocate(arena, DebugLocationSeed, debug_location_sink.capacity) : 0;
    result.debug_info = options.debug_info;
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        IrFunction* function = module->functions + function_index;
        result.failed_function = (IrFunctionId){
            .value = function_index,
        };
        result.failed_instruction = IR_INSTRUCTION_ID_INVALID;
        result.failed_opcode = IR_OPCODE_COUNT;
        if (function->state != IR_FUNCTION_LOWERED)
        {
            continue;
        }
        // Share the target-derived executable padding policy with source
        // alignment. x86 remains one bulk memset, not one encoding per byte.
        u64 alignment = target.cpu_arch == CPU_ARCH_AARCH64 ? 4 : 16;
        u64 entry_padding = (0 - buffer.count) & (alignment - 1);
        u8* entry_padding_bytes = 0;
        if (buffer.error == CODEGEN_ERROR_NONE && entry_padding && codegen_buffer_reserve(&buffer, entry_padding, &entry_padding_bytes))
        {
            if (!assembly_fill_executable_padding(target, entry_padding_bytes, buffer.count - entry_padding, entry_padding))
            {
                buffer.error = CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
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
                                    function->source.source.value, line_source_limit, declaration.line, declaration.column);
        }
        IrBlock* entry = function->blocks + function->entry.value;
        if (entry->first_instruction.value >= function->instruction_count)
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
        u32* machine_block_offsets = 0;
        bool machine_function_emitted = false;
        CodegenFallbackReason fallback_reason = CODEGEN_FALLBACK_TARGET_EXCLUDED;
        IrOpcode fallback_opcode = IR_OPCODE_COUNT;
        // The descriptor is a shell until this path knows its unwind shape.
        // Every public allocator mode reaches machine selection, placement,
        // and encoding; a failure leaves this attempt unpublished and returns
        // a structured error to the caller. ELF/Mach-O frames retain their
        // established shape. PE AArch64
        // uses a compact chain/save area and bounded probe, with every
        // prologue instruction represented in its unwind description.
        if (target.cpu_arch == CPU_ARCH_X86_64 || target.cpu_arch == CPU_ARCH_AARCH64)
        {
            TemporalArena machine_scratch = scratch_begin(&arena, 1);
            MachineSelectResult selected = {0};
            selected = machine_select_validated_canonical_function(machine_scratch.arena, program, function, target, position_independent,
                                                                   options.register_allocator != CODEGEN_REGISTER_ALLOCATOR_MIR_STACK,
                                                                   options.debug_info, machine_module);
            if (bootstrap_trace)
            {
                bootstrap_trace_machine(bootstrap_trace, function, &selected);
                if (bootstrap_trace->invalid_mir)
                {
                    // Do not let a selector certificate bypass a failed
                    // audit verifier and feed invalid MIR to allocation.
                    result.failed_machine_verification = bootstrap_trace->invalid_validation;
                    fallback_reason = CODEGEN_FALLBACK_VERIFICATION;
                    selected.supported = false;
                }
            }
            machine_simd_operation_count = selected.simd_operation_count;
            if (!selected.supported && fallback_reason != CODEGEN_FALLBACK_VERIFICATION)
            {
                fallback_opcode = selected.failed_opcode < IR_OPCODE_COUNT ? selected.failed_opcode : IR_OPCODE_COUNT;
                fallback_reason = selected.signature_rejected ? CODEGEN_FALLBACK_SIGNATURE
                                  : fallback_opcode < IR_OPCODE_COUNT ? CODEGEN_FALLBACK_OPCODE
                                                                      : CODEGEN_FALLBACK_SELECTION_OTHER;
            }
            // The target selectors publish a complete machine function only
            // after their typed builder streams and side tables are closed.
            // Keep the verifier as the authority for replayed/manual machine
            // IR, but do not reread every freshly selected row before its
            // immediate allocator consumer.
            MachineVerifyResult verification = selected.supported && (options.verify_invariants || !selected.selector_certified)
                                                   ? machine_verify_function(&selected.function) : (MachineVerifyResult){0};
            MachineVerifyError verify_error = verification.error;
            if (options.verify_invariants && selected.supported)
            {
                result.statistics.verified_mir_function_count += 1;
                if (verify_error != MACHINE_VERIFY_NONE)
                {
                    result.failed_machine_verification = verification;
                    fallback_reason = CODEGEN_FALLBACK_VERIFICATION;
                    selected.supported = false;
                }
            }
            if (selected.supported && verify_error != MACHINE_VERIFY_NONE)
            {
                fallback_reason = CODEGEN_FALLBACK_VERIFICATION;
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
                        bool scheduled_valid = true;
                        if (options.verify_invariants)
                        {
                            result.statistics.verified_scheduled_function_count += 1;
                            MachineVerifyResult scheduled_verification = machine_verify_function(&scheduled.function);
                            if (scheduled_verification.error != MACHINE_VERIFY_NONE)
                            {
                                result.failed_machine_verification = scheduled_verification;
                                result.failed_machine_scheduled = true;
                                fallback_reason = CODEGEN_FALLBACK_VERIFICATION;
                                scheduled_valid = false;
                                placement.valid = false;
                            }
                        }
                        if (scheduled_valid)
                        {
                            result.statistics.allocator_scheduled_function_count += 1;
                            MachineStackPlacement scheduled_placement = machine_quality_placement_build(machine_scratch.arena, &scheduled.function);
                            if (options.verify_invariants && !scheduled_placement.valid)
                            {
                                fallback_reason = CODEGEN_FALLBACK_VERIFICATION;
                                placement.valid = false;
                            }
                            u32 placement_saved_registers = 0;
                            u32 scheduled_saved_registers = 0;
                            for (u32 physical_register = 0; physical_register < MACHINE_TARGET_REGISTER_LIMIT; physical_register += 1)
                            {
                                placement_saved_registers += (u32)((placement.callee_saved_mask >> physical_register) & 1u);
                                scheduled_saved_registers += (u32)((scheduled_placement.callee_saved_mask >> physical_register) & 1u);
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
                }
                if (!placement.valid)
                {
                    fallback_reason = CODEGEN_FALLBACK_PLACEMENT;
                    if (options.verify_invariants)
                    {
                        fallback_reason = CODEGEN_FALLBACK_VERIFICATION;
                    }
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
                    // the failure is returned without publishing this attempt.
                    result.statistics.exact_attempts += encoded.exact_attempts;
                    result.statistics.exact_successes += encoded.exact_successes;
                    result.statistics.exact_failures += encoded.exact_failures;
                    bool encoded_fits = encoded.valid && buffer.count <= buffer.capacity &&
                                        encoded.byte_count <= buffer.capacity - buffer.count;
                    if (encoded.valid && !encoded_fits)
                    {
                        codegen_buffer_report_exhausted(&buffer);
                    }
                    // A retained encoding still needs valid unwind metadata.
                    // Buffer exhaustion keeps the existing whole-module retry;
                    // abandoned attempts never enter the returned census.
                    fallback_reason = !encoded.valid ? CODEGEN_FALLBACK_ENCODING
                                      : !encoded_fits ? CODEGEN_FALLBACK_OUTPUT_CAPACITY
                                                      : CODEGEN_FALLBACK_UNWIND;
                    if (encoded_fits)
                    {
                        u32 machine_unwind_capacity = 0;
                        if (target.cpu_arch == CPU_ARCH_X86_64)
                        {
                            machine_unwind_capacity = encoded.frame_allocation_offset ? 10u :
                                9u + placement.frame_size / CODEGEN_X64_STACK_PROBE_PAGE +
                                (placement.frame_size % CODEGEN_X64_STACK_PROBE_PAGE != 0);
                        }
                        else
                        {
                            u32 machine_saved_register_count = 0;
                            for (u32 saved_register = 0; saved_register < 32u; saved_register += 1)
                            {
                                machine_saved_register_count += (u32)((placement.callee_saved_mask >> saved_register) & 1u);
                            }
                            u32 machine_frame_total = placement.frame_size + 16u + 8u * machine_saved_register_count;
                            u32 machine_frame_chunks = machine_frame_total / A64_SP_ADJUST_CHUNK +
                                                       (machine_frame_total % A64_SP_ADJUST_CHUNK != 0);
                            machine_unwind_capacity = target_uses_pe_unwind(target)
                                                          ? 22u + machine_saved_register_count
                                                          : 6u + machine_frame_chunks + machine_saved_register_count + function->instruction_count;
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
                            machine_push_count += (u32)((placement.callee_saved_mask >> saved_register) & 1u);
                        }
                        u32 machine_frame_area = placement.frame_size + 8 * machine_push_count;
                        bool machine_windows_frame = selected.function.windows_aarch64_frame;
                        u32 machine_chain_size = machine_windows_frame ? codegen_a64_windows_save_area_size(machine_push_count) : 16u;
                        machine_chain_size += selected.function.windows_aarch64_variadic ? MACHINE_A64_VA_GP_SAVE_BYTES : 0u;
                        u32 machine_frame_total = machine_frame_area + machine_chain_size;
                        bool machine_unwind_valid =
                            codegen_unwind_action_append(descriptor, unwind_action_capacity, 4, CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, machine_chain_size);
                        machine_unwind_valid =
                            codegen_unwind_action_append(descriptor, unwind_action_capacity, 4, CODEGEN_UNWIND_ACTION_SAVE_REGISTER, 29, 0) &&
                            machine_unwind_valid;
                        machine_unwind_valid =
                            codegen_unwind_action_append(descriptor, unwind_action_capacity, 4, CODEGEN_UNWIND_ACTION_SAVE_REGISTER, 30, 8) &&
                            machine_unwind_valid;
                        u32 machine_prologue_cursor;
                        if (machine_windows_frame)
                        {
                            machine_prologue_cursor = 4;
                            u32 save_offset = 16;
                            for (u32 saved_register = 0; saved_register < 32u; saved_register += 1)
                            {
                                if ((placement.callee_saved_mask >> saved_register) & 1u)
                                {
                                    machine_prologue_cursor += 4;
                                    machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                        CODEGEN_UNWIND_ACTION_SAVE_REGISTER, (u8)saved_register,
                                                                                        save_offset) && machine_unwind_valid;
                                    save_offset += 8;
                                }
                            }
                            machine_prologue_cursor += 4;
                            machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                CODEGEN_UNWIND_ACTION_SAVE_REGISTER, 28, save_offset) && machine_unwind_valid;
                            machine_prologue_cursor += 4;
                            machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, 29, 0) && machine_unwind_valid;
                            if (machine_frame_area > A64_SP_ADJUST_CHUNK)
                            {
                                // The shared compact probe keeps SP unchanged
                                // through thirteen words, then allocates once.
                                for (u32 probe_word = 0; probe_word < 13; probe_word += 1)
                                {
                                    machine_prologue_cursor += 4;
                                    machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                        CODEGEN_UNWIND_ACTION_NOP, 0, 0) && machine_unwind_valid;
                                }
                                machine_prologue_cursor += 4;
                                machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                    CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, machine_frame_area) && machine_unwind_valid;
                            }
                            else if (machine_frame_area)
                            {
                                machine_prologue_cursor += 4;
                                machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                    CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, machine_frame_area) && machine_unwind_valid;
                                machine_prologue_cursor += 4;
                                machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                    CODEGEN_UNWIND_ACTION_NOP, 0, 0) && machine_unwind_valid;
                            }
                            machine_prologue_cursor += 4;
                            machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                CODEGEN_UNWIND_ACTION_NOP, 0, 0) && machine_unwind_valid;
                        }
                        else
                        {
                            machine_unwind_valid =
                                codegen_unwind_action_append(descriptor, unwind_action_capacity, 8, CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, 29, 0) &&
                                machine_unwind_valid;
                            machine_prologue_cursor = 8;
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
                            // A large frame materializes the nearby callee-save
                            // base from X29 in two words before the stores.
                            if (machine_frame_area > A64_IMM12_MAX * 8u)
                            {
                                machine_prologue_cursor += 8;
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
                        }
                        for (u32 epilog_index = 0; epilog_index < encoded.epilog_count; epilog_index += 1)
                        {
                            machine_unwind_valid =
                                codegen_epilog_offset_append(descriptor, function->instruction_count, encoded.epilog_offsets[epilog_index]) &&
                                machine_unwind_valid;
                        }
                        if (machine_unwind_valid)
                        {
                            memcpy(buffer.bytes + buffer.count, encoded.bytes, encoded.byte_count);
                            codegen_record_machine_line_marks(program, function, &result, line_entry_capacity, line_source_limit, &selected.function,
                                                              encoded.row_offsets, (u32)buffer.count);
                            for (u32 site_index = 0; site_index < encoded.call_site_count; site_index += 1)
                            {
                                MachineThreadLocalSite thread_local_site = (MachineThreadLocalSite)encoded.call_sites[site_index].thread_local_site;
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = selected.function.call_targets[encoded.call_sites[site_index].target],
                                    .offset = (u32)buffer.count + encoded.call_sites[site_index].code_offset,
                                    .kind = (u8)(encoded.call_sites[site_index].is_thread_local
                                                     ? (thread_local_site == MACHINE_THREAD_LOCAL_SITE_WINDOWS_INDEX
                                                            ? (encoded.call_sites[site_index].thread_local_low
                                                                ? CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_INDEX_LO12
                                                                : CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP)
                                                        : thread_local_site == MACHINE_THREAD_LOCAL_SITE_WINDOWS_OFFSET
                                                            ? CODEGEN_MODULE_RELOCATION_AARCH64_PE_TLS_OFFSET12
                                                        : thread_local_site == MACHINE_THREAD_LOCAL_SITE_DARWIN_DESCRIPTOR
                                                            ? (encoded.call_sites[site_index].thread_local_low
                                                                ? CODEGEN_MODULE_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12
                                                                : CODEGEN_MODULE_RELOCATION_AARCH64_MACH_TLVP_PAGE21)
                                                        : encoded.call_sites[site_index].thread_local_low
                                                            ? CODEGEN_MODULE_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12
                                                            : CODEGEN_MODULE_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12)
                                                     : encoded.call_sites[site_index].page_relative
                                                         ? (encoded.call_sites[site_index].page_low
                                                                ? CODEGEN_MODULE_RELOCATION_AARCH64_MACH_PAGEOFF12
                                                                : CODEGEN_MODULE_RELOCATION_AARCH64_MACH_PAGE21)
                                                     : encoded.call_sites[site_index].absolute ? CODEGEN_MODULE_RELOCATION_ABSOLUTE64
                                                                                               : CODEGEN_MODULE_RELOCATION_AARCH64_CALL26),
                                };
                            }
                            bool machine_inline_relocations_valid =
                                result.relocation_count <= relocation_capacity &&
                                encoded.inline_assembly_relocation_count <= relocation_capacity - result.relocation_count;
                            for (u32 relocation_index = 0;
                                 relocation_index < encoded.inline_assembly_relocation_count && machine_inline_relocations_valid;
                                 relocation_index += 1)
                            {
                                MachineInlineAssemblyRelocation relocation = encoded.inline_assembly_relocations[relocation_index];
                                CodegenModuleRelocationKind kind = CODEGEN_MODULE_RELOCATION_X86_64_PC32;
                                machine_inline_relocations_valid = !relocation.is_block &&
                                    codegen_global_assembly_relocation_kind((AssemblyRelocationKind)relocation.kind, &kind);
                                s64 addend = relocation.addend;
                                if (machine_inline_relocations_valid && kind == CODEGEN_MODULE_RELOCATION_X86_64_PC32)
                                {
                                    machine_inline_relocations_valid = addend <= INT64_MAX - 4;
                                    addend += 4;
                                }
                                IrSymbolId symbol = machine_inline_relocations_valid
                                                        ? codegen_global_assembly_symbol(program, relocation.symbol, target, IR_SYMBOL_DATA)
                                                        : IR_SYMBOL_ID_INVALID;
                                machine_inline_relocations_valid = machine_inline_relocations_valid &&
                                    symbol.value != IR_ID_UNDERLYING_INVALID;
                                if (machine_inline_relocations_valid)
                                {
                                    result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                        .addend = addend,
                                        .symbol = symbol,
                                        .offset = (u32)buffer.count + relocation.offset,
                                        .source = CODEGEN_MODULE_RELOCATION_CODE,
                                        .kind = (u8)kind,
                                    };
                                }
                            }
                            if (machine_inline_relocations_valid)
                            {
                                // Seed storage grows with what recording
                                // actually emits. Reserving the records-times-
                                // rows worst case instead put gigabytes of
                                // unused array in the translation-unit arena
                                // for a few thousand emitted ranges.
                                if (options.debug_info &&
                                    !codegen_record_machine_locations(arena, &result, &debug_location_sink, function, &selected.function,
                                                                      &placement, encoded.row_offsets, (u32)buffer.count,
                                                                      (u32)buffer.count + encoded.byte_count, 0, target))
                                {
                                    result.error = result.error == CODEGEN_ERROR_NONE ? CODEGEN_ERROR_INVALID_IR : result.error;
                                    scratch_end(machine_scratch);
                                    return result;
                                }
                                buffer.count += encoded.byte_count;
                                descriptor->prolog_size = machine_prologue_cursor;
                                descriptor->code_size = (u32)buffer.count - descriptor->code_offset;
                                machine_function_emitted = true;
                                if (label_address_relocation_count)
                                {
                                    machine_block_offsets = arena_allocate(machine_scratch.arena, u32, function->block_count);
                                    memcpy(machine_block_offsets, encoded.block_offsets, sizeof(u32) * function->block_count);
                                }
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
                        if (machine_saves_first && !encoded.frame_pointer_offset)
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
                        if (encoded.frame_allocation_offset)
                        {
                            // The shared Windows probe leaves RSP unchanged
                            // until its final SUB. Consume the actual encoded
                            // offset instead of reconstructing loop lengths.
                            machine_prologue_cursor = encoded.frame_allocation_offset;
                            machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                CODEGEN_UNWIND_ACTION_ALLOCATE_STACK, 0, placement.frame_size) &&
                                                   machine_unwind_valid;
                            machine_frame_remaining = 0;
                        }
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
                        if (encoded.frame_pointer_offset)
                        {
                            machine_prologue_cursor = encoded.frame_pointer_offset;
                            machine_unwind_valid = codegen_unwind_action_append(descriptor, unwind_action_capacity, machine_prologue_cursor,
                                                                                CODEGEN_UNWIND_ACTION_SET_FRAME_POINTER, X64_REGISTER_RBP, 0) &&
                                                   machine_unwind_valid;
                        }
                        if (machine_unwind_valid)
                        {
                            memcpy(buffer.bytes + buffer.count, encoded.bytes, encoded.byte_count);
                            codegen_record_machine_line_marks(program, function, &result, line_entry_capacity, line_source_limit, &selected.function,
                                                              encoded.row_offsets, (u32)buffer.count);
                            for (u32 site_index = 0; site_index < encoded.call_site_count; site_index += 1)
                            {
                                // The encoder says which field of which
                                // sequence each site is; only the ELF
                                // thread-local models have more than one
                                // spelling, and the call beside a
                                // general-dynamic lea is not thread-local at
                                // all -- it resolves to __tls_get_addr.
                                MachineThreadLocalSite thread_local_site =
                                    (MachineThreadLocalSite)encoded.call_sites[site_index].thread_local_site;
                                bool site_is_thread_local = encoded.call_sites[site_index].is_thread_local != 0;
                                // Everything that is not thread-local takes
                                // the reference form the selector wrote beside
                                // the call target, so a GOT load and a PLT
                                // call are told apart here by what the symbol
                                // is rather than by which row emitted them.
                                u32 site_target = encoded.call_sites[site_index].target;
                                u8 site_reference = selected.function.call_target_references
                                                        ? selected.function.call_target_references[site_target]
                                                        : (u8)MACHINE_SYMBOL_REFERENCE_DIRECT;
                                CodegenModuleRelocationKind site_direct_kind =
                                    site_reference == MACHINE_SYMBOL_REFERENCE_GOT   ? CODEGEN_MODULE_RELOCATION_X86_64_GOTPCREL
                                    : site_reference == MACHINE_SYMBOL_REFERENCE_PLT ? CODEGEN_MODULE_RELOCATION_X86_64_PLT32
                                                                                     : CODEGEN_MODULE_RELOCATION_X86_64_PC32;
                                CodegenModuleRelocationKind site_kind =
                                    thread_local_site == MACHINE_THREAD_LOCAL_SITE_TLS_GET_ADDR ? CODEGEN_MODULE_RELOCATION_X86_64_TLS_GET_ADDR_PLT32
                                    : !site_is_thread_local                                     ? site_direct_kind
                                    : thread_local_site == MACHINE_THREAD_LOCAL_SITE_WINDOWS_INDEX ? CODEGEN_MODULE_RELOCATION_X86_64_PE_TLS_INDEX_PC32
                                    : target.os == OPERATING_SYSTEM_WINDOWS                     ? CODEGEN_MODULE_RELOCATION_PE_TLS_OFFSET32
                                    : (target.os == OPERATING_SYSTEM_MACOS || target.os == OPERATING_SYSTEM_IOS)
                                        ? CODEGEN_MODULE_RELOCATION_X86_64_MACH_TLV_PC32
                                    : thread_local_site == MACHINE_THREAD_LOCAL_SITE_INITIAL_EXEC    ? CODEGEN_MODULE_RELOCATION_X86_64_GOTTPOFF
                                    : thread_local_site == MACHINE_THREAD_LOCAL_SITE_GENERAL_DYNAMIC ? CODEGEN_MODULE_RELOCATION_X86_64_TLSGD
                                                                                                     : CODEGEN_MODULE_RELOCATION_X86_64_TPOFF32;
                                result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                    .symbol = selected.function.call_targets[site_target],
                                    .offset = (u32)buffer.count + encoded.call_sites[site_index].code_offset,
                                    .kind = (u8)site_kind,
                                };
                            }
                            bool machine_inline_relocations_valid =
                                result.relocation_count <= relocation_capacity &&
                                encoded.inline_assembly_relocation_count <= relocation_capacity - result.relocation_count;
                            for (u32 relocation_index = 0;
                                 relocation_index < encoded.inline_assembly_relocation_count && machine_inline_relocations_valid;
                                 relocation_index += 1)
                            {
                                MachineInlineAssemblyRelocation relocation = encoded.inline_assembly_relocations[relocation_index];
                                CodegenModuleRelocationKind kind = CODEGEN_MODULE_RELOCATION_X86_64_PC32;
                                machine_inline_relocations_valid = !relocation.is_block &&
                                    codegen_global_assembly_relocation_kind((AssemblyRelocationKind)relocation.kind, &kind);
                                s64 addend = relocation.addend;
                                if (machine_inline_relocations_valid && kind == CODEGEN_MODULE_RELOCATION_X86_64_PC32)
                                {
                                    machine_inline_relocations_valid = addend <= INT64_MAX - 4;
                                    addend += 4;
                                }
                                IrSymbolId symbol = machine_inline_relocations_valid
                                                        ? codegen_global_assembly_symbol(program, relocation.symbol, target, IR_SYMBOL_DATA)
                                                        : IR_SYMBOL_ID_INVALID;
                                machine_inline_relocations_valid = machine_inline_relocations_valid &&
                                    symbol.value != IR_ID_UNDERLYING_INVALID;
                                if (machine_inline_relocations_valid)
                                {
                                    result.relocations[result.relocation_count++] = (CodegenModuleRelocation){
                                        .addend = addend,
                                        .symbol = symbol,
                                        .offset = (u32)buffer.count + relocation.offset,
                                        .source = CODEGEN_MODULE_RELOCATION_CODE,
                                        .kind = (u8)kind,
                                    };
                                }
                            }
                            if (machine_inline_relocations_valid)
                            {
                                u32 machine_frame_base_offset = encoded.frame_pointer_offset ? placement.frame_size : 0;
                                // Same emitted-range sizing as the AArch64 path
                                // above.
                                if (options.debug_info &&
                                    !codegen_record_machine_locations(arena, &result, &debug_location_sink, function, &selected.function,
                                                                      &placement, encoded.row_offsets, (u32)buffer.count,
                                                                      (u32)buffer.count + encoded.byte_count, machine_frame_base_offset, target))
                                {
                                    result.error = result.error == CODEGEN_ERROR_NONE ? CODEGEN_ERROR_INVALID_IR : result.error;
                                    scratch_end(machine_scratch);
                                    return result;
                                }
                                buffer.count += encoded.byte_count;
                                descriptor->prolog_size = machine_prologue_cursor;
                                descriptor->code_size = (u32)buffer.count - descriptor->code_offset;
                                machine_function_emitted = true;
                                if (label_address_relocation_count)
                                {
                                    machine_block_offsets = arena_allocate(machine_scratch.arena, u32, function->block_count);
                                    memcpy(machine_block_offsets, encoded.block_offsets, sizeof(u32) * function->block_count);
                                }
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
            }
            if (machine_function_emitted)
            {
                result.statistics.mutable_virtual_register_count += selected.mutable_virtual_register_count;
            }
            scratch_end(machine_scratch);
        }
        if (machine_function_emitted)
        {
            for (u32 side_index = 0; side_index < label_address_relocation_count; side_index += 1)
            {
                CodegenModuleRelocation* relocation = result.relocations + label_address_relocation_indices[side_index];
                if (relocation->symbol.value != function->symbol.value)
                {
                    continue;
                }
                if (!machine_block_offsets || relocation->label_block.value >= function->block_count)
                {
                    result.error = CODEGEN_ERROR_INVALID_IR;
                    return result;
                }
                s64 block_addend = (s64)machine_block_offsets[relocation->label_block.value];
                if (block_addend > 0 && relocation->addend > INT64_MAX - block_addend)
                {
                    result.error = CODEGEN_ERROR_CAPACITY;
                    return result;
                }
                if (block_addend < 0 && relocation->addend < INT64_MIN - block_addend)
                {
                    result.error = CODEGEN_ERROR_CAPACITY;
                    return result;
                }
                relocation->addend += block_addend;
                relocation->label_address = false;
            }
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
        if (buffer.error != CODEGEN_ERROR_NONE)
        {
            result.failure_reason = codegen_fallback_reason_string(fallback_reason);
            result.error = buffer.error;
            return result;
        }
        result.failed_opcode = fallback_opcode;
        result.failure_reason = codegen_fallback_reason_string(fallback_reason);
        if (fallback_opcode < IR_OPCODE_COUNT)
        {
            for (u32 instruction_index = 0; instruction_index < function->instruction_count; instruction_index += 1)
            {
                if (function->instructions[instruction_index].opcode == fallback_opcode)
                {
                    result.failed_instruction = (IrInstructionId){.value = instruction_index};
                    break;
                }
            }
        }
        result.error = fallback_reason == CODEGEN_FALLBACK_SIGNATURE ? CODEGEN_ERROR_UNSUPPORTED_ABI
                       : fallback_reason == CODEGEN_FALLBACK_VERIFICATION ? CODEGEN_ERROR_INVALID_IR
                       : fallback_reason == CODEGEN_FALLBACK_PLACEMENT || fallback_reason == CODEGEN_FALLBACK_OUTPUT_CAPACITY
                           ? CODEGEN_ERROR_CAPACITY
                           : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
        return result;
    }
    for (u32 relocation_index = 0; relocation_index < result.relocation_count; relocation_index += 1)
    {
        if (result.relocations[relocation_index].label_address)
        {
            result.error = CODEGEN_ERROR_INVALID_IR;
            return result;
        }
    }
    if (target.cpu_arch == CPU_ARCH_X86_64 && module->assembly_count)
    {
        codegen_buffer_ensure_x64_metadata_cache(&buffer, &x64_metadata_cache_tried, arena, module->function_count);
    }
    u32 assembly_function_begin = result.function_count;
    for (u32 assembly_index = 0; assembly_index < module->assembly_count; assembly_index += 1)
    {
        u32 failed_line = 0;
        if (!codegen_emit_global_assembly(arena, program, module->assemblies[assembly_index], target, options, &buffer, &result, relocation_capacity,
                                           &failed_line))
        {
            result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
            result.failed_in_assembly = true;
            result.failed_assembly = assembly_index;
            result.failed_assembly_line = failed_line;
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
    result.assembly_function_count = result.function_count - assembly_function_begin;
    result.code = (ByteSlice){
        .pointer = buffer.bytes,
        .length = buffer.count,
    };
    result.statistics.code_bytes = result.code.length;
    result.error = buffer.error;
    return result;
}

CodegenModule codegen_generate_canonical_module_with_trace(Arena* arena, IrProgram* program, IrModule* module, Target target, CodegenModuleOptions options, BootstrapTrace* bootstrap_trace)
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
    if (options.register_allocator >= CODEGEN_REGISTER_ALLOCATOR_MODE_COUNT)
    {
        result.error = CODEGEN_ERROR_INVALID_IR;
        return result;
    }
    // NONE is retained as a command-line/API compatibility spelling. It no
    // longer exposes the direct emitter: its deliberately low-complexity
    // meaning is the machine selector plus MIR_STACK placement.
    if (options.register_allocator == CODEGEN_REGISTER_ALLOCATOR_NONE)
    {
        options.register_allocator = CODEGEN_REGISTER_ALLOCATOR_MIR_STACK;
    }
    codegen_prewarm_for_target(target);
    // Reserve the active ABI contexts before retries. Classification stays lazy,
    // but filling a reserved page retains no allocation from an attempt that
    // code-buffer growth might rewind. Explicit function conventions reserve
    // their own contexts too; language type records remain untouched.
    ir_prepare_program_abi(program, codegen_canonical_ir_abi_convention(result.abi));
    IrValidationResult validation = ir_prepare_canonical_module(program, module, options.assume_validated && !options.verify_invariants);
    if (validation.error != IR_VALIDATION_NONE)
    {
        result.error = CODEGEN_ERROR_INVALID_IR;
        return result;
    }
    // The ELF default call model depends on whether a symbol has a definition
    // in this module. Module-level assembly is emitted after the functions,
    // so publish its label definitions first to keep that decision stable
    // across repeated codegen passes and allocator modes.
    if (target.cpu_arch == CPU_ARCH_X86_64 && object_format_for_target(target) == OBJECT_FORMAT_ELF64)
    {
        for (u32 assembly_index = 0; assembly_index < module->assembly_count; assembly_index += 1)
        {
            if (!codegen_global_assembly_predeclare_labels(program, target, module->assemblies[assembly_index].source))
            {
                result.error = CODEGEN_ERROR_CAPACITY;
                return result;
            }
        }
    }
    // Function emission uses the machine encoder. Module-level assembly
    // allocates its encoding cache lazily inside each attempt.
    CodegenX64MetadataCache* x64_metadata_cache = 0;
    // The capacity estimate's type table is invariant across retries.
    CodegenSlotCost* slot_costs = codegen_slot_costs_build(arena, &program->types);
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
    // The machine selectors' module context — the per-type projection and
    // the x86-64 per-signature call plans: like the caches above it depends
    // on the program and the target alone, so it is built once here, outside
    // the attempt scope, rather than by every function or every attempt.
    MachineSelectionModule* machine_module = 0;
    if (target.cpu_arch == CPU_ARCH_X86_64)
    {
        machine_module = machine_select_module_prepare(arena, program, target);
    }
    TemporalArena attempt_scope = arena_begin_temporal(arena);
    for (u64 capacity_scale = 1;; capacity_scale *= 2)
    {
        bool code_buffer_exhausted = false;
        if (bootstrap_trace)
        {
            bootstrap_trace_string(bootstrap_trace, S8("codegen attempt"));
            bootstrap_trace_u64(bootstrap_trace, capacity_scale);
        }
        result = codegen_generate_canonical_module_attempt(arena, program, slot_costs, module, target, options, capacity_scale,
                                                           &code_buffer_exhausted, x64_metadata_cache, machine_module, bootstrap_trace);
        // Every other capacity failure -- a frame displacement out of range, a
        // frame past `UINT32_MAX`, a reserve that cannot be addressed -- is one
        // more room cannot fix, and is reported as it stands.
        if (!code_buffer_exhausted)
        {
            result.statistics.verified_ir_module_count = options.verify_invariants ? 1 : 0;
            if (result.error != CODEGEN_ERROR_NONE)
            {
                // The complete module is the publication boundary. A later
                // function, unwind record, relocation, or assembly block may
                // refuse after earlier functions staged valid-looking data.
                // Retain diagnostics and attempted-work counters only.
                result = (CodegenModule){
                    .ir_module = module,
                    .abi = result.abi,
                    .position_independent = result.position_independent,
                    .error = result.error,
                    .failure_reason = result.failure_reason,
                    .statistics = result.statistics,
                    .failed_function = result.failed_function,
                    .failed_instruction = result.failed_instruction,
                    .failed_opcode = result.failed_opcode,
                    .failed_machine_verification = result.failed_machine_verification,
                    .failed_machine_scheduled = result.failed_machine_scheduled,
                    .first_fallback_function = result.first_fallback_function,
                    .first_fallback_opcode = result.first_fallback_opcode,
                    .failed_assembly = result.failed_assembly,
                    .failed_assembly_line = result.failed_assembly_line,
                    .failed_in_assembly = result.failed_in_assembly,
                };
                result.statistics.code_bytes = 0;
            }
            if (result.error == CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION && result.failed_opcode == IR_OPCODE_SIMD)
            {
                result.failure_reason = S8("exact SIMD intrinsic requires a supported target and feature set; select an explicit source fallback");
            }
            return result;
        }
        scratch_end(attempt_scope);
    }
}

CodegenModule codegen_generate_canonical_module(Arena* arena, IrProgram* program, IrModule* module, Target target, CodegenModuleOptions options)
{
    return codegen_generate_canonical_module_with_trace(arena, program, module, target, options, 0);
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
    if (function.read_only_data.length)
    {
        memcpy((u8*)address + data_offset, function.read_only_data.pointer, function.read_only_data.length);
    }
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
