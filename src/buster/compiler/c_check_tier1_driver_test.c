// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1's rematerialization (#178) through the C driver's --tier1. An original fixture's loop keeps more addresses and
// constants live than there are registers, each recipe among them: statics' addresses from rip, a static array's
// member's at an addend, extern variables' from their GOT slots, frame objects' addresses and a member's of one, and
// constants of each encoding (a zero, a 32-bit one, a sign-extended and a 64-bit one), each a value the C driver's typed
// stream converts (its pointers are 64-bit integers). A second fixture's loop divides by constants and by a register
// while such values stay live across the divisions, whose sequences take rax and rdx (#141). Clang's build of the same
// source is each one's oracle. Included by c_checker_driver_test.c after its ELF readers.

// The fixture: tier1_remat is the function under test, main prints its results.
static const char check_test_tier1_remat_source[] =
    "#include <stdio.h>\n"
    "#include <unistd.h>\n"
    "extern char** environ;\n"
    "static long s0, s1, s2, s3, s4, s5;\n"
    "static long sa[8];\n"
    "long tier1_remat(long n)\n"
    "{\n"
    "    long a0[2], a1[2], a2[2], a3[2], a4[2], a5[4];\n"
    "    long *p0 = a0, *p1 = a1, *p2 = a2, *p3 = a3, *p4 = a4, *p5 = &a5[1];\n"
    "    long *q0 = &s0, *q1 = &s1, *q2 = &s2, *q3 = &s3, *q4 = &s4, *q5 = &s5, *q6 = &sa[3];\n"
    "    char*** r0 = &environ;\n"
    "    int* r1 = &opterr;\n"
    "    int* r2 = &optind;\n"
    "    const long zero = 0;\n"
    "    const long middle = 0x87654321l;\n"
    "    const long wide = 0x123456789abcdefl;\n"
    "    const long negative = -0x76543210l;\n"
    "    long total = 0;\n"
    "    for (long k = 0; k < 2; k += 1)\n"
    "    {\n"
    "        p0[k] = k; p1[k] = k + 1; p2[k] = k + 2; p3[k] = k + 3; p4[k] = k + 4; p5[k] = k + 5;\n"
    "    }\n"
    "    a5[0] = 0;\n"
    "    a5[3] = 0;\n"
    "    for (long i = 0; i < n; i += 1)\n"
    "    {\n"
    "        const long k = i & 1;\n"
    "        p0[k] += i; p1[k] ^= i; p2[k] -= i; p3[k] += i * 3; p4[k] ^= i + 7; p5[k] += i ^ 5;\n"
    "        q0[0] += i; q1[0] ^= i; q2[0] -= i; q3[0] += i * 5; q4[0] ^= i + 3; q5[0] += i ^ 9; q6[k] += i;\n"
    "        total += (*r0 != 0) + *r1 + *r2;\n"
    "        total = total * 31 + wide;\n"
    "        total += middle;\n"
    "        p5[k ^ 1] = zero;\n"
    "        q6[k ^ 1] = negative;\n"
    "    }\n"
    "    return total + p0[0] + p0[1] + p1[0] + p1[1] + p2[0] + p2[1] + p3[0] + p3[1] + p4[0] + p4[1] + a5[0] + a5[1] + a5[2] + a5[3] +\n"
    "           s0 + s1 + s2 + s3 + s4 + s5 + sa[3] + sa[4];\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    printf(\"%ld %ld %ld\\n\", tier1_remat(0), tier1_remat(7), tier1_remat(1000));\n"
    "    return 0;\n"
    "}\n";

// The division fixture: tier1_divide is the function under test. Its loop divides by eight constants, each a reciprocal's
// sequence of another shape (64-bit signed by 10 and its remainder by 641, unsigned remainder by 1000003, the add
// sequence's division by 7 and the even divisor's pre-shift by 14; 32-bit signed by -7, unsigned remainder by 10 and
// division by 7), and by a register, while statics' and frame objects' addresses and constants stay live. main's calls
// leave nothing live across them, so the saves its build reports are tier1_divide's (check_test_tier1_divide_main).
static const char check_test_tier1_divide_source[] =
    "#include <stdio.h>\n"
    "static long s0, s1, s2, s3;\n"
    "static unsigned long su[4];\n"
    "unsigned long tier1_divide(long n, long d)\n"
    "{\n"
    "    long a0[2], a1[2], a2[2];\n"
    "    long *p0 = a0, *p1 = a1, *p2 = a2;\n"
    "    long *q0 = &s0, *q1 = &s1, *q2 = &s2, *q3 = &s3;\n"
    "    unsigned long* u = &su[1];\n"
    "    const unsigned long wide = 0x123456789abcdefull;\n"
    "    const unsigned long middle = 0x87654321ull;\n"
    "    unsigned long total = 0;\n"
    "    for (long k = 0; k < 2; k += 1)\n"
    "    {\n"
    "        p0[k] = k; p1[k] = k + 1; p2[k] = k + 2;\n"
    "    }\n"
    "    for (long i = 1; i <= n; i += 1)\n"
    "    {\n"
    "        const long k = i & 1;\n"
    "        const unsigned long x = (unsigned long)i * wide;\n"
    "        p0[k] += i / 10;\n"
    "        p1[k] ^= (long)(x % 1000003u);\n"
    "        p2[k] -= (int)i / -7;\n"
    "        q0[0] += (unsigned)x % 10u;\n"
    "        q1[0] ^= i / d;\n"
    "        q2[0] += i % 641;\n"
    "        q3[0] ^= (long)(x / 7u);\n"
    "        u[k] += x / 14u + (unsigned)x / 7u;\n"
    "        total = total * 31u + wide + middle + (unsigned long)p0[k];\n"
    "    }\n"
    "    return total + (unsigned long)(p0[0] + p0[1] + p1[0] + p1[1] + p2[0] + p2[1]) + (unsigned long)(s0 + s1 + s2 + s3) + su[1] + su[2];\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    printf(\"%lu\\n\", tier1_divide(0, 3));\n"
    "    printf(\"%lu\\n\", tier1_divide(7, 3));\n"
    "    printf(\"%lu\\n\", tier1_divide(1000, 7));\n"
    "    return 0;\n"
    "}\n";

// The division fixture's main alone, whose build must save nothing.
static const char check_test_tier1_divide_main[] =
    "#include <stdio.h>\n"
    "unsigned long tier1_divide(long n, long d);\n"
    "int main(void)\n"
    "{\n"
    "    printf(\"%lu\\n\", tier1_divide(0, 3));\n"
    "    printf(\"%lu\\n\", tier1_divide(7, 3));\n"
    "    printf(\"%lu\\n\", tier1_divide(1000, 7));\n"
    "    return 0;\n"
    "}\n";

// The labels fixture (#789): every function in it has labels and indirect jumps, each one's shape another.
// tier1_interpret's computed gotos go through its dispatch block, which the lowering puts last, with values live across
// it in a loop; tier1_classify's switch is a jump table, with values live across it; tier1_pick jumps once through a ?:
// of two labels' addresses; tier1_both enters a label directly and through its table, where a phi meets values that stay
// live apart, so that each edge into it has moves and the indirect jump's has a stub, where the label's symbol goes;
// tier1_once loads its target from its table; tier1_single can jump to one label alone.
static const char check_test_tier1_labels_source[] =
    "#include <stdio.h>\n"
    "static long tier1_interpret(const unsigned char* code, long n)\n"
    "{\n"
    "    static void* const table[] = { &&op_add, &&op_mul, &&op_sub, &&op_halt };\n"
    "    long acc = 1;\n"
    "    long steps = 0;\n"
    "    const unsigned char* pc = code;\n"
    "    goto *table[*pc];\n"
    "op_add:\n"
    "    acc += n + steps;\n"
    "    steps += 1;\n"
    "    goto *table[*++pc];\n"
    "op_mul:\n"
    "    acc *= 3;\n"
    "    steps += 1;\n"
    "    goto *table[*++pc];\n"
    "op_sub:\n"
    "    acc -= n;\n"
    "    steps += 1;\n"
    "    goto *table[*++pc];\n"
    "op_halt:\n"
    "    return acc * 1000 + steps;\n"
    "}\n"
    "static long tier1_classify(long x, long y)\n"
    "{\n"
    "    long r = y;\n"
    "    long q = x ^ y;\n"
    "    for (long i = 0; i < x; i += 1)\n"
    "    {\n"
    "        switch ((i * 7 + y) % 12)\n"
    "        {\n"
    "        case 0: r += 3; break;\n"
    "        case 1: r ^= i; break;\n"
    "        case 2: r -= 5; q += r; break;\n"
    "        case 3: r *= 2; break;\n"
    "        case 4: r += i * i; break;\n"
    "        case 5: r >>= 1; break;\n"
    "        case 6: r += 11; q ^= i; break;\n"
    "        case 7: r -= i; break;\n"
    "        case 8: r |= 4; break;\n"
    "        case 9: r &= 0xffff; break;\n"
    "        case 10: r += 100; break;\n"
    "        default: r -= 1; break;\n"
    "        }\n"
    "    }\n"
    "    return r * 31 + q;\n"
    "}\n"
    "static int tier1_pick(int p)\n"
    "{\n"
    "    void* t = p ? &&a : &&b;\n"
    "    goto *t;\n"
    "a:\n"
    "    return 10;\n"
    "b:\n"
    "    return 20;\n"
    "}\n"
    "static long tier1_both(long x)\n"
    "{\n"
    "    static void* const targets[] = { &&first, &&second };\n"
    "    long a = x * 3;\n"
    "    long b = x * 5 + 1;\n"
    "    long r;\n"
    "    if (x & 1)\n"
    "    {\n"
    "        r = a;\n"
    "        goto first;\n"
    "    }\n"
    "    r = b;\n"
    "    goto *targets[(x >> 1) & 1];\n"
    "first:\n"
    "    return r * 7 + (a ^ b);\n"
    "second:\n"
    "    return r - a;\n"
    "}\n"
    "static int tier1_once(int i)\n"
    "{\n"
    "    static void* const table[] = { &&a, &&b, &&c };\n"
    "    goto *table[i];\n"
    "a:\n"
    "    return 1;\n"
    "b:\n"
    "    return 2;\n"
    "c:\n"
    "    return 3;\n"
    "}\n"
    "static int tier1_single(int x)\n"
    "{\n"
    "    void* t = &&only;\n"
    "    goto *t;\n"
    "only:\n"
    "    return x + 1;\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    static const unsigned char program[] = { 0, 1, 2, 0, 1, 1, 2, 0, 3 };\n"
    "    printf(\"%ld %ld %ld\\n\", tier1_interpret(program, 5), tier1_classify(1000, 3), tier1_classify(77, 9));\n"
    "    printf(\"%d %d\\n\", tier1_pick(1), tier1_pick(0));\n"
    "    printf(\"%ld %ld %ld %ld\\n\", tier1_both(5), tier1_both(6), tier1_both(8), tier1_both(-3));\n"
    "    printf(\"%d %d %d %d\\n\", tier1_once(0), tier1_once(1), tier1_once(2), tier1_single(41));\n"
    "    return 0;\n"
    "}\n";

// The opmask fixture (#179): tier1_masks is the function under test, every _k* builtin's row of its two words' masks (the
// words truncated to each width, as the builtins' prototypes convert them), folded into one hash; main prints the hash
// over pairs of words around each width's edges.
static const char check_test_tier1_masks_source[] =
    "#include <stdio.h>\n"
    "typedef unsigned long long u64;\n"
    "#define B(name) h = h * 31 + __builtin_ia32_##name(a, b);\n"
    "#define U(name) h = h * 31 + __builtin_ia32_##name(a);\n"
    "#define S(name) h = h * 31 + __builtin_ia32_##name(a, 0); h = h * 31 + __builtin_ia32_##name(a, 1); h = h * 31 + __builtin_ia32_##name(a, 7); \\\n"
    "    h = h * 31 + __builtin_ia32_##name(a, 8); h = h * 31 + __builtin_ia32_##name(a, 31); h = h * 31 + __builtin_ia32_##name(a, 64);\n"
    "u64 tier1_masks(u64 a, u64 b)\n"
    "{\n"
    "    u64 h = 0;\n"
    "    B(kandqi) B(kandhi) B(kandsi) B(kanddi) B(kandnqi) B(kandnhi) B(kandnsi) B(kandndi)\n"
    "    B(korqi) B(korhi) B(korsi) B(kordi) B(kxorqi) B(kxorhi) B(kxorsi) B(kxordi)\n"
    "    B(kxnorqi) B(kxnorhi) B(kxnorsi) B(kxnordi) B(kaddqi) B(kaddhi) B(kaddsi) B(kadddi)\n"
    "    B(kunpckhi) B(kunpcksi) B(kunpckdi)\n"
    "    B(kortestzqi) B(kortestzhi) B(kortestzsi) B(kortestzdi) B(kortestcqi) B(kortestchi) B(kortestcsi) B(kortestcdi)\n"
    "    B(ktestzqi) B(ktestzhi) B(ktestzsi) B(ktestzdi) B(ktestcqi) B(ktestchi) B(ktestcsi) B(ktestcdi)\n"
    "    U(knotqi) U(knothi) U(knotsi) U(knotdi) U(kmovb) U(kmovw) U(kmovd) U(kmovq)\n"
    "    S(kshiftliqi) S(kshiftlihi) S(kshiftlisi) S(kshiftlidi) S(kshiftriqi) S(kshiftrihi) S(kshiftrisi) S(kshiftridi)\n"
    "    return h;\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    static const u64 words[] = { 0, 1, 0x80, 0xff, 0x8000, 0xffff, 0x80000000ull, 0xffffffffull, 0x8000000000000000ull, ~0ull,\n"
    "                                 0x0123456789abcdefull, 0x5555aaaa33330f0full };\n"
    "    u64 h = 0;\n"
    "    for (unsigned i = 0; i < 12; i += 1)\n"
    "    {\n"
    "        for (unsigned j = 0; j < 12; j += 1)\n"
    "        {\n"
    "            h = h * 1000003 + tier1_masks(words[i], words[j]);\n"
    "        }\n"
    "    }\n"
    "    printf(\"%llx\\n\", h);\n"
    "    return 0;\n"
    "}\n";

// The value of `name` (as "spill_stores") in --tier1-report's line of `text`, into *value: whether it has one.
static bool check_test_tier1_field(const char* text, const char* name, uint64_t* value)
{
    const char* line = text != NULL ? strstr(text, "tier1\tfunctions=") : NULL;
    char key[64];
    snprintf(key, sizeof(key), "\t%s=", name);
    const char* end = line != NULL ? strchr(line, '\n') : NULL;
    const char* field = line != NULL ? strstr(line, key) : NULL;
    const bool found = field != NULL && (end == NULL || field < end);
    *value = found ? strtoull(field + strlen(key), NULL, 10) : 0;
    return found;
}

// The function `name` in `-c`'s object at `path`: its bytes, decoded by the scalar disassembler into `records` (room for
// `capacity`), and its relocations, each one's offset from the function's start and type (as R_X86_64_PC32) in
// `offsets` and `types` (room for `capacity` too). Returns how many records, *relocation_count set, or 0 on a failure.
static uint32_t check_test_tier1_function(const char* path, const char* name, X86Decoded* records, uint32_t capacity, uint32_t* offsets, uint32_t* types, uint32_t* relocation_count)
{
    uint64_t length = 0;
    uint8_t* bytes = check_test_driver_read_aligned(path, &length);
    CheckTestElf elf;
    bool valid = bytes != NULL && check_test_elf_open(&elf, (String8){ .pointer = (char*)bytes, .length = length });
    const Elf64_Sym* function = NULL;
    for (uint64_t i = 1; valid && function == NULL && i < elf.symbol_count; i += 1)
    {
        const Elf64_Sym* symbol = &elf.symbols[i];
        const char* symbol_name = check_test_elf_string(elf.symbol_names, elf.symbol_names_size, symbol->st_name);
        const bool named = symbol_name != NULL && strcmp(symbol_name, name) == 0 && ELF64_ST_TYPE(symbol->st_info) == STT_FUNC && symbol->st_shndx != SHN_UNDEF &&
                           symbol->st_shndx < elf.section_count;
        function = named ? symbol : NULL;
    }
    const Elf64_Shdr* text = function != NULL ? &elf.sections[function->st_shndx] : NULL;
    valid = valid && text != NULL && function->st_size != 0 && function->st_value <= text->sh_size && function->st_size <= text->sh_size - function->st_value &&
            check_test_elf_range(elf.file, text->sh_offset, text->sh_size);
    const X86DecodeResult decoded = valid ? x86_disassemble_scalar((const uint8_t*)bytes + text->sh_offset + function->st_value, function->st_size, records, capacity)
                                          : (X86DecodeResult){ .status = X86_DECODE_BAD_ARGUMENT };
    valid = valid && decoded.status == X86_DECODE_OK && decoded.consumed == function->st_size;
    *relocation_count = 0;
    for (uint32_t s = 0; valid && s < elf.section_count; s += 1)
    {
        const Elf64_Shdr* section = &elf.sections[s];
        const bool applies = section->sh_type == SHT_RELA && section->sh_info == function->st_shndx;
        valid = !applies || (section->sh_entsize == sizeof(Elf64_Rela) && check_test_elf_range(elf.file, section->sh_offset, section->sh_size));
        const Elf64_Rela* relocations = applies && valid ? (const Elf64_Rela*)(bytes + section->sh_offset) : NULL;
        for (uint64_t r = 0; relocations != NULL && valid && r < section->sh_size / sizeof(Elf64_Rela); r += 1)
        {
            const uint64_t offset = relocations[r].r_offset;
            const bool inside = offset >= function->st_value && offset < function->st_value + function->st_size;
            valid = !inside || *relocation_count < capacity;
            if (inside && valid)
            {
                offsets[*relocation_count] = (uint32_t)(offset - function->st_value);
                types[*relocation_count] = (uint32_t)ELF64_R_TYPE(relocations[r].r_info);
                *relocation_count += 1;
            }
        }
    }
    free(bytes);
    return valid ? (uint32_t)decoded.count : 0;
}

// The status flags (CF, PF, AF, ZF, SF and OF) live before decoded record `index`, given those live after it, live[index]:
// those it reads, and those live after it that it neither writes nor leaves undefined (x86_semantics.c).
static uint8_t check_test_tier1_live_in(const X86Decoded* records, const uint8_t* live, uint32_t index)
{
    const X86Effects* effects = x86_effects(records[index].instruction.form & BUSTER_X86_FORM_NUMBER);
    return (uint8_t)(effects->flags_read | (live[index] & ~(effects->flags_written | effects->flags_undefined)));
}

// The status flags live after each of `count` decoded records of one function, into `live`: a backward fixpoint over
// the records' successors, the next record (but after a jump, a return or a trap) and a direct jump's or branch's target.
// A record whose control the effects can't follow (an indirect jump, an uncovered form), a jump out of the function and
// falling off its end keep every flag live.
static void check_test_tier1_flags(const X86Decoded* records, uint32_t count, uint8_t* live)
{
    const uint8_t status = BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_PF | BUSTER_X86_FLAG_AF | BUSTER_X86_FLAG_ZF | BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF;
    memset(live, 0, count);
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (uint32_t k = count; k > 0; k -= 1)
        {
            const uint32_t i = k - 1;
            const uint32_t form = records[i].instruction.form & BUSTER_X86_FORM_NUMBER;
            const X86Effects* effects = x86_effects(form);
            const uint32_t control = (uint32_t)effects->kinds >> 4;
            const int64_t goal = (int64_t)records[i].offset + records[i].length + records[i].instruction.displacement;
            // The target's record, by halving over the records' offsets.
            uint32_t low = 0;
            uint32_t high = count;
            while (low < high)
            {
                const uint32_t middle = low + (high - low) / 2;
                low = (int64_t)records[middle].offset < goal ? middle + 1 : low;
                high = (int64_t)records[middle].offset < goal ? high : middle;
            }
            const bool targeted = x86_form_descriptions[form].relative_bytes != 0 && low < count && (int64_t)records[low].offset == goal;
            const bool next = control == BUSTER_X86_CONTROL_NONE || control == BUSTER_X86_CONTROL_CALL || control == BUSTER_X86_CONTROL_BRANCH;
            const bool jumps = control == BUSTER_X86_CONTROL_JUMP || control == BUSTER_X86_CONTROL_BRANCH;
            const bool unknown = control == BUSTER_X86_CONTROL_UNKNOWN || effects->operation == X86_OPERATION_NONE;
            const uint8_t falls = next ? (i + 1 < count ? check_test_tier1_live_in(records, live, i + 1) : status) : 0;
            const uint8_t taken = jumps ? (targeted ? check_test_tier1_live_in(records, live, low) : status) : 0;
            const uint8_t after = (uint8_t)((falls | taken | (unknown ? status : 0)) & status);
            changed |= after != live[i];
            live[i] = after;
        }
    }
}

// Whether a decoded record is a zeroing xor of a register (any width the selector and Tier 1 emit).
static bool check_test_tier1_zeroing(const X86Instruction* instruction)
{
    const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
    const bool exclusive = form == X86_FORM_XOR_RM32_R32 || form == X86_FORM_XOR_R32_RM32 || form == X86_FORM_XOR_RM64_R64 || form == X86_FORM_XOR_R64_RM64;
    return exclusive && instruction->mode == BUSTER_X86_REGISTER_DIRECT && instruction->reg == instruction->rm;
}

// What one build of the fixture by the driver did: its report's fields.
typedef struct CheckTestTier1Build CheckTestTier1Build;
struct CheckTestTier1Build
{
    uint64_t rematerializations;
    uint64_t dropped;
    uint64_t stores;
    uint64_t loads;
    uint64_t slots;
    uint64_t frame;
    uint64_t call_stores;
    bool reported;
};

// A fixture compiled by --tier1 with and without rematerialization (`rematerialize`) by each path, into an executable
// (`-o output`) or with `object` an object (-c), with `option` (as -march=x86-64-v4) where it isn't NULL, whose report
// goes into *build: whether it compiled with its function `name` by Tier 1 (no refusal line names it), or with an empty
// name every function.
static bool check_test_tier1_build(const char* executable, const char* source, const char* name, const char* output, bool rematerialize, bool scalar, bool object, const char* option,
                                   CheckTestTier1Build* build)
{
    char* arguments[16] = { (char*)executable, "--tier1", "--tier1-report", "--tier1-refusals", "--verify" };
    uint32_t count = 5;
    if (option != NULL)
    {
        arguments[count++] = (char*)option;
    }
    if (!rematerialize)
    {
        arguments[count++] = "--tier1-no-rematerialize";
    }
    if (scalar)
    {
        arguments[count++] = "--scalar";
    }
    if (object)
    {
        arguments[count++] = "-c";
    }
    arguments[count++] = "-o";
    arguments[count++] = (char*)output;
    arguments[count++] = (char*)source;
    arguments[count] = NULL;
    char* text = NULL;
    const int status = check_test_driver_spawn(arguments, &text);
    *build = (CheckTestTier1Build){ 0 };
    build->reported = check_test_tier1_field(text, "rematerializations", &build->rematerializations) && check_test_tier1_field(text, "dropped_definitions", &build->dropped) &&
                      check_test_tier1_field(text, "spill_stores", &build->stores) && check_test_tier1_field(text, "spill_loads", &build->loads) &&
                      check_test_tier1_field(text, "spill_slots", &build->slots) && check_test_tier1_field(text, "frame_bytes", &build->frame) &&
                      check_test_tier1_field(text, "call_stores", &build->call_stores);
    char refused[128];
    snprintf(refused, sizeof(refused), "tier1-refused\t%s%s", name, name[0] == 0 ? "" : "\t");
    const bool taken = text != NULL && strstr(text, refused) == NULL;
    if (status != 0 && text != NULL)
    {
        fprintf(stderr, "%s", text);
    }
    free(text);
    return status == 0 && build->reported && taken;
}

// The fixture's object, decoded: every zeroing xor leaves no status flag live (#178: a recomputed zero is an xor only
// where no flag is live, as every point between two nodes' records is), there is one at least, and every instruction
// that addresses from rip has its own relocation at its displacement, a PC32 for a lea and a GOT one for a load (each
// recomputed address one more). Into *loop_addresses, how many instructions in the loop (the range of its back edge, the
// last backward jump) address from rip or from rsp's frame: the recomputed addresses there; and into *zeroing_count, its
// zeroing xors.
static bool check_test_tier1_code(CheckTest* test, const char* object, uint32_t* loop_addresses, uint32_t* zeroing_count)
{
    enum
    {
        CHECK_TEST_TIER1_RECORDS = 4096,
    };
    X86Decoded* records = calloc(CHECK_TEST_TIER1_RECORDS, sizeof(X86Decoded));
    uint32_t* offsets = calloc(CHECK_TEST_TIER1_RECORDS, sizeof(uint32_t));
    uint32_t* types = calloc(CHECK_TEST_TIER1_RECORDS, sizeof(uint32_t));
    uint8_t* live = calloc(CHECK_TEST_TIER1_RECORDS, 1);
    uint32_t relocations = 0;
    const uint32_t count = records != NULL && offsets != NULL && types != NULL && live != NULL ? check_test_tier1_function(object, "tier1_remat", records, CHECK_TEST_TIER1_RECORDS, offsets, types, &relocations) : 0;
    check_test_expect(test, count != 0, "the fixture's function decodes, with its relocations");
    if (count != 0)
    {
        check_test_tier1_flags(records, count, live);
    }
    uint32_t zeroing = 0;
    bool safe = true;
    bool referenced = true;
    uint32_t addressed = 0;
    // The loop: from the last backward jump's target to it.
    uint32_t loop_first = count;
    uint32_t loop_last = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const X86Instruction* instruction = &records[i].instruction;
        const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
        const bool jump = x86_form_descriptions[form].relative_bytes != 0 && (x86_effects(form)->kinds >> 4) != BUSTER_X86_CONTROL_CALL;
        const int64_t goal = (int64_t)records[i].offset + records[i].length + instruction->displacement;
        for (uint32_t j = 0; jump && goal < (int64_t)records[i].offset && j < count; j += 1)
        {
            loop_first = (int64_t)records[j].offset == goal ? j : loop_first;
            loop_last = (int64_t)records[j].offset == goal ? i : loop_last;
        }
        if (check_test_tier1_zeroing(instruction))
        {
            zeroing += 1;
            safe &= live[i] == 0;
        }
        if (instruction->mode == BUSTER_X86_MEMORY_RIP && (form == X86_FORM_LEA_R64_M || form == X86_FORM_MOV_R64_RM64))
        {
            // The displacement is the record's last four bytes: neither form has an immediate.
            const uint32_t field = records[i].offset + records[i].length - 4;
            const uint32_t expected = form == X86_FORM_LEA_R64_M ? R_X86_64_PC32 : R_X86_64_REX_GOTPCRELX;
            uint32_t found = 0;
            for (uint32_t r = 0; r < relocations; r += 1)
            {
                found += offsets[r] == field && types[r] == expected;
            }
            referenced &= found == 1;
        }
    }
    for (uint32_t i = loop_first; i <= loop_last && i < count; i += 1)
    {
        const X86Instruction* instruction = &records[i].instruction;
        const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
        const bool rip = instruction->mode == BUSTER_X86_MEMORY_RIP && (form == X86_FORM_LEA_R64_M || form == X86_FORM_MOV_R64_RM64);
        const bool frame = instruction->mode == BUSTER_X86_MEMORY_BASE && instruction->rm == X86_REGISTER_RSP && form == X86_FORM_LEA_R64_M;
        addressed += rip || frame;
    }
    check_test_expect(test, count == 0 || (zeroing != 0 && safe), "every zeroing xor of Tier 1's code leaves no flag live, and the fixture has one");
    check_test_expect(test, count == 0 || referenced, "each instruction that addresses from rip has a relocation of its own");
    check_test_expect(test, count == 0 || loop_first < loop_last, "the fixture's loop is found by its back edge");
    *loop_addresses = addressed;
    *zeroing_count = zeroing;
    free(records);
    free(offsets);
    free(types);
    free(live);
    return count != 0;
}

// See the file's header: each build's program prints clang's output, rematerialization recomputes in the loop what its
// absence reloads, with fewer spills, slots and frame bytes, and its objects' code passes check_test_tier1_code.
static void check_test_driver_tier1_rematerialization(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char source[512];
    char reference[512];
    snprintf(source, sizeof(source), "%s/tier1-remat.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-remat-clang", directory);
    const bool written = parse_test_write(source, check_test_tier1_remat_source, sizeof(check_test_tier1_remat_source) - 1);
    char* clang_arguments[] = { (char*)clang, "-O0", "-o", reference, source, NULL };
    char* clang_text = NULL;
    const bool referenced = written && check_test_driver_spawn(clang_arguments, &clang_text) == 0;
    free(clang_text);
    char expected[256] = { 0 };
    const bool ran = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of the rematerialization fixture runs");
    const bool ready = ran && (x86_semantics_initialized || x86_semantics_initialize()) && x86_disassembler_initialize();
    check_test_expect(test, ready, "the semantics and the disassembler's tables");
    CheckTestTier1Build builds[2][2];
    uint32_t addresses[2][2] = { { 0 } };
    uint32_t zeroing[2][2] = { { 0 } };
    for (uint32_t rematerialize = 0; ready && rematerialize < 2; rematerialize += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char output[512];
            char object[512];
            char linked[512];
            snprintf(output, sizeof(output), "%s/tier1-remat-%u-%u", directory, rematerialize, path);
            snprintf(object, sizeof(object), "%s/tier1-remat-%u-%u.o", directory, rematerialize, path);
            snprintf(linked, sizeof(linked), "%s/tier1-remat-%u-%u-cc", directory, rematerialize, path);
            // The executable by the direct path, and the object, which cc links.
            CheckTestTier1Build object_build;
            const bool built = check_test_tier1_build(executable, source, "tier1_remat", output, rematerialize != 0, path != 0, false, NULL, &builds[rematerialize][path]);
            const bool compiled = check_test_tier1_build(executable, source, "tier1_remat", object, rematerialize != 0, path != 0, true, NULL, &object_build);
            check_test_expect(test, built && compiled, "the fixture compiles by --tier1, its function Tier 1's");
            char* cc_arguments[] = { "cc", "-o", linked, object, NULL };
            char* cc_text = NULL;
            const bool cc_linked = compiled && check_test_driver_spawn(cc_arguments, &cc_text) == 0;
            free(cc_text);
            char actual[256] = { 0 };
            char from_object[256] = { 0 };
            check_test_expect(test, built && check_test_driver_capture(output, actual, sizeof(actual)) && strcmp(actual, expected) == 0, "the executable prints clang's build's output");
            check_test_expect(test, cc_linked && check_test_driver_capture(linked, from_object, sizeof(from_object)) && strcmp(from_object, expected) == 0,
                              "the object, linked by cc, prints clang's build's output");
            const bool decoded = compiled && check_test_tier1_code(test, object, &addresses[rematerialize][path], &zeroing[rematerialize][path]);
            check_test_expect(test, !decoded || (rematerialize != 0 ? addresses[rematerialize][path] != 0 : addresses[rematerialize][path] == 0),
                              rematerialize != 0 ? "the loop recomputes addresses from rip and the frame" : "without rematerialization the loop computes no address");
            unlink(output);
            unlink(object);
            unlink(linked);
        }
    }
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        const CheckTestTier1Build* with = &builds[1][path];
        const CheckTestTier1Build* without = &builds[0][path];
        check_test_expect(test, with->rematerializations != 0 && with->dropped != 0 && without->rematerializations == 0 && without->dropped == 0,
                          "the report counts recomputed values and the definitions left out only with rematerialization");
        check_test_expect(test, with->stores < without->stores && with->loads < without->loads && with->slots < without->slots && with->frame < without->frame,
                          "rematerialization leaves fewer spill stores, reloads, slots and frame bytes");
        printf("  Tier 1's rematerialization, %s path: stores %" PRIu64 " (%" PRIu64 " without), reloads %" PRIu64 " (%" PRIu64 "), slots %" PRIu64 " (%" PRIu64 "), frame bytes %" PRIu64
               " (%" PRIu64 "); %" PRIu64 " recomputed, %" PRIu64 " definitions left out, %u addresses recomputed in the loop, %u zeroing xors\n",
               path == 0 ? "vector" : "scalar", with->stores, without->stores, with->loads, without->loads, with->slots, without->slots, with->frame, without->frame,
               with->rematerializations, with->dropped, addresses[1][path], zeroing[1][path]);
    }
    unlink(source);
    unlink(reference);
}

// The division fixture (#141), by each path, with and without rematerialization: it prints clang's build's output; its
// function's object has a one-operand multiplication for each of the eight constant divisors and one div, for the
// register; values left in rax and rdx past the divisions are saved around them (main alone saves nothing); and with
// rematerialization (#178) values are recomputed and definitions left out among the divisions, without it none.
static void check_test_driver_tier1_divide(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    enum
    {
        CHECK_TEST_TIER1_DIVIDE_RECORDS = 4096,
    };
    char source[512];
    char main_source[512];
    char reference[512];
    snprintf(source, sizeof(source), "%s/tier1-divide.c", directory);
    snprintf(main_source, sizeof(main_source), "%s/tier1-divide-main.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-divide-clang", directory);
    const bool written = parse_test_write(source, check_test_tier1_divide_source, sizeof(check_test_tier1_divide_source) - 1) &&
                         parse_test_write(main_source, check_test_tier1_divide_main, sizeof(check_test_tier1_divide_main) - 1);
    char* clang_arguments[] = { (char*)clang, "-O0", "-o", reference, source, NULL };
    char* clang_text = NULL;
    const bool referenced = written && check_test_driver_spawn(clang_arguments, &clang_text) == 0;
    free(clang_text);
    char expected[256] = { 0 };
    const bool ran = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of the division fixture runs");
    const bool ready = ran && x86_disassembler_initialize();
    X86Decoded* records = calloc(CHECK_TEST_TIER1_DIVIDE_RECORDS, sizeof(X86Decoded));
    uint32_t* offsets = calloc(CHECK_TEST_TIER1_DIVIDE_RECORDS, sizeof(uint32_t));
    uint32_t* types = calloc(CHECK_TEST_TIER1_DIVIDE_RECORDS, sizeof(uint32_t));
    CheckTestTier1Build builds[2][2] = { { { 0 } } };
    for (uint32_t rematerialize = 0; ready && rematerialize < 2; rematerialize += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char output[512];
            char object[512];
            char main_object[512];
            snprintf(output, sizeof(output), "%s/tier1-divide-%u-%u", directory, rematerialize, path);
            snprintf(object, sizeof(object), "%s/tier1-divide-%u-%u.o", directory, rematerialize, path);
            snprintf(main_object, sizeof(main_object), "%s/tier1-divide-main-%u-%u.o", directory, rematerialize, path);
            CheckTestTier1Build object_build;
            CheckTestTier1Build main_build;
            const bool built = check_test_tier1_build(executable, source, "tier1_divide", output, rematerialize != 0, path != 0, false, NULL, &builds[rematerialize][path]);
            const bool compiled = check_test_tier1_build(executable, source, "tier1_divide", object, rematerialize != 0, path != 0, true, NULL, &object_build);
            const bool main_compiled = check_test_tier1_build(executable, main_source, "main", main_object, rematerialize != 0, path != 0, true, NULL, &main_build);
            check_test_expect(test, built && compiled && main_compiled, "the division fixture compiles by --tier1, its functions Tier 1's");
            check_test_expect(test, main_compiled && main_build.call_stores == 0, "the division fixture's main alone saves nothing around its calls");
            char actual[256] = { 0 };
            check_test_expect(test, built && check_test_driver_capture(output, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                              "the division fixture prints clang's build's output");
            uint32_t relocations = 0;
            const uint32_t count = compiled && records != NULL && offsets != NULL && types != NULL ?
                                       check_test_tier1_function(object, "tier1_divide", records, CHECK_TEST_TIER1_DIVIDE_RECORDS, offsets, types, &relocations) : 0;
            uint32_t multiplications = 0;
            uint32_t divisions = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t form = records[i].instruction.form & BUSTER_X86_FORM_NUMBER;
                multiplications += form == X86_FORM_MUL_RM32 || form == X86_FORM_MUL_RM64 || form == X86_FORM_IMUL_RM32 || form == X86_FORM_IMUL_RM64;
                divisions += form == X86_FORM_DIV_RM32 || form == X86_FORM_DIV_RM64 || form == X86_FORM_IDIV_RM32 || form == X86_FORM_IDIV_RM64;
            }
            check_test_expect(test, count != 0 && multiplications == 8 && divisions == 1,
                              "each of the fixture's eight constant divisors is a reciprocal's multiplication, its register divisor a div");
            unlink(output);
            unlink(object);
            unlink(main_object);
        }
    }
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        const CheckTestTier1Build* with = &builds[1][path];
        const CheckTestTier1Build* without = &builds[0][path];
        check_test_expect(test, with->call_stores != 0 && without->call_stores != 0, "values left in rax and rdx past the divisions are saved around them");
        check_test_expect(test, with->rematerializations != 0 && with->dropped != 0 && without->rematerializations == 0 && without->dropped == 0,
                          "with rematerialization, values are recomputed and definitions left out among the divisions, and without it none");
        printf("  Tier 1's divisions, %s path: %" PRIu64 " saves around them (%" PRIu64 " without rematerialization), %" PRIu64 " recomputed values, %" PRIu64
               " definitions left out\n",
               path == 0 ? "vector" : "scalar", with->call_stores, without->call_stores, with->rematerializations, with->dropped);
    }
    free(records);
    free(offsets);
    free(types);
    unlink(source);
    unlink(main_source);
    unlink(reference);
}

// The labels fixture's function `name` in the object at `path`: its indirect jumps through a register and through
// memory, into *registers and *memory, and into *trailing the direct jumps after its last indirect jump: its edges'
// stubs, each of which ends in one to its label's block, since the dispatch block is laid out last (#1016's review).
// Whether it was found.
static bool check_test_tier1_jumps(const char* path, const char* name, X86Decoded* records, uint32_t capacity, uint32_t* offsets, uint32_t* types, uint32_t* registers, uint32_t* memory,
                                   uint32_t* trailing)
{
    uint32_t relocations = 0;
    const uint32_t count = check_test_tier1_function(path, name, records, capacity, offsets, types, &relocations);
    *registers = 0;
    *memory = 0;
    *trailing = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t form = records[i].instruction.form & BUSTER_X86_FORM_NUMBER;
        const bool jump = form == X86_FORM_JMP_RM64;
        *registers += jump && records[i].instruction.mode == BUSTER_X86_REGISTER_DIRECT;
        *memory += jump && records[i].instruction.mode != BUSTER_X86_REGISTER_DIRECT;
        *trailing = jump ? 0 : *trailing + (form == X86_FORM_JMP_REL8 || form == X86_FORM_JMP_REL32);
    }
    return count != 0;
}

// The labels fixture (#789): by --tier1, every function Tier 1's, by each path with and without rematerialization, and
// its program prints what clang -O0's and Tier 0's builds of it print. Tier 1's indirect jumps are each function's own:
// one through a register where computed gotos share their dispatch block or a switch's table dispatches, none where the
// jump can go to one label alone, and one with a stub after it where an edge into a label has moves.
static void check_test_driver_tier1_labels(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    enum
    {
        CHECK_TEST_TIER1_LABELS_RECORDS = 4096,
    };
    char source[512];
    char reference[512];
    char tier0[512];
    snprintf(source, sizeof(source), "%s/tier1-labels.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-labels-clang", directory);
    snprintf(tier0, sizeof(tier0), "%s/tier1-labels-tier0", directory);
    const bool written = parse_test_write(source, check_test_tier1_labels_source, sizeof(check_test_tier1_labels_source) - 1);
    char* clang_arguments[] = { (char*)clang, "-O0", "-o", reference, source, NULL };
    char* tier0_arguments[] = { (char*)executable, "--verify", "-o", tier0, source, NULL };
    char* text = NULL;
    const bool referenced = written && check_test_driver_spawn(clang_arguments, &text) == 0;
    free(text);
    text = NULL;
    const bool tier0_built = written && check_test_driver_spawn(tier0_arguments, &text) == 0;
    free(text);
    char expected[512] = { 0 };
    char tier0_output[512] = { 0 };
    const bool ran = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of the labels fixture runs");
    check_test_expect(test, ran && tier0_built && check_test_driver_capture(tier0, tier0_output, sizeof(tier0_output)) && strcmp(tier0_output, expected) == 0,
                      "Tier 0's build of the labels fixture prints clang's build's output");
    const bool ready = ran && x86_disassembler_initialize();
    X86Decoded* records = calloc(CHECK_TEST_TIER1_LABELS_RECORDS, sizeof(X86Decoded));
    uint32_t* offsets = calloc(CHECK_TEST_TIER1_LABELS_RECORDS, sizeof(uint32_t));
    uint32_t* types = calloc(CHECK_TEST_TIER1_LABELS_RECORDS, sizeof(uint32_t));
    for (uint32_t rematerialize = 0; ready && rematerialize < 2; rematerialize += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char output[512];
            char object[512];
            snprintf(output, sizeof(output), "%s/tier1-labels-%u-%u", directory, rematerialize, path);
            snprintf(object, sizeof(object), "%s/tier1-labels-%u-%u.o", directory, rematerialize, path);
            CheckTestTier1Build build;
            CheckTestTier1Build object_build;
            const bool built = check_test_tier1_build(executable, source, "", output, rematerialize != 0, path != 0, false, NULL, &build);
            const bool compiled = check_test_tier1_build(executable, source, "", object, rematerialize != 0, path != 0, true, NULL, &object_build);
            check_test_expect(test, built && compiled, "the labels fixture compiles by --tier1, every function Tier 1's");
            char actual[512] = { 0 };
            check_test_expect(test, built && check_test_driver_capture(output, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                              "the labels fixture prints clang's build's output");
            static const char* const names[5] = { "tier1_interpret", "tier1_classify", "tier1_once", "tier1_single", "tier1_both" };
            uint32_t registers[5] = { 0 };
            uint32_t memory[5] = { 0 };
            uint32_t trailing[5] = { 0 };
            bool decoded = compiled && records != NULL && offsets != NULL && types != NULL;
            for (uint32_t f = 0; decoded && f < BUSTER_ARRAY_LENGTH(names); f += 1)
            {
                decoded = check_test_tier1_jumps(object, names[f], records, CHECK_TEST_TIER1_LABELS_RECORDS, offsets, types, &registers[f], &memory[f], &trailing[f]);
            }
            check_test_expect(test, decoded && registers[0] == 1 && memory[0] == 0 && registers[2] == 1 && memory[2] == 0,
                              "a function's computed gotos share one indirect jump through a register, in their dispatch block");
            check_test_expect(test, decoded && registers[1] == 1 && memory[1] == 0, "tier1_classify's switch is one indirect jump through a register");
            check_test_expect(test, decoded && registers[3] == 0 && memory[3] == 0, "tier1_single's jump to its one label is a direct jump");
            check_test_expect(test, decoded && registers[4] == 1 && trailing[4] != 0, "tier1_both's indirect jump has a stub after it, for the label its edge moves into");
            unlink(output);
            unlink(object);
        }
    }
    free(records);
    free(offsets);
    free(types);
    unlink(source);
    unlink(reference);
    unlink(tier0);
}

// The opmask fixture (#179), by each path, with and without rematerialization, for x86-64-v4: its function of every _k*
// builtin compiles by Tier 1 (its rows through ir_simd_typed.c's decoder, its masks in the opmask class), and where the
// host has AVX-512 F, BW, DQ and VL its executable prints clang -O0's build's output. Its object holds opmask
// instructions: the masks stay in k registers between their conversions.
static void check_test_driver_tier1_masks(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    enum
    {
        CHECK_TEST_TIER1_MASKS_RECORDS = 16384,
    };
    char source[512];
    char reference[512];
    snprintf(source, sizeof(source), "%s/tier1-masks.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-masks-clang", directory);
    const bool written = parse_test_write(source, check_test_tier1_masks_source, sizeof(check_test_tier1_masks_source) - 1);
    char* clang_arguments[] = { (char*)clang, "-O0", "-march=x86-64-v4", "-o", reference, source, NULL };
    char* clang_text = NULL;
    const bool referenced = written && check_test_driver_spawn(clang_arguments, &clang_text) == 0;
    free(clang_text);
    check_test_expect(test, referenced, "clang builds the opmask fixture");
    const uint64_t needed = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512DQ) | BUSTER_X86_FEATURE(AVX512VL);
    const bool host = (x86_target_native().features & needed) == needed;
    char expected[256] = { 0 };
    const bool ran = referenced && host && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, !host || ran, "clang's build of the opmask fixture runs");
    const bool ready = referenced && x86_disassembler_initialize();
    X86Decoded* records = calloc(CHECK_TEST_TIER1_MASKS_RECORDS, sizeof(X86Decoded));
    uint32_t* offsets = calloc(CHECK_TEST_TIER1_MASKS_RECORDS, sizeof(uint32_t));
    uint32_t* types = calloc(CHECK_TEST_TIER1_MASKS_RECORDS, sizeof(uint32_t));
    uint32_t opmask = 0;
    for (uint32_t rematerialize = 0; ready && rematerialize < 2; rematerialize += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char output[512];
            char object[512];
            snprintf(output, sizeof(output), "%s/tier1-masks-%u-%u", directory, rematerialize, path);
            snprintf(object, sizeof(object), "%s/tier1-masks-%u-%u.o", directory, rematerialize, path);
            CheckTestTier1Build build;
            CheckTestTier1Build object_build;
            const bool built = check_test_tier1_build(executable, source, "tier1_masks", output, rematerialize != 0, path != 0, false, "-march=x86-64-v4", &build);
            const bool compiled = check_test_tier1_build(executable, source, "tier1_masks", object, rematerialize != 0, path != 0, true, "-march=x86-64-v4", &object_build);
            check_test_expect(test, built && compiled, "the opmask fixture compiles by --tier1, its function of _k* builtins Tier 1's");
            char actual[256] = { 0 };
            check_test_expect(test, !host || (built && check_test_driver_capture(output, actual, sizeof(actual)) && strcmp(actual, expected) == 0),
                              "the opmask fixture prints clang's build's output");
            uint32_t relocations = 0;
            const uint32_t count = compiled && records != NULL && offsets != NULL && types != NULL ?
                                       check_test_tier1_function(object, "tier1_masks", records, CHECK_TEST_TIER1_MASKS_RECORDS, offsets, types, &relocations) : 0;
            uint32_t found = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                const X86FormDescription* description = x86_form_description(records[i].instruction.form & BUSTER_X86_FORM_NUMBER);
                found += (description->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_OPMASK || (description->rm & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_OPMASK;
            }
            check_test_expect(test, count != 0 && found != 0, "the opmask fixture's function keeps its masks in opmask registers");
            opmask = found > opmask ? found : opmask;
            unlink(output);
            unlink(object);
        }
    }
    printf("  Tier 1's opmask builtins: %u instructions on opmask registers%s\n", opmask, host ? "" : " (compiled only: no AVX-512 here)");
    free(records);
    free(offsets);
    free(types);
    unlink(source);
    unlink(reference);
}

// Slot sharing between frame objects (#177, c_check_program_lifetime.c and tier1_emit_cfg_lifetimes): each function's
// objects in blocks of their own, which --tier1 gives lifetimes and Tier 1's frame lets share slots, and the blocks that
// keep theirs: one a goto enters past its opening, a switch's own block, those around labels whose addresses are taken.
// Arrays in sibling blocks, in a loop's body left by break and continue, after it, beside the switch's cases, compound
// literals and an if's two arms; and a pointer to an array kept across a goto back inside its block, which C lets the
// next round read. Clang's build is the oracle of each build, with lifetimes and without (--tier1-no-lifetimes).
static const char check_test_tier1_lifetimes_source[] =
    "#include <stdio.h>\n"
    "#include <string.h>\n"
    "\n"
    "static void fill(int* p, int n, int seed) { for (int i = 0; i < n; i += 1) { p[i] = seed + i; } }\n"
    "static int sum(const int* p, int n) { int s = 0; for (int i = 0; i < n; i += 1) { s += p[i]; } return s; }\n"
    "\n"
    "// Two sibling blocks: their arrays can share one slot.\n"
    "int siblings(int x)\n"
    "{\n"
    "    int total = 0;\n"
    "    {\n"
    "        int a[64];\n"
    "        fill(a, 64, x);\n"
    "        total += sum(a, 64);\n"
    "    }\n"
    "    {\n"
    "        int b[64];\n"
    "        fill(b, 64, x * 2);\n"
    "        total += sum(b, 64);\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "\n"
    "// A loop body's array, live across the call, and a break and a continue that leave its block.\n"
    "int loop(int n)\n"
    "{\n"
    "    int total = 0;\n"
    "    for (int i = 0; i < n; i += 1)\n"
    "    {\n"
    "        int a[32];\n"
    "        fill(a, 32, i);\n"
    "        if (i == 3) { continue; }\n"
    "        if (i == 7) { break; }\n"
    "        {\n"
    "            int b[32];\n"
    "            fill(b, 32, sum(a, 32));\n"
    "            total += sum(b, 32);\n"
    "        }\n"
    "    }\n"
    "    {\n"
    "        int c[32];\n"
    "        fill(c, 32, total);\n"
    "        total = sum(c, 32);\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "\n"
    "// A goto that leaves a block, and one that enters another past its declaration (no lifetimes there).\n"
    "int jumps(int x)\n"
    "{\n"
    "    int total = 0;\n"
    "    {\n"
    "        int a[16];\n"
    "        fill(a, 16, x);\n"
    "        if (x > 2) { goto out; }\n"
    "        total += sum(a, 16);\n"
    "    }\n"
    "    {\n"
    "        int b[16];\n"
    "        fill(b, 16, x + 1);\n"
    "        total += sum(b, 16);\n"
    "    }\n"
    "out:\n"
    "    if (x > 5) { goto inside; }\n"
    "    {\n"
    "        int c[16];\n"
    "        fill(c, 16, x + 2);\n"
    "inside:\n"
    "        c[0] = x;\n"
    "        total += c[0];\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "\n"
    "// A switch's own block's array, and a case's block.\n"
    "int choose(int x)\n"
    "{\n"
    "    int total = 0;\n"
    "    switch (x)\n"
    "    {\n"
    "        int shared[8];\n"
    "    case 0:\n"
    "        fill(shared, 8, 1);\n"
    "        total = sum(shared, 8);\n"
    "        break;\n"
    "    case 1:\n"
    "    {\n"
    "        int mine[8];\n"
    "        fill(mine, 8, 2);\n"
    "        total = sum(mine, 8);\n"
    "        break;\n"
    "    }\n"
    "    default:\n"
    "    {\n"
    "        int other[8];\n"
    "        fill(other, 8, 3);\n"
    "        total = sum(other, 8);\n"
    "    }\n"
    "    }\n"
    "    {\n"
    "        int after[8];\n"
    "        fill(after, 8, total);\n"
    "        total += sum(after, 8);\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "\n"
    "// A label whose address is taken: its blocks have no lifetimes.\n"
    "int computed(int x)\n"
    "{\n"
    "    static void* targets[] = { &&first, &&second };\n"
    "    int total = 0;\n"
    "    {\n"
    "        int a[8];\n"
    "        fill(a, 8, x);\n"
    "        total += sum(a, 8);\n"
    "        goto *targets[x & 1];\n"
    "    }\n"
    "    {\n"
    "first:;\n"
    "        int b[8];\n"
    "        fill(b, 8, 5);\n"
    "        total += sum(b, 8);\n"
    "    }\n"
    "second:\n"
    "    {\n"
    "        int c[8];\n"
    "        fill(c, 8, 7);\n"
    "        total += sum(c, 8);\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "\n"
    "// Compound literals and structs in sibling blocks.\n"
    "struct point { int x, y, z, w; };\n"
    "static int norm(const struct point* p) { return p->x + p->y + p->z + p->w; }\n"
    "int literals(int x)\n"
    "{\n"
    "    int total = 0;\n"
    "    {\n"
    "        total += norm(&(struct point){ x, x + 1, x + 2, x + 3 });\n"
    "    }\n"
    "    {\n"
    "        struct point q = { x * 2, 1, 2, 3 };\n"
    "        total += norm(&q);\n"
    "    }\n"
    "    if (x) {\n"
    "        char text[40];\n"
    "        snprintf(text, sizeof(text), \"%d\", x);\n"
    "        total += (int)strlen(text);\n"
    "    } else {\n"
    "        char other[40];\n"
    "        memset(other, 'a', 39);\n"
    "        other[39] = 0;\n"
    "        total += (int)strlen(other);\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "\n"
    "// A pointer saved in one iteration and read after a backward goto within the same block: valid C.\n"
    "int backward(int n)\n"
    "{\n"
    "    int total = 0;\n"
    "    {\n"
    "        int* saved = 0;\n"
    "        int rounds = 0;\n"
    "again:\n"
    "        if (saved != 0) { total += *saved; }\n"
    "        int value[2] = { rounds * 3, 1 };\n"
    "        saved = value;\n"
    "        rounds += 1;\n"
    "        if (rounds < n) { goto again; }\n"
    "    }\n"
    "    {\n"
    "        int spill[2] = { 100, 200 };\n"
    "        total += spill[0] + spill[1];\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "\n"
    "int main(void)\n"
    "{\n"
    "    int total = 0;\n"
    "    for (int i = 0; i < 9; i += 1)\n"
    "    {\n"
    "        total += siblings(i) + loop(i) + jumps(i) + choose(i % 3) + computed(i) + literals(i) + backward(i);\n"
    "        printf(\"%d %d %d %d %d %d %d\\n\", siblings(i), loop(i), jumps(i), choose(i % 3), computed(i), literals(i), backward(i));\n"
    "    }\n"
    "    printf(\"%d\\n\", total);\n"
    "    return 0;\n"
    "}\n";

// The lifetime fixture compiled by --tier1 (`lifetimes` or not, by each path) into `output`, whose report's frame bytes
// go into *frame: whether it compiled with siblings, loop and literals Tier 1's.
static bool check_test_tier1_lifetime_build(const char* executable, const char* source, const char* output, bool lifetimes, bool scalar, uint64_t* frame)
{
    char* arguments[16] = { (char*)executable, "--tier1", "--tier1-report", "--tier1-refusals", "--verify" };
    uint32_t count = 5;
    if (!lifetimes)
    {
        arguments[count++] = "--tier1-no-lifetimes";
    }
    if (scalar)
    {
        arguments[count++] = "--scalar";
    }
    arguments[count++] = "-o";
    arguments[count++] = (char*)output;
    arguments[count++] = (char*)source;
    arguments[count] = NULL;
    char* text = NULL;
    const int status = check_test_driver_spawn(arguments, &text);
    *frame = 0;
    const bool reported = check_test_tier1_field(text, "frame_bytes", frame);
    const bool taken = text != NULL && strstr(text, "tier1-refused\tsiblings\t") == NULL && strstr(text, "tier1-refused\tloop\t") == NULL &&
                       strstr(text, "tier1-refused\tliterals\t") == NULL;
    if (status != 0 && text != NULL)
    {
        fprintf(stderr, "%s", text);
    }
    free(text);
    return status == 0 && reported && taken;
}

static void check_test_driver_tier1_lifetimes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char source[512];
    char reference[512];
    snprintf(source, sizeof(source), "%s/tier1-lifetimes.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-lifetimes-clang", directory);
    const bool written = parse_test_write(source, check_test_tier1_lifetimes_source, sizeof(check_test_tier1_lifetimes_source) - 1);
    char* clang_arguments[] = { (char*)clang, "-O0", "-w", "-o", reference, source, NULL };
    char* clang_text = NULL;
    const bool referenced = written && check_test_driver_spawn(clang_arguments, &clang_text) == 0;
    free(clang_text);
    char expected[4096] = { 0 };
    const bool ran = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of the lifetime fixture runs");
    uint64_t frames[2][2] = { { 0 } };
    for (uint32_t lifetimes = 0; ran && lifetimes < 2; lifetimes += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char output[512];
            snprintf(output, sizeof(output), "%s/tier1-lifetimes-%u-%u", directory, lifetimes, path);
            const bool built = check_test_tier1_lifetime_build(executable, source, output, lifetimes != 0, path != 0, &frames[lifetimes][path]);
            check_test_expect(test, built, "the lifetime fixture compiles by --tier1, its sharing functions Tier 1's");
            char actual[4096] = { 0 };
            check_test_expect(test, built && check_test_driver_capture(output, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                              "the lifetime fixture prints clang's build's output");
            unlink(output);
        }
    }
    for (uint32_t path = 0; ran && path < 2; path += 1)
    {
        check_test_expect(test, frames[1][path] != 0 && frames[1][path] < frames[0][path], "objects' lifetimes shrink Tier 1's frames");
        printf("  Tier 1's frames, %s path: %" PRIu64 " bytes with lifetimes, %" PRIu64 " without\n", path == 0 ? "vector" : "scalar", frames[1][path], frames[0][path]);
    }
    check_test_expect(test, !ran || (frames[1][0] == frames[1][1] && frames[0][0] == frames[0][1]), "both paths' frames take the same bytes");
    unlink(source);
    unlink(reference);
}
