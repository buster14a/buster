// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Included after assembler_corpus_test.c. #1115's whole-program corpus: ordinary C programs (examples/assembler/
// program-*.c: floating point of every width, thread-local storage of every model with threads, common and static
// zero-initialized storage, wide string literals, dense switches' jump tables, gathers) that GCC and Clang compile to
// assembly, at -O0, -O2, -O2 -fPIC, -O3 -march=x86-64-v4, and -O0 and -O2 with -g (whose DWARF leaves its line table
// and view numbers to the assembler), in AT&T and Intel syntax. Each source must assemble by both paths into the same
// object, by the `assemble` command too, into GNU as's sections, symbols and relocations (the existing comparator's);
// and the program linked from it must print what the one linked from GNU as's object prints, and succeed. Every refusal
// is then a named gap of the compilers' ordinary output, rather than one a user meets. The x86-64-v4 programs run where
// the host has every feature of x86-64-v4; elsewhere their objects are still compared.

// #1115's fixtures, each compared with GNU as by --cross-check: the TLS models' relocation modifiers in both cases and
// syntaxes, on instructions and on .long (with GNU as's prefixes written as statements, data16 and rex64, and its
// _GLOBAL_OFFSET_TABLE_); .comm and .lcomm, global and local, with and without alignments, after .bss's own contents;
// merged strings of 2- and 4-byte characters as GCC and clang write them; the x87's stack registers, with GNU as's
// implicit operands, LLVM's fucompi and AT&T's reversed subtractions and divisions; and the compare-predicate names.
static const char asm_test_tls_fixture[] =
    ".text\ntls:\nmovl %fs:tl@TPOFF, %esi\nmovl %fs:tl@tpoff+4, %esi\nleaq tl@TPOFF(%rax), %rax\nmovq $tl@tpoff, %rax\naddq $tl@tpoff, %rax\n"
    // The 64-bit offsets (R_X86_64_TPOFF64 and DTPOFF64): a 64-bit immediate's, and data's below.
    "movabsq $tl@tpoff, %rax\nmovabsq $tl@dtpoff+4, %rdx\nmovabsq $ext@TPOFF, %rcx\n"
    "movq ext@GOTTPOFF(%rip), %rax\naddq ext@gottpoff(%rip), %rax\n"
    "data16\nleaq ext@TLSGD(%rip), %rdi\ndata16\ndata16\nrex64\ncall __tls_get_addr@PLT\n"
    "data16 leaq ext@tlsgd(%rip), %rdi\n.value 0x6666\nrex64\ncall __tls_get_addr@PLT\n"
    "leaq tl@tlsld(%rip), %rdi\ncall __tls_get_addr@PLT\nmovl tl@dtpoff(%rax), %eax\nleaq tl@DTPOFF+8(%rax), %rdx\naddq $tl@dtpoff, %rax\n"
    ".intel_syntax noprefix\nmov eax, dword ptr fs:[stl@TPOFF]\nmov esi, DWORD PTR fs:tl@tpoff\nlea rdi, stl@tlsld[rip]\nadd eax, DWORD PTR stl@dtpoff[rbx]\n"
    "data16 lea rdi, ext@tlsgd[rip]\nmov rax, QWORD PTR ext@gottpoff[rip]\n.att_syntax prefix\n"
    // A TLS modifier through an alias: of an undefined symbol (which becomes STT_TLS), and of a label of .tbss.
    ".set tls_alias, tls_alias_target\nmovq %fs:tls_alias@TPOFF, %rax\nmovq tls_alias@GOTTPOFF(%rip), %rax\nleaq tls_alias@TLSGD(%rip), %rdi\n"
    "leaq tls_alias@TLSLD(%rip), %rdi\nmovl tls_alias@DTPOFF(%rax), %eax\n.set tls_local_alias, tl+4\nmovl %fs:tls_local_alias@tpoff, %eax\n.p2align 4\n"
    ".section .rodata.offsets,\"a\",@progbits\n.long tl@dtpoff\n.long stl@DTPOFF+4\n.long tl@tpoff\n.long tls_alias@DTPOFF\n"
    ".quad tl@dtpoff\n.quad tl@tpoff+8\n.quad ext@DTPOFF\n.quad tls_alias@dtpoff\n"
    ".section .tbss,\"awT\",@nobits\n.type tl, @object\ntl: .zero 16\n.local stl\nstl: .zero 4\n"
    ".section .tdata,\"awT\",@progbits\n.globl data\ndata: .long 7\n";
static const char asm_test_common_fixture[] =
    ".text\ncommon: movl got(%rip), %eax\nmovl lc(%rip), %eax\nmovl loc(%rip), %eax\n"
    ".comm got,4,4\n.comm big,200,32\n.comm noalign,24\n.comm one,1\n.comm three,3\n.comm odd,100\n.comm empty,0\n.comm got,4,4\n"
    ".local loc\n.comm loc,8,8\n.local unaligned\n.comm unaligned,5\n.lcomm lc,12\n.lcomm lc2,5\n.lcomm lc3,100\n.lcomm byte,1\n"
    ".bss\n.zero 3\nafter_bss: .zero 1\n.data\n.long got\n.quad big+8\n"
    // A global's repeated .comm, as GNU as takes it: the first size, the last alignment (its default, by the kept size).
    ".comm ra,4,4\n.comm ra,4,16\n.comm rb,4,16\n.comm rb,4,4\n.comm rc,4,4\n.comm rc,8,16\n.comm rd,8,8\n.comm rd,4,2\n.comm re,4,8\n.comm re,4\n"
    ".comm rf,4\n.comm rf,4,32\n.comm rg,24\n.comm rg,24\n.comm rh,4,4\n.comm rh,4,0\n.comm rx,4,4\n.comm rx,100\n.comm ry,100,64\n.comm ry,2\n"
    ".comm rz,0,8\n.comm rz,0\n"
    // A global common made local and declared again, which GNU as places in .bss (subsection 1, after .lcomm's) at its
    // new alignment's place plus its first size, of that first size: by .comm with the same size, a larger one, none
    // and no alignment, and by .lcomm; then one made local without being declared again, which stays a global common.
    ".text\nmovl ma(%rip), %eax\nmovl mf(%rip), %eax\n.comm ma,4,4\n.local ma\n.comm ma,4,4\n.comm mb,4,4\n.local mb\n.comm mb,8,8\n"
    ".comm mc,4,4\n.local mc\n.lcomm mc,4\n.comm md,4,4\n.local md\n.comm md,4\n.comm me,24,4\n.local me\n.comm me,24,8\n"
    ".comm mf,4,4\n.local mf\n.lcomm mg,3\n.data\n.quad me+4\n";
static const char asm_test_wide_fixture[] =
    ".section .rodata.str4.8,\"aMS\",@progbits,4\n.align 8\n.LC2:\n.string \"h\"\n.string \"\"\n.string \"\"\n.string \"i\"\n.string \"\"\n.string \"\"\n"
    ".string \"\"\n.string \"\"\n.string \"\"\n.string \"\"\n"
    ".section .rodata.str4.4,\"aMS\",@progbits,4\n.p2align 2, 0x0\n.L.str:\n.long 104\n.long 101\n.long 0\n"
    ".section .rodata.str2.2,\"aMS\",@progbits,2\n.p2align 1, 0x0\n.L.str.1:\n.short 115\n.short 0\n"
    ".text\nwide: leaq .LC2(%rip), %rax\nleaq .L.str+4(%rip), %rcx\nleaq .L.str.1(%rip), %rdx\nret\n.data\n.quad .L.str\n";
static const char asm_test_x87_fixture[] =
    ".text\nx87:\nfsub %st, %st(1)\nfsubp %st, %st(1)\nfsubr %st, %st(1)\nfsubrp %st, %st(1)\nfdiv %st, %st(1)\nfdivp %st, %st(1)\n"
    "fdivr %st, %st(1)\nfdivrp %st, %st(1)\nfsub %st(1), %st\nfsubr %st(1), %st\nfadd %st(0), %st\nfadd %st, %st(0)\nfsub %st, %st(0)\n"
    "fsubr %st, %st(0)\nfdiv %st(0), %st\nfmulp %st, %st(1)\nfaddp %st, %st(1)\nfsubp\nfsubrp\nfdivp\nfdivrp\nfaddp\nfmulp\nfxch\nfxch %st(2)\n"
    "fld %st(0)\nfld %st(7)\nfst %st(3)\nfstp %st(1)\nfstp %st\nfucomip %st(1), %st\nfucompi %st(1), %st\nfcompi %st(2), %st\nfcomi\nfcomip\n"
    "fucomi %st(3)\nfcmovne %st(1), %st\nfcmovb %st(2), %st\nfcmovnbe %st(7), %ST\nfsubp %st(2)\nfsubrp %st(2)\nfadd %st(2)\nfdivr %st(3)\n"
    "fcom\nfcomp\nfucom\nfucomp\nfcom %st(4)\nfucomp %st(5)\nffree %st(1)\nffreep %st(2)\n"
    // GNU as's translations (which it warns of): the arithmetic without operands is the pop, and faddp's and fmulp's
    // ST(i) source the destination.
    "fadd\nfmul\nfsub\nfsubr\nfdiv\nfdivr\nfaddp %st(1), %st\nfmulp %st(1), %st\nfaddp %st(3), %st\nfaddp %st(0), %st\n"
    ".intel_syntax noprefix\nfsub st(1), st\nfsubp st(1), st\nfsubr st(1), st\nfsubrp st(1), st\nfsub st, st(1)\nfdivp st(1), st\nfdivrp st(1), st\n"
    "fmulp st(1), st\nfxch st(1)\nfld st\nfstp st(0)\nfucomip st, st(1)\nfsubp\nfdivrp\nfadd st, st(5)\nfcmove st, st(3)\nfaddp st, st(1)\n"
    "fmulp st, st(2)\n";
static const char asm_test_predicate_fixture[] =
    ".text\npredicates:\ncmpltsd %xmm2, %xmm1\ncmpeqss (%rax), %xmm0\ncmpunordps %xmm1, %xmm2\ncmpnlepd %xmm3, %xmm4\ncmpordsd %xmm5, %xmm6\n"
    "cmpneqss %xmm7, %xmm8\ncmpnltpd 16(%rdi), %xmm9\ncmplesd %xmm10, %xmm11\n"
    "vcmpltsd %xmm2, %xmm1, %xmm0\nvcmpge_oqps %ymm1, %ymm2, %ymm3\nvcmptrue_usps %ymm1, %ymm2, %ymm3\nvcmpeq_oqsd %xmm1, %xmm2, %xmm3\n"
    "vcmpnge_uqpd (%rax), %ymm4, %ymm5\nvcmpfalsess %xmm1, %xmm2, %xmm3\nvcmpneq_oqpd %zmm1, %zmm2, %k1{%k2}\nvcmpunordps (%rax){1to16}, %zmm1, %k3\n"
    "vpcmpltd %zmm1, %zmm2, %k1\nvpcmpnleuq %zmm1, %zmm2, %k3{%k4}\nvpcmpequb %xmm1, %xmm2, %k1\nvpcmpneqw %ymm1, %ymm2, %k2\nvpcmpeqd %zmm1, %zmm2, %k1\n"
    ".intel_syntax noprefix\ncmpltsd xmm1, xmm2\ncmpnless xmm1, dword ptr [rax]\nvcmpltsd xmm0, xmm1, xmm2\nvpcmpltd k1, zmm2, zmm1\nvcmpgtps k1{k2}, zmm1, zmm2\n";

// Branch relaxation in GNU as's order (relax_segment): a backward jmp whose distance shrinks as an earlier jmp grows
// and a max-skip alignment pads less, the GCC 16 -O2 pattern that a round-based layout grew; a forward jmp across an
// alignment that absorbs an earlier jmp's growth; and a forward jmp that 45 grown jmps before it would see behind it,
// which waits for the next pass. Alignments fill with int3, as GNU as's code padding differs between binutils versions.
// `.set` aliases' types and sizes, as GNU as copies them (copy_symbol_attributes): a weak alias of a function (GCC's
// `__attribute__((weak, alias("original")))`), an alias of a label typed only after it (which isn't copied), of a
// function defined after it, a chain declared before its target, an object's, one whose own .size replaces the copied
// one, an .lcomm object's and a local .comm one's; and sizes that reach past their section's end, which GNU as
// writes: an alias of an object past its start (`tail`, which takes the object's size), and a `.size` beyond it.
static const char asm_test_alias_fixture[] =
    ".text\n.globl original\n.type original,@function\noriginal: mov $42,%eax\nret\n.size original,.-original\n.weak alias\n"
    ".set alias,original\n.globl untyped\nuntyped: ret\n.set early,untyped\n.globl early\n.type untyped,@function\n"
    ".size untyped,.-untyped\n.set forward,later\n.globl forward\n.type later,@function\nlater: ret\n.size later,.-later\n"
    ".set second,first\n.set first,original\n.globl first\n.globl second\n"
    ".data\nobj: .long 1\n.type obj,@object\n.size obj,4\n.set oalias,obj\n.globl oalias\n.set salias,obj\n.size salias,2\n"
    ".globl salias\n.lcomm buf,16\n.set balias,buf\n.globl balias\n.local lbuf\n.comm lbuf,24,8\n.set lalias,lbuf\n.globl lalias\n"
    ".section .rodata.tail,\"a\",@progbits\n.globl whole\n.type whole,@object\nwhole: .long 1\n.size whole,4\n.globl tail\n.set tail,whole+2\n"
    ".globl short\nshort: .long 2\n.size short, 8\n";
#define BUSTER_ASM_TEST_FAR5 "jmp .Lh_far\njmp .Lh_far\njmp .Lh_far\njmp .Lh_far\njmp .Lh_far\n"
static const char asm_test_relax_fixture[] =
    ".section .text.backward,\"ax\",@progbits\nbackward: jmp .Lb_far\n.skip 13, 0x90\n.Lb_target: .skip 7, 0x90\n.p2align 4, 0xcc, 10\n"
    ".skip 110, 0x90\njmp .Lb_target\n.skip 200, 0x90\n.Lb_far: ret\n"
    ".section .text.forward,\"ax\",@progbits\nforward: jmp .Lf_far\n.skip 12, 0x90\njmp .Lf_target\n.skip 120, 0x90\n.p2align 4, 0xcc\n"
    ".Lf_target: .skip 200, 0x90\n.Lf_far: ret\n"
    ".section .text.hold,\"ax\",@progbits\nhold:\n" BUSTER_ASM_TEST_FAR5 BUSTER_ASM_TEST_FAR5 BUSTER_ASM_TEST_FAR5 BUSTER_ASM_TEST_FAR5
    BUSTER_ASM_TEST_FAR5 BUSTER_ASM_TEST_FAR5 BUSTER_ASM_TEST_FAR5 BUSTER_ASM_TEST_FAR5 BUSTER_ASM_TEST_FAR5
    "jmp .Lh_target\n.p2align 4, 0xcc\n.Lh_target: .skip 200, 0x90\n.Lh_far: ret\n";
#undef BUSTER_ASM_TEST_FAR5

// Random relaxation programs (#1115): labels, jmp and jcc to any of them, filler, and alignments with and without a
// most padding, filled with int3, so that each branch's size is GNU as's pass order's. --cross-check compares each
// with GNU as; otherwise both paths assemble each into the same object.
static void asm_test_relax_programs(AsmTest* test, const char* directory, bool cross_check)
{
    static const char* const branches[] = { "jmp", "je", "jne", "jl", "jg", "jb", "ja" };
    char source[16384];
    uint64_t state = 1115;
    for (uint32_t program = 0; program < 300; program += 1)
    {
        const uint32_t labels = 4 + (uint32_t)(asm_test_random(&state) % 37);
        int length = snprintf(source, sizeof(source), ".text\nrelax:\n");
        for (uint32_t label = 0; label < labels; label += 1)
        {
            length += snprintf(source + length, sizeof(source) - length, ".Lr%u:\n", label);
            const uint32_t statements = (uint32_t)(asm_test_random(&state) % 4);
            for (uint32_t i = 0; i < statements; i += 1)
            {
                const uint32_t kind = (uint32_t)(asm_test_random(&state) % 20);
                const uint32_t power = 2 + (uint32_t)(asm_test_random(&state) % 4);
                const uint32_t most = (uint32_t)(asm_test_random(&state) % (1u << power));
                const bool limited = asm_test_random(&state) % 10 < 7;
                if (kind < 9)
                {
                    length += snprintf(source + length, sizeof(source) - length, "%s .Lr%u\n", branches[asm_test_random(&state) % BUSTER_ARRAY_LENGTH(branches)], (uint32_t)(asm_test_random(&state) % labels));
                }
                else if (kind < 14)
                {
                    length += snprintf(source + length, sizeof(source) - length, ".skip %u, 0x90\n", 1 + (uint32_t)(asm_test_random(&state) % 90));
                }
                else if (kind < 17 && limited)
                {
                    length += snprintf(source + length, sizeof(source) - length, ".p2align %u, 0xcc, %u\n", power, most);
                }
                else if (kind < 17)
                {
                    length += snprintf(source + length, sizeof(source) - length, ".p2align %u, 0xcc\n", power);
                }
                else
                {
                    length += snprintf(source + length, sizeof(source) - length, "addl $1, %%eax\n");
                }
            }
        }
        snprintf(source + length, sizeof(source) - length, "ret\n");
        if (cross_check)
        {
            if (!asm_test_oracle(test, source, directory, X86_ARCH_BASELINE))
            {
                fprintf(stderr, "assembler relaxation program %u:\n%s", program, source);
            }
        }
        else
        {
            asm_test_pair(test, (String8){ .pointer = source, .length = strlen(source) }, 1, X86_ARCH_BASELINE);
        }
    }
}

// A jump table as GCC's -O2 PIE code writes one (#1115): label differences whose subtrahend is the table's own label in
// .rodata and whose minuend is .text's, which GNU as makes PC-relative relocations of the minuend; with an undefined
// minuend, `.`, an addend, and `.quad`'s PC64.
static const char asm_test_jump_fixture[] =
    ".text\n.globl dispatch\ndispatch: cmpl $3, %edi\nja .Ldef\nleaq .Ltab(%rip), %rdx\nmovslq (%rdx,%rdi,4), %rax\naddq %rdx, %rax\n"
    "notrack jmp *%rax\n.L1: movl $1, %eax\nret\n.L2: movl $2, %eax\nret\n.Ldef: xorl %eax, %eax\nret\n"
    ".section .rodata\n.align 4\n.Ltab: .long .L1-.Ltab\n.long .L2-.Ltab\n.long .Ldef-.Ltab\n.long ext-.Ltab\n.long ext-.\n"
    ".long .L2-.Ltab+8\n.quad .L1-.\n.quad dispatch-.Ltab\n";
// Undefined symbols as GNU as lists them (#1115): each named one is a global, from .globl, a TLS or GOT reference, an
// expression, an alias's target, .hidden, .protected, .internal, .size, .type and even .local, keeping its .type and
// .size; a weak one only when something refers to it. An alias of an undefined symbol, exported or not, isn't listed:
// its references are its target's, whose binding is its own (`wtarget` stays weak), and a chain resolves through each.
static const char asm_test_undefined_fixture[] =
    ".text\n.globl alias1\n.set alias1, ext1\ncall alias1\nmovq alias1@GOTPCREL(%rip), %rax\n.weak walias\n.set walias, ext2\n"
    ".globl galias\n.type galias,@function\n.set galias, ext3\n.size galias, 8\n.globl unused\n.set unused, ext4\ncall walias\n"
    ".set quiet, ext5\n.weak wtarget\n.globl alias6\n.set alias6, wtarget\ncall alias6\n.globl halias\n.hidden halias\n"
    ".set halias, ext6\n.local lalias\n.set lalias, ext7\n.globl declared\n.weak wunused\n.hidden hidden_undefined\n"
    ".protected protected_undefined\n.internal internal_undefined\n.size sized_undefined, 4\n.type typed_undefined, @function\n"
    ".local local_undefined\n.set chain_b, chain_a\n.set chain_a, ext13 + 4\n.data\n.long ext14 - ext14\n.quad alias1+4\n.quad chain_b\n";

// A section's name as a symbol (clang's DWARF writes `.long .debug_abbrev`), which GNU as writes as that section's
// symbol, not an undefined one, and of no symbol of its own: as data, with an addend, and RIP-relative.
static const char asm_test_section_name_fixture[] =
    ".section .foo,\"a\",@progbits\n.byte 1\n.section .bar,\"a\",@progbits\n.quad .foo\n.long .foo+1\n.text\nleaq .foo(%rip), %rax\n"
    ".section .debug_abbrev,\"\",@progbits\n.byte 0\n.section .debug_aranges,\"\",@progbits\n.long .debug_abbrev\n";

// String instructions' explicit operands, as GNU as takes them (#1115: clang's `rep;movsq (%rsi), %es:(%rdi)`, and
// Intel's `rep movsb es:[rdi], [rsi]`): each instruction's in both syntaxes, a source's segment (DS's as none), 32-bit
// addresses, sizes from a register or `ptr`, with rep and a rep statement, and SSE's movsd beside them; and .zero's
// fill (clang's `.zero 16,63`).
static const char asm_test_string_fixture[] =
    ".text\nstrings:\nmovsq (%rsi), %es:(%rdi)\nmovsq %ds:(%rsi), %es:(%rdi)\nmovsq %fs:(%rsi), %es:(%rdi)\nmovsq (%esi), %es:(%edi)\n"
    "stos %eax, %es:(%rdi)\nstosl %eax, (%rdi)\nlods (%rsi), %ax\nlodsb %fs:(%rsi), %al\nscasq %es:(%rdi), %rax\ncmpsl %es:(%rdi), (%rsi)\n"
    "insb (%dx), %es:(%rdi)\ninsl %dx, %es:(%rdi)\noutsw %gs:(%rsi), (%dx)\nrep movsq %fs:(%rsi), %es:(%rdi)\nrep movsq %fs:(%esi), %es:(%edi)\n"
    "rep stosw %ax, %es:(%edi)\nrepne scasb %es:(%rdi), %al\nrep;movsb (%rsi), %es:(%rdi)\nrep;stosb %al, %es:(%rdi)\nmovsb %cs:(%rsi), %es:(%rdi)\n"
    "movsb %es:(%rsi), %es:(%rdi)\nmovsb %ss:(%rsi), (%rdi)\nmovsd %xmm0, %xmm1\nmovsd (%rsi), %xmm1\n.intel_syntax noprefix\n"
    "rep movsb es:[rdi], [rsi]\nrep stosb es:[rdi], al\nmovs qword ptr es:[rdi], qword ptr fs:[rsi]\nlods eax, dword ptr [rsi]\n"
    "cmps byte ptr [rsi], byte ptr es:[rdi]\nscasb al, byte ptr es:[rdi]\nmovsd dword ptr es:[rdi], dword ptr [rsi]\noutsb dx, byte ptr [rsi]\n"
    "movsd xmm0, qword ptr [rsi]\n.att_syntax prefix\n.data\n.zero 16,63\n.zero 3\n.zero 2,0x90\n";
// AVX's vmovq between memory and an XMM register in its own form where VEX's three bytes make the general register's
// as long (#1115), and AVX-512's in the general register's, as GNU as chooses them.
static const char asm_test_vmovq_fixture[] =
    ".text\nvmovq_forms:\nvmovq %xmm1, (%r14,%rcx)\nvmovq (%r13,%rax), %xmm1\nvmovq %xmm9, (%rax)\nvmovq (%rax), %xmm9\nvmovq %xmm1, (%rax)\nvmovq %rax, %xmm9\n"
    "vmovq %xmm9, %r10\nvmovq %xmm9, %xmm2\nvmovq %xmm17, (%r14)\nvmovq (%r14), %xmm17\n.intel_syntax noprefix\nvmovq qword ptr [r14+rcx], xmm1\n";

// AVX2's and AVX-512's gathers and scatters, of vector-indexed (VSIB) memory (#1115: clang's `vpgatherqd
// 32(%rdx,%ymm7), %xmm8 {%k1}`): each index class, with and without a base, a displacement and a scale, high
// registers, in both syntaxes (Intel's vector index wherever it is written).
static const char asm_test_gather_fixture[] =
    ".text\ngathers:\nvpgatherqd 32(%rdx,%ymm7), %xmm8 {%k1}\nvpgatherqq (%rax,%zmm1,8), %zmm2 {%k3}\nvpgatherdd %ymm1, (%rax,%ymm2,4), %ymm0\n"
    "vgatherdps %xmm1, 8(%rax,%xmm9,2), %xmm3\nvpscatterdd %zmm0, (%rax,%zmm1,4) {%k1}\nvgatherqpd (,%zmm20,8), %zmm5 {%k2}\n"
    "vpgatherdd (%r12,%zmm31), %zmm30 {%k7}\nvscatterqps %ymm3, -64(%r9,%zmm4,4) {%k5}\nvgatherdpd 0x80(%rsp,%xmm4,8), %ymm2 {%k1}\n"
    "vpgatherqd %xmm0, (%rbp,%ymm1), %xmm2\n.intel_syntax noprefix\nvpgatherdd ymm0, dword ptr [rax+ymm2*4], ymm1\n"
    "vpgatherqd xmm8 {k1}, dword ptr [rdx+ymm7+32]\nvpscatterdd dword ptr [rax+4*zmm1] {k1}, zmm0\nvgatherqpd zmm5 {k2}, qword ptr [8*zmm20]\n"
    "vgatherdps xmm3, dword ptr [rax+xmm9*2+8], xmm1\n.att_syntax prefix\n";
// GNU as's line table of `.file` and `.loc` (#1115), compared byte for byte: DWARF 5's tables with MD5s and each way
// a file names its directory; each row's file, column, discriminator, isa, is_stmt, basic_block, prologue_end and
// epilogue_begin; line advances in and out of a special opcode's range, address advances by special opcode,
// DW_LNS_const_add_pc and DW_LNS_advance_pc, and sequences' ends; a row of line 0, which is none but keeps its column;
// two `.loc`s in a row; a sequence per code section, which keeps the isa; and GCC's view numbers, as data, with `view
// 0` and `view -0`.
static const char asm_test_lines_fixture[] =
    ".file 0 \"/work\" \"/src/main.c\" md5 0x00112233445566778899aabbccddeeff\n.file 1 \"/src/main.c\" md5 0x00112233445566778899aabbccddeeff\n"
    ".file 2 \"include/header.h\"\n.file 3 \"/usr/include\" \"stdio.h\"\n"
    ".text\n.globl f\nf:\n.loc 1 10 5\npushq %rbp\n.loc 1 11 9 prologue_end\nmovl $1, %eax\n.loc 2 3 1 is_stmt 0\nnop\n.loc 1 200 2 discriminator 3\nnop\n"
    ".loc 1 190 1 basic_block epilogue_begin\n.loc 1 191 1 isa 2\n.skip 300\nnop\n.loc 1 0 7\nnop\n.loc 1 192 is_stmt 1\n.skip 20\nnop\n.loc 3 1 0\n.skip 17\nret\n"
    ".section .text.other,\"ax\",@progbits\ng:\n.loc 1 50 0 view .LVU1\n.loc 1 51 0 view .LVU2\nnop\n.loc 1 52 0 view .LVU3\n.p2align 4, 0xcc\n"
    ".loc 1 53 0 view -0\nnop\n.loc 1 54 0 view 0\n.loc 1 55 0 view .LVU4\njmp .Lnext\n.Lnext:\n.loc 1 56 0 view -0\nnop\n.loc 1 57 0 view -0\n"
    ".loc 1 58 0 view .LVU5\n.skip 17\n"
    ".section .debug_info,\"\",@progbits\n.long 0\n.section .debug_loclists,\"\",@progbits\n.uleb128 .LVU1\n.uleb128 .LVU2\n.uleb128 .LVU3\n.uleb128 .LVU4\n"
    ".byte .LVU5\n";
// Where GNU as states a `view -0` row's address again, by its frags (asm_line_emit): at the previous row's address,
// after a .skip that the previous row ended its frag's bytes at, after two alignments with code between them (as the
// previous row ended its frag), but not after a byte of data before an alignment.
static const char asm_test_line_frags_fixture[] =
    ".file 0 \"/w\" \"m.c\"\n.text\nnop\n.loc 0 1 0 view .LVU1\n.loc 0 2 0 view -0\nnop\n.loc 0 3 0 view .LVU2\n.skip 4\n.loc 0 4 0 view -0\nnop\n"
    ".loc 0 5 0 view .LVU3\n.p2align 3, 0xcc\nnop\n.p2align 4, 0xcc\n.loc 0 6 0 view -0\nnop\n.loc 0 7 0 view .LVU4\n.byte 1\n.p2align 4, 0xcc\n"
    ".loc 0 8 0 view -0\nret\n.section .debug_info,\"\",@progbits\n.long 0\n.section .debug_loclists,\"\",@progbits\n.uleb128 .LVU1\n.uleb128 .LVU2\n"
    ".uleb128 .LVU3\n.uleb128 .LVU4\n";
// DWARF 3's table, without `.file 0`: its directories and files in .debug_line itself.
static const char asm_test_line_three_fixture[] =
    ".file 1 \"a.c\"\n.file 2 \"dir/b.c\"\n.file 3 \"/abs/c.h\"\n.text\n.loc 1 3 0\nnop\n.loc 2 4 2\nnop\n.loc 3 10 1 prologue_end\nret\n"
    ".section .debug_info,\"\",@progbits\n.long 0\n";
// The header GNU as writes for a .debug_info without rows: of no files, and of DWARF 5's files.
static const char asm_test_line_empty_fixture[] = ".text\nempty: ret\n.section .debug_info,\"\",@progbits\n.long 0\n";
static const char asm_test_line_files_fixture[] =
    ".file 0 \"/w\" \"/src/m.c\" md5 0x00112233445566778899aabbccddeeff\n.file 1 \"/src/m.c\"\n.section .debug_info,\"\",@progbits\n.long 0\n.text\nret\n";
// GCC's empty .debug_line with its label, which .debug_info refers to; a row before an alignment, which waits for the
// instruction after it, a branch that grows past a large address advance, and a second section's sequence.
static const char asm_test_line_label_fixture[] =
    ".file 0 \"/w\" \"m.c\"\n.text\n.loc 0 1 0\n.p2align 4, 0xcc\nnop\n.loc 0 2 0\njmp .L1\n.loc 0 300 0\n.zero 274\n.L1:\nret\n"
    ".section .text.b,\"ax\",@progbits\n.loc 0 9 0\nret\n.section .debug_info,\"\",@progbits\n.long 0\n.section .debug_line,\"\",@progbits\n.Lline0:\n"
    ".section .debug_info\n.long .Lline0\n";

// GNU as's .debug_frame, which GCC and Clang write for -g without unwind tables (#1115): CIEs of version 1 without
// augmentation, shared between FDEs, whose addresses are 64-bit relocations, every entry padded to 8 bytes; a simple
// procedure's CIE, a signal frame's and another return column's, advances of each width, saved state, an escape, and
// a second code section.
static const char asm_test_debug_frame_fixture[] =
    ".cfi_sections .debug_frame\n.text\n.globl f\nf:\n.cfi_startproc\npushq %rbp\n.cfi_def_cfa_offset 16\n.cfi_offset 6, -16\nmovq %rsp, %rbp\n"
    ".cfi_def_cfa_register 6\n.skip 70\n.cfi_remember_state\npopq %rbp\n.cfi_def_cfa 7, 8\n.skip 300\n.cfi_restore_state\n.skip 70000\nleave\n"
    ".cfi_def_cfa 7, 8\nret\n.cfi_endproc\ng:\n.cfi_startproc\nret\n.cfi_endproc\n.section .text.other,\"ax\",@progbits\nh:\n"
    ".cfi_startproc simple\n.cfi_signal_frame\n.cfi_def_cfa 7, 8\nnop\n.cfi_escape 0x2e, 0x10\nret\n.cfi_endproc\nk:\n.cfi_startproc\n"
    ".cfi_return_column 17\nnop\n.cfi_undefined 3\nret\n.cfi_endproc\nm:\n.cfi_startproc\nret\n.cfi_endproc\n";
// Both sections, .eh_frame's first; and no section: an empty .cfi_sections writes neither, and .debug_frame without
// procedures is not written.
static const char asm_test_both_frames_fixture[] =
    ".cfi_sections .eh_frame, .debug_frame\n.text\nf:\n.cfi_startproc\npushq %rbx\n.cfi_def_cfa_offset 16\n.cfi_offset 3, -16\npopq %rbx\n"
    ".cfi_def_cfa_offset 8\nret\n.cfi_endproc\ng:\n.cfi_startproc\nret\n.cfi_endproc\n";
static const char asm_test_no_frames_fixture[] = ".cfi_sections\n.text\nf:\n.cfi_startproc\npushq %rbx\n.cfi_def_cfa_offset 16\npopq %rbx\nret\n.cfi_endproc\n";
static const char asm_test_unused_frames_fixture[] = ".cfi_sections .debug_frame\n.text\nf: ret\n";

static void asm_test_issue_fixtures(AsmTest* test, const char* directory, bool cross_check)
{
    static const struct
    {
        const char* source;
        X86Architecture architecture;
    } accepted[] = {
        { asm_test_tls_fixture, X86_ARCH_BASELINE },
        { asm_test_common_fixture, X86_ARCH_BASELINE },
        { asm_test_wide_fixture, X86_ARCH_BASELINE },
        { asm_test_x87_fixture, X86_ARCH_BASELINE },
        { asm_test_predicate_fixture, X86_ARCH_V4 },
        { asm_test_relax_fixture, X86_ARCH_BASELINE },
        { asm_test_alias_fixture, X86_ARCH_BASELINE },
        { asm_test_jump_fixture, X86_ARCH_BASELINE },
        { asm_test_undefined_fixture, X86_ARCH_BASELINE },
        { asm_test_section_name_fixture, X86_ARCH_BASELINE },
        { asm_test_lines_fixture, X86_ARCH_BASELINE },
        { asm_test_line_frags_fixture, X86_ARCH_BASELINE },
        { asm_test_line_three_fixture, X86_ARCH_BASELINE },
        { asm_test_line_empty_fixture, X86_ARCH_BASELINE },
        { asm_test_line_files_fixture, X86_ARCH_BASELINE },
        { asm_test_line_label_fixture, X86_ARCH_BASELINE },
        { asm_test_string_fixture, X86_ARCH_BASELINE },
        { asm_test_vmovq_fixture, X86_ARCH_V4 },
        { asm_test_gather_fixture, X86_ARCH_V4 },
        { asm_test_debug_frame_fixture, X86_ARCH_BASELINE },
        { asm_test_both_frames_fixture, X86_ARCH_BASELINE },
        { asm_test_no_frames_fixture, X86_ARCH_BASELINE },
        { asm_test_unused_frames_fixture, X86_ARCH_BASELINE },
    };
    asm_test_relax_programs(test, directory, cross_check);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(accepted); i += 1)
    {
        if (cross_check)
        {
            if (!asm_test_oracle(test, accepted[i].source, directory, accepted[i].architecture))
            {
                fprintf(stderr, "assembler #1115 fixture %u:\n%s", i, accepted[i].source);
            }
        }
        else
        {
            asm_test_pair(test, (String8){ .pointer = (char*)accepted[i].source, .length = strlen(accepted[i].source) }, 1, accepted[i].architecture);
        }
    }
    // GNU as 2.43's .base64 (which GCC 15 writes for string literals), by its bytes: 2.42 doesn't take it, so it isn't
    // compared with GNU as here; the program corpus compares GCC 15's with a GNU as that does.
    static const uint8_t base64[] = { 'h', 'e', 'l', 'l', 'o', 0, 1, 2, 0xff, 'h', 'i' };
    asm_test_bytes(test, ".data\n.base64 \"aGVsbG8=\", \"AAEC/w==\"\n.base64 \"\"\n.base64 \"aGk=\"\n", 2, base64, sizeof(base64), ".base64 decodes each string");
    // What GNU as refuses too, or what the Link can't hold: @TPOFF in an unsigned field (`movl $x@tpoff, %eax`),
    // @GOTTPOFF without RIP, a 2-byte TLS offset as data, a TLS modifier on a symbol outside a TLS section; .comm's
    // alignment that isn't a power of two, .lcomm's alignment operand (GNU as's ELF .lcomm takes none), a defined or
    // weak common symbol, a local's .comm or .lcomm declared again, an alias of a global common (before or after its
    // .comm) and a TLS modifier on one; strings of 8-byte characters; %st(8); fsubp of ST(0) and ST(i), and Intel's
    // arithmetic without operands, which GNU as refuses; an uppercase mnemonic, as every mnemonic is lowercase here
    // (GNU as takes either); malformed base64; a label difference whose subtrahend is in neither the datum's section
    // nor the minuend's, which GNU as can't resolve; and, deliberately, an undefined .L label, which GNU as lists as an
    // undefined global for the link to fail on. Of the line table: a .debug_line of bytes beside rows, a row of an
    // unassigned file, `view 0` where the view isn't 0, a numeric view but 0, is_stmt but 0 or 1, an unknown `.loc`
    // option, a file slot named again for another file, an MD5 within 64 bits, a view of a defined symbol; and,
    // deliberately, rows without a .debug_info, where GNU as writes its own DWARF, and a DWARF 5 table without `.file
    // 0`'s directory, where GNU as takes its working directory. Of string instructions' operands: a destination of
    // another segment than ES, a register of another size than the suffix's, mixed address sizes, Intel's without a
    // size, and, deliberately, AT&T's without one (where GNU as warns and takes 32 bits) and RDI's memory as the source
    // (where it warns and takes the instruction's own). Of .cfi_sections: a trailing comma, names without a comma, and,
    // deliberately, .sframe, a choice after the first procedure (which GNU as applies to the procedures after it), and
    // a .debug_frame of the source's own bytes beside CFI's (which GNU as appends to).
    static const char* const rejected[] = {
        ".text\nmovl $tl@tpoff, %eax\n.section .tbss,\"awT\",@nobits\ntl: .zero 4\n",
        ".text\nmovq ext@gottpoff, %rax\n",
        ".text\nleaq ext@tlsgd(%rax), %rdi\n",
        ".text\nmovl tl@tpoff(%rip), %eax\n.section .tbss,\"awT\",@nobits\ntl: .zero 4\n",
        ".data\n.short tl@dtpoff\n.section .tbss,\"awT\",@nobits\ntl: .zero 4\n",
        ".data\n.long tl@gottpoff\n.section .tbss,\"awT\",@nobits\ntl: .zero 4\n",
        ".data\nd: .long 0\n.text\nmovl %fs:d@tpoff, %eax\n",
        ".comm x,4,3\n",
        ".lcomm x,4,4\n",
        ".data\nx: .long 0\n.comm x,4,4\n",
        ".comm x,4,4\n.data\nx: .long 0\n",
        ".lcomm x,4\n.data\nx: .long 0\n",
        ".weak x\n.comm x,4,4\n",
        ".local x\n.comm x,4,4\n.comm x,4,16\n",
        ".lcomm x,4\n.lcomm x,4\n",
        ".comm c,4,4\n.globl a\n.set a, c\n.data\n.quad a\n",
        ".set a, c+4\n.comm c,4,4\n",
        ".comm t,4,4\n.text\nmovl %fs:t@tpoff, %eax\n",
        ".text\njmp .Lnowhere\n",
        ".file 0 \"/d\" \"x.c\"\n.text\n.loc 0 1 0\nnop\n.section .debug_info,\"\",@progbits\n.long 0\n.section .debug_line,\"\",@progbits\n.byte 0\n",
        ".file 1 \"x.c\"\n.text\n.loc 2 1 0\nnop\n",
        ".file 0 \"/d\" \"x.c\"\n.text\n.loc 0 1 0 view .LVU1\n.loc 0 2 0 view 0\nnop\n.section .debug_info,\"\",@progbits\n.long 0\n",
        ".file 0 \"/d\" \"x.c\"\n.text\n.loc 0 1 0 view 1\n",
        ".file 1 \"x.c\"\n.text\n.loc 1 1 0 is_stmt 2\n",
        ".file 1 \"x.c\"\n.text\n.loc 1 1 0 bogus\n",
        ".file 1 \"x.c\"\n.file 1 \"y.c\"\n",
        ".file 0 \"/d\" \"x.c\" md5 0x1234\n",
        ".file 1 \"x.c\"\n.text\nl: nop\n.loc 1 1 0 view l\n",
        ".file 0 \"/d\" \"x.c\"\n.text\n.loc 0 1 0\nnop\n",
        ".file 0 \"x.c\"\n.text\n.loc 0 1 0\nnop\n.section .debug_info,\"\",@progbits\n.long 0\n",
        ".text\nmovsq (%rsi), %ds:(%rdi)\n",
        ".text\nstosb %ax, %es:(%rdi)\n",
        ".text\nmovsq (%esi), %es:(%rdi)\n",
        ".intel_syntax noprefix\nmovs es:[rdi], [rsi]\n",
        ".text\nmovs (%rsi), %es:(%rdi)\n",
        ".text\nmovsq (%rdi), %es:(%rsi)\n",
        ".section .rodata.str8.8,\"aMS\",@progbits,8\n.quad 0\n",
        "fadd %st(8)\n",
        "fld %st(x)\n",
        "fadd %st(1), %st(2)\n",
        "fsubp %st(1), %st\n",
        ".intel_syntax noprefix\nfadd\n",
        "CMPLTSD %xmm0, %xmm1\n",
        "DATA16\n",
        ".data\n.base64 \"abc\"\n",
        ".data\n.base64 \"ab$=\"\n",
        ".data\n.base64 \"=AAA\"\n",
        ".data\n.base64 \"A=AA\"\n",
        ".data\n.base64 \"AA==AAAA\"\n",
        ".data\n.base64 label\n",
        ".data\n.long a - b\n.text\na: nop\n.section .rodata\nb: .byte 0\n",
        ".cfi_sections .debug_frame,\n",
        ".cfi_sections .debug_frame .eh_frame\n",
        ".cfi_sections .sframe\n",
        ".text\nf: .cfi_startproc\nret\n.cfi_endproc\n.cfi_sections .debug_frame\n",
        ".cfi_sections .debug_frame\n.section .debug_frame,\"\",@progbits\n.byte 1\n.text\nf: .cfi_startproc\nret\n.cfi_endproc\n",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(rejected); i += 1)
    {
        asm_test_pair(test, (String8){ .pointer = (char*)rejected[i], .length = strlen(rejected[i]) }, 0, X86_ARCH_BASELINE);
    }
    // Gathers' and scatters' operands on x86-64-v4, where their forms are (#1115): what GNU as refuses too, k0 or no
    // mask of AVX-512's, a zeroing mask, an index of another class, a vector index on an instruction without VSIB, a
    // vector base and RIP's; and, deliberately, the mask, index and destination registers that aren't distinct, which
    // GNU as warns of (the instruction faults).
    static const char* const rejected_vsib[] = {
        ".text\nvpgatherdd (%rax,%zmm1), %zmm2 {%k0}\n",
        ".text\nvpgatherdd (%rax,%zmm1), %zmm2\n",
        ".text\nvpgatherdd (%rax,%zmm1), %zmm2 {%k1}{z}\n",
        ".text\nvpgatherdd %ymm1, (%rax,%xmm2,4), %ymm0\n",
        ".text\nmovl (%rax,%xmm1), %eax\n",
        ".text\nvpgatherdd %ymm1, (%ymm2), %ymm0\n",
        ".text\nvpgatherdd %ymm1, (%rip,%ymm2), %ymm0\n",
        ".text\nvpgatherdd %ymm1, (%rax,%ymm1,4), %ymm0\n",
        ".text\nvpgatherdd (%rax,%zmm1), %zmm1 {%k1}\n",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(rejected_vsib); i += 1)
    {
        asm_test_pair(test, (String8){ .pointer = (char*)rejected_vsib[i], .length = strlen(rejected_vsib[i]) }, 0, X86_ARCH_V4);
    }
    // The object's common symbols and _GLOBAL_OFFSET_TABLE_, without --cross-check too: SHN_COMMON with the alignment as
    // the value (each its own atom, BUSTER_LINK_SYMBOL_COMMON's, which the object leaves to the linker), and an
    // undefined global no relocation refers to; asm_assemble counts the commons (AsmResult.common_count).
    AsmResult common = asm_test_pair(test, BUSTER_S8(".comm block,40\n.comm word,4,8\ncommon_reader: movq x@GOTPCREL(%rip), %rax\n"), 1, X86_ARCH_BASELINE);
    const Object object = common.error == 0 ? asm_object(&common, BUSTER_S8("common.s"), &test->vector) : (Object){ .error = OBJECT_ERROR_RANGE };
    ObjectView view;
    char error[256] = "no object";
    bool okay = object.error == OBJECT_ERROR_NONE && object_view_open(&view, object.bytes, object.size, false, error, sizeof(error)) && common.common_count == 2;
    uint32_t found = 0;
    for (uint64_t i = 1; okay && i < view.symbol_count; i += 1)
    {
        const Elf64_Sym* symbol = &view.symbols[i];
        const char* name = object_view_string(view.symbol_names, view.symbol_names_size, symbol->st_name);
        const bool block = name != 0 && strcmp(name, "block") == 0 && symbol->st_shndx == SHN_COMMON && symbol->st_value == 16 && symbol->st_size == 40;
        const bool word = name != 0 && strcmp(name, "word") == 0 && symbol->st_shndx == SHN_COMMON && symbol->st_value == 8 && symbol->st_size == 4;
        const bool got = name != 0 && strcmp(name, "_GLOBAL_OFFSET_TABLE_") == 0 && symbol->st_shndx == SHN_UNDEF && ELF64_ST_BIND(symbol->st_info) == STB_GLOBAL;
        found += block + word + got;
        okay = okay && (ELF64_ST_TYPE(symbol->st_info) == STT_OBJECT) == (block || word || (name != 0 && symbol->st_shndx == SHN_COMMON));
    }
    asm_test_check(test, okay && found == 3, "common symbols are SHN_COMMON objects and the GOT symbol is declared", error);
    // The writer refuses declared symbols it can't take before it writes: a count without a list, a defined symbol (the
    // first with an atom, a common one), an id past the table, and symbol sizes short of the symbols.
    ObjectUnits units;
    ObjectSymbols symbols;
    if (common.error == 0 && object_prepare(&units, &common.link, 1, &test->vector) && object_map_symbols(&symbols, &common.link, &test->vector))
    {
        const uint64_t saved = test->vector.length;
        ObjectOptions options = common.object_options;
        options.declared_symbols = 0;
        const Object no_list = object_build(&common.link, &units, &symbols, 0, options, BUSTER_S8("common.s"), &test->vector);
        SymbolId defined = 0;
        for (SymbolId s = 1; s < common.link.symbols.count; s += 1)
        {
            defined = defined == 0 && common.link.symbols.atom[s] != 0 ? s : defined;
        }
        const SymbolId wrong[2] = { defined, common.link.symbols.count };
        options = common.object_options;
        options.declared_symbols = &wrong[0];
        options.declared_symbol_count = 1;
        const Object declared_defined = object_build(&common.link, &units, &symbols, 0, options, BUSTER_S8("common.s"), &test->vector);
        options.declared_symbols = &wrong[1];
        const Object declared_past = object_build(&common.link, &units, &symbols, 0, options, BUSTER_S8("common.s"), &test->vector);
        // And symbol sizes short of the symbols, or one below the Link's size.
        uint64_t sizes[64] = { 0 };
        SymbolId sized = 0;
        for (SymbolId s = 1; s < common.link.symbols.count && s < BUSTER_ARRAY_LENGTH(sizes); s += 1)
        {
            sizes[s] = common.link.symbols.size[s];
            sized = sized == 0 && sizes[s] != 0 ? s : sized;
        }
        options = common.object_options;
        options.symbol_sizes = sizes;
        options.symbol_size_count = common.link.symbols.count - 1;
        const Object short_sizes = object_build(&common.link, &units, &symbols, 0, options, BUSTER_S8("common.s"), &test->vector);
        sizes[sized] -= sized != 0 ? 1 : 0;
        options.symbol_size_count = common.link.symbols.count;
        const Object smaller_size = object_build(&common.link, &units, &symbols, 0, options, BUSTER_S8("common.s"), &test->vector);
        asm_test_check(test, defined != 0 && sized != 0 && common.link.symbols.count <= BUSTER_ARRAY_LENGTH(sizes) && no_list.error == OBJECT_ERROR_RANGE &&
                                 declared_defined.error == OBJECT_ERROR_RANGE && declared_past.error == OBJECT_ERROR_RANGE &&
                                 short_sizes.error == OBJECT_ERROR_RANGE && smaller_size.error == OBJECT_ERROR_RANGE && test->vector.length == saved,
                       "invalid declared symbols and symbol sizes are refused before output allocation", 0);
    }
    else
    {
        asm_test_check(test, false, "declared symbol fixture allocation", common.error);
    }
    // A TLS modifier through an alias of an undefined symbol makes that symbol STT_TLS (GNU ld refuses a non-TLS
    // reference to a TLS definition): linked with C that defines it, the read gives the thread's value.
    if (cross_check)
    {
        static const char tls_alias[] = ".text\n.globl tls_alias_read\n.type tls_alias_read, @function\n.set tls_alias, tls_alias_target\n"
                                        "tls_alias_read: movl %fs:tls_alias@TPOFF, %eax\nret\n.size tls_alias_read, .-tls_alias_read\n";
        static const char caller[] = "_Thread_local int tls_alias_target = 41;\nint tls_alias_read(void);\n"
                                     "int main(void) { tls_alias_target += 1; return tls_alias_read() == 42 ? 0 : 1; }\n";
        char object_path[PATH_MAX];
        char caller_path[PATH_MAX];
        char executable[PATH_MAX];
        char output[PATH_MAX];
        char errors[PATH_MAX];
        snprintf(object_path, sizeof(object_path), "%s/tls-alias.o", directory);
        snprintf(caller_path, sizeof(caller_path), "%s/tls-alias-caller.c", directory);
        snprintf(executable, sizeof(executable), "%s/tls-alias", directory);
        snprintf(output, sizeof(output), "%s/tls-alias-output", directory);
        snprintf(errors, sizeof(errors), "%s/tls-alias-errors", directory);
        AsmResult result = asm_test_pair(test, (String8){ .pointer = (char*)tls_alias, .length = sizeof(tls_alias) - 1 }, 1, X86_ARCH_BASELINE);
        const Object assembled = result.error == 0 ? asm_object(&result, BUSTER_S8("tls-alias.s"), &test->vector) : (Object){ .error = OBJECT_ERROR_RANGE };
        char* link[] = { "cc", caller_path, object_path, "-o", executable, 0 };
        char* run[] = { executable, 0 };
        const bool ran = assembled.error == OBJECT_ERROR_NONE && object_write_file(object_path, assembled.bytes, assembled.size) == OBJECT_ERROR_NONE &&
                         object_write_file(caller_path, (const uint8_t*)caller, sizeof(caller) - 1) == OBJECT_ERROR_NONE && object_test_run(link, output, errors) == 0 &&
                         object_test_run(run, output, errors) == 0;
        asm_test_check(test, ran, "a TLS modifier through an alias of an undefined symbol links with its TLS definition and reads it", result.error);
        const char* const paths[] = { object_path, caller_path, executable, output, errors };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(paths); i += 1)
        {
            unlink(paths[i]);
        }
    }
}

typedef struct AsmProgramVariant AsmProgramVariant;
struct AsmProgramVariant
{
    const char* name;
    // Each variant names its -march: a compiler's default may be another (Ubuntu 26.04's GCC 15 writes AVX's at -O2); its
    // debug information, -g0 or -g; and any other option.
    const char* flags[5];
    X86Architecture architecture;
};

// Whether the section a compiler's line (from `word` to `end`) switches to holds code: .text, .data and .bss's, and a
// .section's by its flags (x), or without flags by its name (.text.*); any other line keeps `code`.
static bool asm_test_program_code(const char* line, uint64_t length, bool code)
{
    const bool text = length >= 5 && memcmp(line, ".text", 5) == 0 && (length == 5 || line[5] == ' ' || line[5] == '\t');
    const bool data = (length >= 5 && memcmp(line, ".data", 5) == 0 && (length == 5 || line[5] == ' ' || line[5] == '\t')) ||
                      (length >= 4 && memcmp(line, ".bss", 4) == 0 && (length == 4 || line[4] == ' ' || line[4] == '\t'));
    const bool section = length > 9 && memcmp(line, ".section", 8) == 0 && (line[8] == ' ' || line[8] == '\t');
    bool result = text ? true : data ? false : code;
    if (section)
    {
        uint64_t at = 8;
        while (at < length && (line[at] == ' ' || line[at] == '\t' || line[at] == '"'))
        {
            at += 1;
        }
        const bool named = length - at >= 5 && memcmp(line + at, ".text", 5) == 0;
        const char* comma = memchr(line + at, ',', length - at);
        const char* quote = comma != 0 ? memchr(comma, '"', (uint64_t)(line + length - comma)) : 0;
        const char* close = quote != 0 ? memchr(quote + 1, '"', (uint64_t)(line + length - quote - 1)) : 0;
        result = close != 0 ? memchr(quote, 'x', (uint64_t)(close - quote)) != 0 : named;
    }
    return result;
}

// The source with its code alignment filled with int3: each .p2align, .align and .balign of a code section without a
// fill, or with 0x90's (which pad code with NOPs), takes 0xcc instead, which pads with that byte and leaves the layout
// as it was.
static String8 asm_test_program_filled(Arena* arena, String8 source)
{
    char* bytes = arena_push(arena, source.length * 2 + 1, 1);
    uint64_t length = 0;
    bool in_code = true;
    for (uint64_t at = 0; bytes != 0 && at < source.length;)
    {
        uint64_t end = at;
        while (end < source.length && source.pointer[end] != '\n')
        {
            end += 1;
        }
        uint64_t word = at;
        while (word < end && (source.pointer[word] == ' ' || source.pointer[word] == '\t'))
        {
            word += 1;
        }
        in_code = asm_test_program_code(source.pointer + word, end - word, in_code);
        static const char* const directives[] = { ".p2align", ".balign", ".align" };
        uint64_t directive = 0;
        for (uint32_t i = 0; directive == 0 && i < BUSTER_ARRAY_LENGTH(directives); i += 1)
        {
            const uint64_t size = strlen(directives[i]);
            directive = end - word > size && memcmp(source.pointer + word, directives[i], size) == 0 &&
                                (source.pointer[word + size] == ' ' || source.pointer[word + size] == '\t') ? word + size : 0;
        }
        // The line up to its first operand's end, then its fill (0xcc for none or 0x90), then the rest.
        uint64_t comma = directive;
        while (directive != 0 && comma < end && source.pointer[comma] != ',' && source.pointer[comma] != '#')
        {
            comma += 1;
        }
        uint64_t fill_end = comma < end && source.pointer[comma] == ',' ? comma + 1 : comma;
        while (directive != 0 && fill_end < end && source.pointer[fill_end] != ',' && source.pointer[fill_end] != '#')
        {
            fill_end += 1;
        }
        String8 fill = { .pointer = (char*)source.pointer + (comma < end && source.pointer[comma] == ',' ? comma + 1 : comma), .length = 0 };
        fill.length = (uint64_t)(source.pointer + fill_end - fill.pointer);
        while (fill.length != 0 && (fill.pointer[0] == ' ' || fill.pointer[0] == '\t'))
        {
            fill.pointer += 1;
            fill.length -= 1;
        }
        while (fill.length != 0 && (fill.pointer[fill.length - 1] == ' ' || fill.pointer[fill.length - 1] == '\t'))
        {
            fill.length -= 1;
        }
        const bool code = in_code && directive != 0 && (fill.length == 0 || x86_match_name(fill, "0x90") || x86_match_name(fill, "144"));
        if (code)
        {
            memcpy(bytes + length, source.pointer + at, comma - at);
            length += comma - at;
            memcpy(bytes + length, ",0xcc", 5);
            length += 5;
            memcpy(bytes + length, source.pointer + fill_end, end - fill_end);
            length += end - fill_end;
        }
        else
        {
            memcpy(bytes + length, source.pointer + at, end - at);
            length += end - at;
        }
        if (end < source.length)
        {
            bytes[length++] = '\n';
        }
        at = end + 1;
    }
    return (String8){ .pointer = bytes, .length = bytes != 0 ? length : 0 };
}

// Where GNU as pads code unlike binutils 2.47 (2.42 orders its long NOPs otherwise), GNU as's object's code padding is
// replaced with ours before the comparison: the bytes where the int3-filled source's object (asm_test_program_filled)
// has 0xcc and ours doesn't are padding, and only there do the objects' bytes differ in GNU as's choice of NOPs. Every
// other byte, and every section, symbol and relocation, is still compared.
static bool asm_test_program_padding(AsmTest* test, String8 source, X86Architecture architecture, const ObjectView* ours, ObjectView* theirs, Arena* arena)
{
    const String8 filled = asm_test_program_filled(arena, source);
    AsmResult result = filled.length != 0 ? asm_test_pair(test, filled, 1, architecture) : (AsmResult){ .error = "arena exhausted" };
    const Object object = result.error == 0 ? asm_object(&result, BUSTER_S8("filled.s"), arena) : (Object){ .error = OBJECT_ERROR_RANGE };
    if (result.error != 0)
    {
        uint32_t line = 1;
        for (uint64_t i = 0; i < result.location && i < filled.length; i += 1)
        {
            line += filled.pointer[i] == '\n';
        }
        printf("assembler program padding: the int3-filled source's line %u: %s\n", line, result.error);
    }
    ObjectView padded;
    char error[256] = { 0 };
    bool valid = object.error == OBJECT_ERROR_NONE && object_view_open(&padded, object.bytes, object.size, false, error, sizeof(error));
    if (result.error == 0 && !valid)
    {
        printf("assembler program padding: the int3-filled source's object: error %u, %s\n", (unsigned)object.error, error);
    }
    for (uint64_t i = 1; valid && i < ours->section_count; i += 1)
    {
        const Elf64_Shdr* section = &ours->sections[i];
        if ((section->sh_flags & SHF_EXECINSTR) != 0 && section->sh_type == SHT_PROGBITS)
        {
            const char* name = object_view_section_name(ours, i);
            const uint64_t j = object_test_section_named(&padded, name, false);
            const uint64_t k = object_test_section_named(theirs, name, false);
            const Elf64_Shdr* filler = j != 0 ? &padded.sections[j] : 0;
            const Elf64_Shdr* gnu = k != 0 ? &theirs->sections[k] : 0;
            if (filler != 0 && gnu != 0 && filler->sh_size == section->sh_size && gnu->sh_size == section->sh_size)
            {
                uint8_t* bytes = (uint8_t*)theirs->file + gnu->sh_offset;
                for (uint64_t b = 0; b < section->sh_size; b += 1)
                {
                    const uint8_t own = ours->file[section->sh_offset + b];
                    bytes[b] = padded.file[filler->sh_offset + b] == 0xcc && own != 0xcc ? own : bytes[b];
                }
            }
        }
    }
    return valid;
}

// Every section of ours that isn't GNU as's of its name, with where their bytes first differ: the comparator names only
// the first (an object lists .eh_frame before .text, whose different length then shows in .eh_frame first).
static void asm_test_program_sections(const ObjectView* ours, const ObjectView* theirs, const char* name)
{
    for (uint64_t i = 1; i < ours->section_count; i += 1)
    {
        const Elf64_Shdr* a = &ours->sections[i];
        const uint64_t j = object_test_compared_section(ours, i) ? object_test_section_named(theirs, object_view_section_name(ours, i), false) : 0;
        const Elf64_Shdr* b = j != 0 ? &theirs->sections[j] : 0;
        const bool contents = b != 0 && a->sh_type != SHT_NOBITS && b->sh_type != SHT_NOBITS;
        uint64_t first = 0;
        while (contents && first < a->sh_size && first < b->sh_size && ours->file[a->sh_offset + first] == theirs->file[b->sh_offset + first])
        {
            first += 1;
        }
        // What object_test_same_as compares: a compared section GNU as lacks, its type, flags, size, alignment, entry
        // size and bytes.
        if (b == 0 && object_test_compared_section(ours, i))
        {
            printf("assembler program section %s: %s: GNU as has none\n", name, object_view_section_name(ours, i));
        }
        else if (b != 0 && (a->sh_type != b->sh_type || a->sh_size != b->sh_size || a->sh_flags != b->sh_flags || a->sh_addralign != b->sh_addralign || a->sh_entsize != b->sh_entsize ||
                            (contents && first < a->sh_size)))
        {
            printf("assembler program section %s: %s: type %u, flags %#llx, size %llu, alignment %llu, entry size %llu against GNU as's %u, %#llx, %llu, %llu, %llu; first difference at "
                   "%#llx:",
                   name, object_view_section_name(ours, i), a->sh_type, (unsigned long long)a->sh_flags, (unsigned long long)a->sh_size, (unsigned long long)a->sh_addralign,
                   (unsigned long long)a->sh_entsize, b->sh_type, (unsigned long long)b->sh_flags, (unsigned long long)b->sh_size, (unsigned long long)b->sh_addralign,
                   (unsigned long long)b->sh_entsize, (unsigned long long)first);
            for (uint32_t side = 0; contents && side < 2; side += 1)
            {
                const ObjectView* view = side == 0 ? ours : theirs;
                const Elf64_Shdr* header = side == 0 ? a : b;
                printf(" %s", side == 0 ? "ours" : "GNU as's");
                for (uint64_t k = first; k < first + 16 && k < header->sh_size; k += 1)
                {
                    printf(" %02x", view->file[header->sh_offset + k]);
                }
            }
            printf("\n");
        }
    }
    for (uint64_t j = 1; j < theirs->section_count; j += 1)
    {
        const char* section = object_view_section_name(theirs, j);
        // GNU as writes .text, .data and .bss even empty, which the comparator lets ours omit.
        const bool always = theirs->sections[j].sh_size == 0 && (strcmp(section, ".text") == 0 || strcmp(section, ".data") == 0 || strcmp(section, ".bss") == 0);
        if (object_test_compared_section(theirs, j) && !always && object_test_section_named(ours, section, false) == 0)
        {
            printf("assembler program section %s: %s: only GNU as has it\n", name, section);
        }
    }
}

static bool asm_test_program_link(const char* compiler, const char* object, const char* executable, const char* output, const char* errors)
{
    char* link[] = { (char*)compiler, (char*)object, "-o", (char*)executable, "-pthread", 0 };
    unlink(executable);
    return object_test_run(link, output, errors) == 0;
}

static void asm_test_program_corpus(AsmTest* test, const char* directory, bool keep)
{
    static const char* const programs[] = { "floating", "storage", "strings", "switch", "gather" };
    static const char* const compilers[] = { "gcc", "clang" };
    static const char* const syntaxes[] = { "-masm=att", "-masm=intel" };
    static const AsmProgramVariant variants[] = {
        { .name = "O0", .flags = { "-O0", "-march=x86-64", "-fPIE", "-g0" }, .architecture = X86_ARCH_BASELINE },
        { .name = "O2", .flags = { "-O2", "-march=x86-64", "-fPIE", "-g0" }, .architecture = X86_ARCH_BASELINE },
        { .name = "O2-pic", .flags = { "-O2", "-march=x86-64", "-fPIC", "-g0" }, .architecture = X86_ARCH_BASELINE },
        { .name = "O3-v4", .flags = { "-O3", "-march=x86-64-v4", "-fPIE", "-g0" }, .architecture = X86_ARCH_V4 },
        // -g's DWARF (#1115): the line table GNU as makes of .loc, GCC's view numbers and its location lists.
        { .name = "O0-g", .flags = { "-O0", "-march=x86-64", "-fPIE", "-g" }, .architecture = X86_ARCH_BASELINE },
        { .name = "O2-g", .flags = { "-O2", "-march=x86-64", "-fPIE", "-g" }, .architecture = X86_ARCH_BASELINE },
        // Without unwind tables, -g's CFI is GNU as's .debug_frame (`.cfi_sections .debug_frame`).
        { .name = "O2-g-frame", .flags = { "-O2", "-march=x86-64", "-fPIE", "-g", "-fno-asynchronous-unwind-tables" }, .architecture = X86_ARCH_BASELINE },
    };
    static const char* const targets[] = { "-march=x86-64", "-march=x86-64-v2", "-march=x86-64-v3", "-march=x86-64-v4" };
    // The x86-64-v4 programs run where the host has every feature of x86-64-v4, not only AVX-512's: clang's code for it
    // takes FMA's vfmadd231sd, and a virtual CPU can show AVX512F, BW, DQ and VL with FMA or another of v3's masked
    // (#1223's review). Elsewhere their objects are still compared.
    X86Target v4 = { 0 };
    const bool parsed = x86_target_parse("x86-64-v4", 9, &v4);
    asm_test_check(test, parsed, "the corpus's x86-64-v4 target", 0);
    const bool runs_v4 = parsed && (x86_target_native().features & v4.features) == v4.features;
    const bool gnu_padding = asm_test_gas_pads_like_2_47(test, directory);
    Arena files = arena_reserve(1ull << 28);
    uint32_t passed = 0;
    uint32_t cases = 0;
    for (uint32_t compiler = 0; compiler < BUSTER_ARRAY_LENGTH(compilers); compiler += 1)
    {
        char output[PATH_MAX];
        char errors[PATH_MAX];
        snprintf(output, sizeof(output), "%s/programs-version", directory);
        snprintf(errors, sizeof(errors), "%s/programs-version-errors", directory);
        char* version[] = { (char*)compilers[compiler], "--version", 0 };
        const bool available = object_test_run(version, output, errors) == 0;
        asm_test_check(test, available, "program corpus tool available", compilers[compiler]);
        unlink(output);
        unlink(errors);
        for (uint32_t program = 0; available && program < BUSTER_ARRAY_LENGTH(programs); program += 1)
        {
            for (uint32_t variant = 0; variant < BUSTER_ARRAY_LENGTH(variants); variant += 1)
            {
                for (uint32_t syntax = 0; syntax < BUSTER_ARRAY_LENGTH(syntaxes); syntax += 1)
                {
                    files.length = 0;
                    const AsmProgramVariant* chosen = &variants[variant];
                    char name[128];
                    snprintf(name, sizeof(name), "%s %s %s %s", programs[program], compilers[compiler], chosen->name, syntax ? "intel" : "att");
                    char stem[PATH_MAX];
                    snprintf(stem, sizeof(stem), "%s/program-%s-%s-%s-%s", directory, programs[program], compilers[compiler], chosen->name, syntax ? "intel" : "att");
                    char source_path[PATH_MAX];
                    snprintf(source_path, sizeof(source_path), "examples/assembler/program-%s.c", programs[program]);
                    const size_t length = strlen(stem);
                    static const char* const suffixes[] = { ".s", "-reference.o", "-vector.o", "-scalar.o", "-reference", "-program", "-reference-output", "-output", "-errors" };
                    char paths[BUSTER_ARRAY_LENGTH(suffixes)][PATH_MAX];
                    bool okay = length + 24 < PATH_MAX;
                    for (uint32_t path = 0; okay && path < BUSTER_ARRAY_LENGTH(suffixes); path += 1)
                    {
                        memcpy(paths[path], stem, length);
                        strcpy(paths[path] + length, suffixes[path]);
                    }
                    char* const assembly = paths[0];
                    char* const reference_path = paths[1];
                    char* const vector_path = paths[2];
                    char* const scalar_path = paths[3];
                    char* const reference_program = paths[4];
                    char* const program_path = paths[5];
                    char* const reference_output = paths[6];
                    char* const program_output = paths[7];
                    char* const error_path = paths[8];
                    // The compiler's arguments: the variant's flags it gives, then the corpus's.
                    char* generate[16] = { (char*)compilers[compiler] };
                    uint32_t argument = 1;
                    for (uint32_t flag = 0; flag < BUSTER_ARRAY_LENGTH(chosen->flags); flag += 1)
                    {
                        generate[argument] = (char*)chosen->flags[flag];
                        argument += chosen->flags[flag] != 0;
                    }
                    char* const common_arguments[] = { (char*)syntaxes[syntax], "-fcf-protection=none", "-fcommon", "-S", source_path, "-o", assembly, compiler == 1 ? "-fno-integrated-as" : 0 };
                    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(common_arguments); i += 1)
                    {
                        generate[argument + i] = common_arguments[i];
                    }
                    okay = okay && asm_test_tool(generate, assembly, program_output, error_path) == 0;
                    const String8 source = okay ? object_test_read_file(&files, assembly) : (String8){ 0 };
                    okay = okay && source.pointer != 0 && source.length != 0;
                    asm_test_check(test, okay, name, "generate the program's assembly");
                    cases += 1;
                    // The first step that fails, which the report names.
                    char why[320] = "generate the program's assembly";
                    if (okay)
                    {
                        const char* target = targets[chosen->architecture <= X86_ARCH_V4 ? chosen->architecture : 0];
                        // GNU as compresses debug sections where its build makes that the default (Arch's binutils
                        // does, Ubuntu's doesn't); the assembler's are uncompressed, as the comparison needs (#1115).
                        char* gas[] = { "as", "--64", "--nocompress-debug-sections", "-o", reference_path, assembly, 0 };
                        char* vector[] = { "/proc/self/exe", "assemble", (char*)target, "-o", vector_path, assembly, 0 };
                        char* scalar[] = { "/proc/self/exe", "assemble", "--scalar", (char*)target, "-o", scalar_path, assembly, 0 };
                        const bool runs = chosen->architecture != X86_ARCH_V4 || runs_v4;
                        bool reference = oracle_epoch_file(assembly) && asm_test_tool(gas, reference_path, program_output, error_path) == 0;
                        reference = reference && asm_test_program_link(compilers[compiler], reference_path, reference_program, program_output, error_path);
                        reference = reference && (!runs || object_test_run((char*[]){ reference_program, 0 }, reference_output, error_path) == 0);
                        asm_test_check(test, reference, name, "GNU as's object links and runs");
                        const AsmResult result = asm_test_pair(test, source, 1, chosen->architecture);
                        bool assembled = result.error == 0 && object_test_run(vector, program_output, error_path) == 0 && object_test_run(scalar, program_output, error_path) == 0;
                        asm_test_check(test, assembled, name, result.error != 0 ? result.error : "both production assembly commands");
                        bool same = false;
                        char difference[256] = "the vector and scalar objects differ";
                        if (assembled)
                        {
                            const String8 gnu = object_test_read_file(&files, reference_path);
                            const String8 vector_object = object_test_read_file(&files, vector_path);
                            const String8 scalar_object = object_test_read_file(&files, scalar_path);
                            same = vector_object.length != 0 && vector_object.length == scalar_object.length && memcmp(vector_object.pointer, scalar_object.pointer, vector_object.length) == 0;
                            ObjectView ours;
                            ObjectView theirs;
                            same = same && object_view_open(&ours, (const uint8_t*)vector_object.pointer, vector_object.length, false, difference, sizeof(difference));
                            same = same && reference && asm_test_gnu_object(&theirs, gnu, &files, difference, sizeof(difference));
                            if (same && !gnu_padding && !asm_test_program_padding(test, source, chosen->architecture, &ours, &theirs, &files))
                            {
                                snprintf(difference, sizeof(difference), "the source with its code padding filled with int3 doesn't assemble");
                                same = false;
                            }
                            const bool viewed = same;
                            same = same && object_test_same_as(&ours, &theirs, difference, sizeof(difference));
                            asm_test_check(test, same, name, difference);
                            if (viewed && !same)
                            {
                                asm_test_program_sections(&ours, &theirs, name);
                            }
                        }
                        // The program runs as GNU as's does, even where its objects differ (in an older GNU as's code padding).
                        bool ran = assembled && reference && asm_test_program_link(compilers[compiler], vector_path, program_path, program_output, error_path);
                        ran = ran && (!runs || object_test_run((char*[]){ program_path, 0 }, program_output, error_path) == 0);
                        if (ran && runs)
                        {
                            const String8 expected = object_test_read_file(&files, reference_output);
                            const String8 actual = object_test_read_file(&files, program_output);
                            ran = expected.pointer != 0 && actual.pointer != 0 && expected.length != 0 && expected.length == actual.length &&
                                  memcmp(expected.pointer, actual.pointer, expected.length) == 0;
                        }
                        if (assembled && reference)
                        {
                            asm_test_check(test, ran, name, "the program assembled here links, runs and prints GNU as's program's output");
                        }
                        okay = reference && assembled && same && ran;
                        snprintf(why, sizeof(why), "%s", !reference ? "GNU as's object links and runs" : !assembled ? (result.error != 0 ? result.error : "both production assembly commands") :
                                                         !same ? difference : "the program assembled here links, runs and prints GNU as's program's output");
                        if (!assembled && result.error != 0)
                        {
                            uint32_t line = 1;
                            for (uint32_t at = 0; at < result.location && at < source.length; at += 1)
                            {
                                line += source.pointer[at] == '\n';
                            }
                            const uint64_t used = strlen(why);
                            snprintf(why + used, sizeof(why) - used, " (line %u%s%.*s)", line, result.error_name.length != 0 ? ": " : "", (int)result.error_name.length,
                                     result.error_name.pointer != 0 ? result.error_name.pointer : "");
                        }
                    }
                    if (!okay)
                    {
                        const String8 diagnostic = object_test_read_file(&files, error_path);
                        fprintf(stderr, "assembler program corpus %s: %s%s%.*s\n", name, why, diagnostic.length != 0 ? ": " : "", (int)diagnostic.length,
                                diagnostic.pointer != 0 ? diagnostic.pointer : "");
                        printf("assembler program artifacts: %s\n", stem);
                        // The compilers' output differs by version, and a CI runner's artifacts are gone with it: the
                        // source itself, where it is small enough, says what failed.
                        if (source.length != 0 && source.length <= 65536)
                        {
                            printf("assembler program source %s (%llu bytes):\n%.*s\nassembler program source end\n", name, (unsigned long long)source.length, (int)source.length,
                                   source.pointer);
                        }
                    }
                    passed += okay;
                    for (uint32_t path = 0; okay && !keep && path < BUSTER_ARRAY_LENGTH(suffixes); path += 1)
                    {
                        unlink(paths[path]);
                    }
                }
            }
        }
    }
    printf("assembler program corpus: %u of %u GCC and Clang assembly sources assembled as GNU as does, linked and ran%s%s\n", passed, cases,
           runs_v4 ? "" : " (x86-64-v4 programs compared, not run: the host lacks AVX-512)",
           gnu_padding ? "" : " (this GNU as orders code padding unlike binutils 2.47: its padding was taken as ours)");
    arena_release(&files);
}
