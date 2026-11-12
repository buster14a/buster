// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// C's inline assembly (#147), as the lowering compiles it: each asm statement whose template assembles to instructions
// is a per-unit helper, a leaf function of its own records that the statement's code calls through Tier 0's ordinary
// CALL, as the atomic and floating-environment helpers are (c_check_program_helpers.c); a template of no instructions,
// the compiler barriers', is no code at all, but one with x87 operands, whose helper passes their values through the
// x87's stack as clang's code does. Tier 0 doesn't see the asm: its registers stay out of Tier 0's depths and
// homes, the call saves the values live across it, and the helper has an unwind entry of its own.
//
// check_program_asm_plan plans every statement of the selected bodies before they are lowered, beside the builtin
// helpers, in this order (CheckProgramAsm):
//   - Placement. Each operand takes its constraint's first alternative (check_asm_constraint_parse): a general register
//     of a class or a fixed one, an XMM register for a float or a double, the x87's st(0) or st(1) for a float, a
//     double or a long double (t, u: BUSTER_CHECK_ASM_X87, by LLVM's rules for an asm's stack), an MMX register (y:
//     BUSTER_CHECK_ASM_MMX), an opmask register (k, Yk: BUSTER_CHECK_ASM_MASK), A's rax or rdx:rax
//     (BUSTER_CHECK_ASM_PAIR), memory (an lvalue's:
//     clang and the checker refuse an rvalue there), an integer constant, or an output's register (a matching input).
//     An operand that names an explicit register variable takes the variable's register instead, as clang's code gives
//     it (check_asm_variable_register); rsp's, an asm body's alone, as clang's code moves it (check_asm_stack). Every operand has a register of its own but a matching pair, none a clobber,
//     caller-saved ones first (check_asm_take).
//   - Substitution (check_asm_substitute): the template with each operand as GCC and clang print it in AT&T syntax: a
//     register by the operand's size or its modifier's (b h w k q), memory as (%reg), a constant as $value, and %%, %=,
//     %{ %| %} and {att|intel}'s first alternative. A basic asm statement's template is its text, as it is.
//   - Assembly: asm_assemble_code's records of the text, by the checker's path (the assembler's vector path or its
//     scalar twin), from a match index that admits every form the encoder has (an inline template's instructions are
//     its author's to choose, as GNU as and clang's integrated assembler take any).
//   - A template that takes a directive besides the syntax switches is one source assembled whole by asm_assemble
//     (check_asm_unit_helper): the record helper's frame and operand loads around the template as encoded bytes, the
//     template, then its exits, stores and epilogue.
//     The emission publishes its atoms, symbols and fixups (check_program_emit_asm_unit). A template of alignment or
//     section directives alone has no code: its atoms and labels are published and the statement calls nothing.
//   - The helper (check_asm_helper_code): pushes of the callee-saved registers the statement touches, rsp aligned to 16
//     at the asm, the operands loaded from the operand block whose address comes in rdi (the x87's onto its stack), the
//     records, the flag outputs' conditions (setcc) and the register outputs stored through their addresses (which the
//     block holds), the x87's popped so and the x87 inputs left popped, and the epilogue: a frame unwind_tier0_frame
//     takes. An asm goto's template jumps to its labels' exits instead, each of which sets the exit's number before the
//     stores, which the helper returns: the statement's code branches to the label of that number. Its symbol follows the
//     builtin helpers', and check_program_emit publishes it as theirs.
// A naked function's statements are its whole code instead (check_program_asm_plan_naked): their templates, whose
// operands must be integer constants, assembled together, with no prologue, epilogue or trap of the compiler's, as
// clang's. Then the names (check_program_asm_names): every label a template defines and every name one refers to, once
// in the unit, which the emission makes symbols of (check_program_emit_asm_symbols): a label a local symbol at its
// record, and another name the program's symbol of that link name, or an undefined global, as GNU as and clang make
// them.
// The lowering (check_lower_asm, c_check_program_lower.c) fills the block in clang's order: each output's address (with
// a `+` operand's value, loaded through it), then each input's value or a memory input's address, then the call.
//
// This is scalar: each statement is planned once per unit, and its template is a few instructions. The statements are
// rare (main.c's units have 58), and docs/c-inline-assembly.md ("Why it is scalar") measures what one costs a compile.
//
// What the lowering refuses, by name (CheckProgramAsm.reason): an asm goto's label as anything but a jmp's or a jcc's
// target, flag outputs of a size no integer store covers or with other alternatives, f's x87 registers, the vector
// register classes and the other letters it doesn't place (p, Y but Yk, the floating constants), MMX operands with a
// modifier, opmask operands with a modifier or without their moves' features, x87 operands and clobbers off the stack's
// top, symbolic immediates
// and constants outside their
// letter's range, operands of types no general or XMM register holds by value (but a vector in a template of no
// instructions, BUSTER_CHECK_ASM_HELD), bit-fields, explicit register variables in registers it doesn't place one in
// (APX's and the other classes'), in rsp in a statement that keeps a helper, in XMM16 up without AVX-512F, and as memory
// inputs, clobbers of an operand's register, APX's
// registers, templates the assembler refuses, a template's CFI directives, a label two templates define, and a `.L`
// name no template defines; and of naked functions (CheckProgramNaked.reason) statements other than asm statements,
// operands other than integer constants, and bodies of no instructions.
// Include after c_check_program_helpers.c, assembler.c, x86_target.c and tier0.c.
#ifndef BUSTER_C_CHECK_PROGRAM_ASM_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_ASM_INCLUDED

// An operand's place (CheckAsmPlace.kind).
#define BUSTER_CHECK_ASM_GENERAL 1
#define BUSTER_CHECK_ASM_VECTOR 2
#define BUSTER_CHECK_ASM_MEMORY 3
#define BUSTER_CHECK_ASM_CONSTANT 4
// A vector in a vector register's constraint ("+v"(x) of an __m512i), which the helper doesn't load or store yet: held
// while its statement is planned, and refused where the template refers to it or has instructions. In a template of none
// (a compiler barrier's, as main.c's `__asm__("" : "+v"(x))`), whose operands the lowering evaluates alone, it is planned
// at last as an output's object (BUSTER_CHECK_ASM_MEMORY: its address, which the template leaves as it was) or as an
// input's value (BUSTER_CHECK_ASM_VECTOR: evaluated and dropped). A volatile output isn't held: clang's code loads and
// stores it.
#define BUSTER_CHECK_ASM_HELD 5
// A float, a double or a long double on the x87's stack (t's st(0), u's st(1): CheckAsmPlace.reg is the depth). The
// helper loads the inputs from the operand block, the deepest first, and stores the outputs and pops the inputs left
// after the template, as LLVM's x87 stackifier takes an asm's stack (check_program_asm_plan_one's rules).
#define BUSTER_CHECK_ASM_X87 6
// An f input's x87 register before the planning places it (CheckAsmConstraint.x87's bit, and CheckAsmPlace.reg).
#define BUSTER_CHECK_ASM_X87_ANY 0x80u
// A flag output ("=@ccz"(r), #147): its condition's value, 0 or 1, written over the whole object after the template, as
// clang's code converts setcc's byte to the output's type (CheckAsmPlace.reg is the condition's code, x86's: 4 for z).
// An object of 1, 2, 4, 8 or 16 bytes (CheckAsmPlace.size), whatever its type: clang's code stores the value's integer
// bits, a float's or a struct's too. A `+` one reads nothing: clang's code loads its value where the template can't read it.
#define BUSTER_CHECK_ASM_FLAG 7
// An MMX register's operand ("y"(x), #1846): an integer, a pointer, a float or a double of 1 to 8 bytes (an __m64's
// vector isn't one: Tier 0 has no 8-byte vector load), in mm0 to mm7 (CheckAsmPlace.reg), as clang's code places it: the helper loads the value
// zero-extended through rax (movq %rax, %mmN) and stores the register's low bytes after the template.
#define BUSTER_CHECK_ASM_MMX 8
// An opmask register's operand ("k"(m), "Yk"(m), #1846): an integer or a pointer of 1 or 2 bytes (AVX-512F's kmovw),
// or of 4 or 8 (AVX-512BW's kmovd and kmovq), in k0 to k7 (CheckAsmPlace.reg; Yk's k1 up), as clang's code places
// it: loaded zero-extended through rax (kmovw %eax, %k0), and stored after the template through a general register.
#define BUSTER_CHECK_ASM_MASK 9
// A 16-byte integer in A's pair (#1846): its low half in rax, its high half in rdx (CheckAsmPlace.reg is rax), as
// clang's code places an __int128 there; its slots take 16 bytes. A narrower A operand is a general register's, rax
// (CheckAsmPlace.wide).
#define BUSTER_CHECK_ASM_PAIR 10
// A naked function's operand that names a static object (#1845), whose name and offset (CheckAsmPlace.symbol and
// .constant) the template prints, as clang's: an "i" operand's address constant (`$g+8`, bare `g+8` with c, p or P,
// and `g+8(%rip)` with a), and an "m" operand of the object itself (`g+8(%rip)`, `g+16(%rip)` with H).
#define BUSTER_CHECK_ASM_SYMBOL 11
#define BUSTER_CHECK_ASM_SYMBOL_MEMORY 12

// check_asm_variable_register's number for an explicit register variable in rsp, esp or sp (#1846), and the first of
// ymm0 to ymm31's and of zmm0 to zmm31's.
#define BUSTER_CHECK_ASM_VARIABLE_RSP 90
#define BUSTER_CHECK_ASM_VARIABLE_YMM 96
#define BUSTER_CHECK_ASM_VARIABLE_ZMM 128

// The general registers but rsp, the ones the helper may take, and the callee-saved ones among them.
#define BUSTER_CHECK_ASM_GENERAL_ALL (0xffffu & ~(1u << X86_REGISTER_RSP))
#define BUSTER_CHECK_ASM_CALLEE_SAVED                                                                                   \
    (1u << X86_REGISTER_RBX | 1u << X86_REGISTER_RBP | 1u << X86_REGISTER_R12 | 1u << X86_REGISTER_R13 | 1u << X86_REGISTER_R14 | \
     1u << X86_REGISTER_R15)
// The operand block's slots: 8 bytes each, a value's or an address's.
#define BUSTER_CHECK_ASM_SLOT_BYTES 8
// The most operands a statement the lowering takes may have: GCC takes 30.
#define BUSTER_CHECK_ASM_OPERANDS 64
// The name an asm goto's template refers to its label by (#147): the prefix, the statement's index and the label's, as
// `.Lbuster_goto_3_1`, which no source defines.
#define BUSTER_CHECK_ASM_GOTO_PREFIX ".Lbuster_goto_"

// An asm goto's jump to one of its labels in its template (check_program_asm_plan_one): the record, the label's index.
typedef struct CheckAsmGoto CheckAsmGoto;
struct CheckAsmGoto
{
    uint32_t record;
    uint32_t label;
};

// Where the lowering places an operand. `slot` is its block slot's offset (an output's or a memory operand's address,
// an input's value; BUSTER_CHECK_NONE for a constant), `value` a `+` register operand's value slot, `tied` a matching
// input's output (BUSTER_CHECK_NONE for none), `reg` the register (GENERAL's and VECTOR's, MEMORY's address's, X87's
// depth on the stack), and `size` the operand's bytes in a register (1, 2, 4 or 8: its type's; X87's 4, 8 or 16, a
// float's, a double's or a long double's, whose slots take 16 bytes).
typedef struct CheckAsmPlace CheckAsmPlace;
struct CheckAsmPlace
{
    int64_t constant;
    TypeId type;
    uint32_t slot;
    uint32_t value;
    uint32_t tied;
    uint8_t kind;
    uint8_t reg;
    uint8_t size;
    bool written;
    bool read;
    // A flag output's _Atomic object, which its value is exchanged into (check_asm_helper_code).
    bool atomic;
    // An A operand in rax of up to 8 bytes (#1846), which clang's code widens to 64 bits: printed by its 64-bit name.
    bool wide;
    // A byte in a high byte register (ah, ch, dh or bh of CheckAsmPlace.reg's register), an explicit register variable's
    // (#1846), loaded and stored by its name and printed by it.
    bool high;
    // The general register a flag output's name prints (#1846): the one its setcc writes after the template
    // (check_asm_helper_code's stores[1]), as clang prints the register its code sets the condition into.
    uint8_t named;
    // An x87 input whose depth another input's value takes (#1846: two operands at one depth, clang's one register),
    // which the helper doesn't load.
    bool shadowed;
    // An __m64 in an MMX register (#1846, the owner's decision on #1447): its 8 bytes as an unsigned long (`type`),
    // which the lowering reads through the object's address (`object`, the operand's lvalue node past its value's
    // conversions), as Tier 0 has no 8-byte vector load, or through a copy's for an input of no object
    // (BUSTER_CHECK_NONE: `v + v`).
    bool m64;
    uint32_t object;
    // A vector of 16, 32 or 64 bytes (`size`) in an XMM, YMM or ZMM register (#1846, the owner's decision on #1447): its
    // slots hold addresses, an output's of its object and an input's (and a `+` operand's value's) of a copy the lowering
    // makes, which the asm body or the helper moves it through by movups (`vex`: VEX's vmovups, with AVX; EVEX's for a
    // ZMM register and for XMM16 up), loading each address into rax before the general registers' loads.
    bool packed;
    bool vex;
    // A global register variable's operand (#1846): its register, rsp or rbp, which the template names and nothing loads,
    // stores or gives a slot.
    bool global;
    // A symbol operand's name (BUSTER_CHECK_ASM_SYMBOL and _SYMBOL_MEMORY, #1845).
    String8 symbol;
};

// A label a template defines (AsmCodeLabel): its spelling, its record in its code, the helper's or the naked function's
// (the code's count for its end), and its name, an index in CheckProgramAsms.names (check_program_asm_names'), whose
// symbol the emission defines at the record's offset.
typedef struct CheckProgramAsmLabel CheckProgramAsmLabel;
struct CheckProgramAsmLabel
{
    String8 spelling;
    uint32_t record;
    uint32_t name;
};

// A name the unit's templates define or refer to, once (CheckProgramAsms.names): its spelling, and whether a template
// defines it, as a label. Another is the program's symbol of that link name, or else an undefined global symbol of its
// own (check_program_emit_asm_symbols).
typedef struct CheckAsmName CheckAsmName;
struct CheckAsmName
{
    String8 spelling;
    // The binding and visibility a template's directive gives the name (BUSTER_LINK_SYMBOL_LOCAL, GLOBAL or WEAK, and
    // the visibility bits), and whether a template defines it, as a label. An absolute (`.set`) name is the template's
    // alone: the emission adds no symbol for it, and another template's reference is refused by name.
    uint8_t flags;
    // The binding and size directives a template gave the name (BUSTER_ASM_DIRECTIVE_BINDING and _SIZE), and the size
    // a `.size` did: a name that maps to one of the unit's program symbols takes them, as clang's assembled module does.
    uint8_t directives;
    uint64_t size;
    bool label;
    bool absolute;
    // Whether a fixup refers to the name: an undefined global that none does is a declaration, not a reference.
    bool referenced;
    // Whether the file-scope assembly defines it (#1848): clang's assembler reports a C definition of a label's name
    // without a source location only for module assembly, which it streams before the module's definitions.
    bool file;
};

// A statement's plan: its operands' places (output_count + input_count of them), its block's bytes (0 for none), its
// helper's index in CheckProgramHelpers.items (BUSTER_CHECK_NONE for a template of no instructions without x87 operands,
// which has none),
// and for a statement the lowering refuses, why (CheckProgramLowered.reason), reported where the statement is lowered.
// A helper's references refer to names: `names` has each one's spelling, in their order, and check_program_asm_names
// makes each reference's target its index in CheckProgramAsms.names, which the emission makes its symbol's. `labels`
// are the labels its template defines, at their records in the helper.
typedef struct CheckProgramAsm CheckProgramAsm;
struct CheckProgramAsm
{
    CheckAsmPlace* places;
    const char* reason;
    String8* names;
    CheckProgramAsmLabel* labels;
    uint32_t label_count;
    uint32_t block;
    uint32_t helper;
    // The callee-saved registers the helper touches, and its frame's reservation (8 or 16): a unit's synthetic unwind
    // entry (check_program_emit_asm_unit's frame, check_asm_unit_frame).
    uint32_t saved;
    uint32_t frame;
    // A template that uses a directive besides the syntax switches: the whole helper, assembled by asm_assemble into a
    // unit of its own (atoms, symbols and fixups), which the emission publishes as a helper's code and sections.
    AsmResult unit;
    bool unit_assembled;
    bool planned;
    // A statement placed in its caller (#1059, check_asm_row): its X86 entry's row, in the output arena, which the lowering
    // gives its operands (check_lower_asm) and Tier 0 places, with no helper; 0 for a statement of a helper or of no code.
    Tier0Operation* row;
    // A statement placed in its caller as an asm body (#1839, check_asm_inline_code): its records, which run in its call
    // row's place (Tier0AsmBody), reading its operand block through BUSTER_TIER0_ASM_BLOCK, and the registers they touch;
    // with no helper. 0 records for any other statement.
    X86Instruction* inline_records;
    uint32_t inline_count;
    uint32_t inline_touched;
    // Its labels' records and names (Tier0AsmBody.label_records and label_names, label_count of them): `labels`' records
    // and, once check_program_asm_names has them, their names.
    uint32_t* inline_label_records;
    uint32_t* inline_label_names;
    // The opmask registers its template's records write (k0 to k7, a bit each, check_asm_inline_touched's), which a Tier 1
    // body's emission saves the live masks of around it as a call's (tier1_emit_cfg_asm).
    uint8_t inline_masks;
    // Its template's local branches (Tier0AsmBody.branches and targets), relative to its first record.
    uint32_t* inline_branches;
    uint32_t* inline_targets;
    uint32_t inline_branch_count;
    // Whether its template returns (Tier0AsmBody.leaves).
    bool inline_leaves;
    // Whether it moves rsp's variable's input into rsp (#1846, Tier0AsmBody.stack).
    bool inline_stack;
    // Whether its register operands are its call row's arguments, each in its register (check_asm_inline_code's
    // `passed`), rather than loads from its block.
    bool inline_passed;
    bool inlined;
};

// A naked function's code (check_program_asm_plan_naked): its asm statements' templates assembled together, the whole
// function, its references' names and its labels as a helper's plan has them; or why the lowering refuses it.
typedef struct CheckProgramNaked CheckProgramNaked;
struct CheckProgramNaked
{
    Tier0Code code;
    const char* reason;
    String8* names;
    CheckProgramAsmLabel* labels;
    uint32_t label_count;
    // Its templates use a directive besides the syntax switches: the whole function, assembled by asm_assemble.
    AsmResult unit;
    // Its templates' CFI (#1848): the unit's frame description, decoded into the rows its unwind entry takes
    // (check_asm_naked_frame_rows); 0 of them without CFI.
    EmissionFrameRow* frame_rows;
    uint32_t frame_row_count;
    // Whether its unit's frame description is replaced so: its .eh_frame is left out of the emission.
    bool framed;
    bool unit_assembled;
    bool planned;
};

typedef struct CheckProgramAsms CheckProgramAsms;
struct CheckProgramAsms
{
    // input->asm_count of them, by statement (0 for a program without asm statements); a statement of no selected body
    // isn't planned.
    CheckProgramAsm* plans;
    // By body, for a program with a naked function (0 otherwise): a naked body's code; another body isn't planned.
    CheckProgramNaked* naked;
    // The names the templates define and refer to, each once.
    CheckAsmName* names;
    uint32_t name_count;
    // The most placed statements in one selected body, each of at most one machine-operation row. A lane reuses
    // its operation table between bodies; helper and naked statements add no row to that table (#1359).
    uint32_t row_capacity;
    // The most statements in one selected body placed as asm bodies (#1839, CheckProgramAsm.inline_records), each one
    // Tier0AsmBody of its function's.
    uint32_t body_capacity;
    // The unit's file-scope assembly (#619, check_program_asm_plan_file): its statements' templates assembled together,
    // whose sections, labels and references join the templates' names and the emission's link before the functions'
    // (`file_assembled`); or why the emission refuses it, at its first statement (`file_token`).
    AsmResult file_unit;
    const char* file_reason;
    uint32_t file_token;
    bool file_assembled;
    CheckProgramStatus status;
    uint32_t token;
};

// A constraint's first alternative: the general registers it allows (a bit each), whether it allows an XMM register
// (`fixed_vector` its number plus one where an explicit register variable fixes it, 0 for any), the x87's stack
// registers it names (`x87`, a bit each: t's st(0), u's st(1)), memory or an integer constant (`ranged`: in [low,
// high], by its sign-extended value; `unsigned32`: Z's, by its zero-extended one, check_asm_constant_fits), the output
// a matching input is tied to (BUSTER_CHECK_NONE for none), and the first letter the lowering doesn't place, as the
// feature it names.
typedef struct CheckAsmConstraint CheckAsmConstraint;
struct CheckAsmConstraint
{
    int64_t low;
    int64_t high;
    const char* refused;
    uint32_t general;
    uint32_t tied;
    uint8_t fixed_vector;
    // An explicit register variable's MMX register's number plus one (#1846), 0 for any.
    uint8_t fixed_mmx;
    uint8_t x87;
    // A flag output's condition code plus one (x86's, check_asm_flag_condition), 0 for none.
    uint8_t flag;
    bool vector;
    // y's MMX registers (#1846).
    bool mmx;
    // The opmask registers k and Yk allow (#1846): k0 to k7's bits, Yk's without k0's.
    uint8_t masks;
    // A's rax and rdx pair (#1846): rax for up to 8 bytes, both for 16.
    bool pair;
    bool memory;
    bool immediate;
    bool ranged;
    bool unsigned32;
};

static const char* const check_asm_registers[4][16] = {
    { "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil", "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b" },
    { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di", "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w" },
    { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d" },
    { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" },
};

// The general registers in the order an operand takes them: the caller-saved ones first, so that a template of them
// costs its helper no saves, then the callee-saved ones, which its prologue pushes.
static const uint8_t check_asm_order[15] = {
    X86_REGISTER_RAX,
    X86_REGISTER_RCX,
    X86_REGISTER_RDX,
    X86_REGISTER_RSI,
    X86_REGISTER_RDI,
    X86_REGISTER_R8,
    X86_REGISTER_R9,
    X86_REGISTER_R10,
    X86_REGISTER_R11,
    X86_REGISTER_RBX,
    X86_REGISTER_R12,
    X86_REGISTER_R13,
    X86_REGISTER_R14,
    X86_REGISTER_R15,
    X86_REGISTER_RBP,
};

// A clobber's register name as clang's target normalizes it (#2262, getNormalizedGCCRegisterName): past one '%' or '#',
// and an index into its GCCRegNames (181 names, in getAsInteger's radix: 0x, 0b and 0 prefixes) replaced by that name,
// written in `buffer`. The checker took only names clang knows (check_input_register_name_valid).
static CheckAsmText check_asm_clobber_name(CheckAsmText text, char* buffer)
{
    if (text.length != 0 && (text.bytes[0] == '%' || text.bytes[0] == '#'))
    {
        text.bytes += 1;
        text.length -= 1;
    }
    const bool numeric = text.length != 0 && text.bytes[0] >= '0' && text.bytes[0] <= '9';
    const bool prefixed = numeric && text.length > 2 && text.bytes[0] == '0' && (text.bytes[1] == 'x' || text.bytes[1] == 'X' || text.bytes[1] == 'b' || text.bytes[1] == 'B');
    const uint32_t radix = prefixed ? (text.bytes[1] == 'x' || text.bytes[1] == 'X' ? 16 : 2) : numeric && text.bytes[0] == '0' && text.length > 1 ? 8 : 10;
    uint32_t index = 0;
    for (uint32_t at = prefixed ? 2 : 0; numeric && at < text.length && index <= 180; at += 1)
    {
        const uint8_t c = text.bytes[at];
        const uint32_t digit = c >= '0' && c <= '9' ? (uint32_t)(c - '0') : c >= 'a' && c <= 'f' ? (uint32_t)(c - 'a' + 10) : (uint32_t)(c - 'A' + 10);
        index = index * radix + digit;
    }
    // GCCRegNames' runs, in order: each a prefix and its first number (or a name of its own), and its length.
    static const struct
    {
        const char* name;
        int32_t first;
        uint32_t count;
    } runs[] = {
        { "ax", -1, 1 }, { "dx", -1, 1 }, { "cx", -1, 1 }, { "bx", -1, 1 }, { "si", -1, 1 }, { "di", -1, 1 }, { "bp", -1, 1 }, { "sp", -1, 1 },
        { "st", -1, 1 }, { "st(", 1, 7 }, { "argp", -1, 1 }, { "flags", -1, 1 }, { "fpcr", -1, 1 }, { "fpsr", -1, 1 }, { "dirflag", -1, 1 },
        { "frame", -1, 1 }, { "xmm", 0, 8 }, { "mm", 0, 8 }, { "r", 8, 8 }, { "xmm", 8, 8 }, { "ymm", 0, 16 }, { "xmm", 16, 16 }, { "ymm", 16, 16 },
        { "zmm", 0, 32 }, { "k", 0, 8 }, { "cr0", -1, 1 }, { "cr", 2, 3 }, { "cr8", -1, 1 }, { "dr", 0, 4 }, { "dr", 6, 2 }, { "bnd", 0, 4 },
        { "tmm", 0, 8 }, { "r", 16, 16 },
    };
    uint32_t start = 0;
    for (uint32_t r = 0; numeric && r < BUSTER_ARRAY_LENGTH(runs); r += 1)
    {
        if (index >= start && index < start + runs[r].count)
        {
            const int32_t number = runs[r].first < 0 ? -1 : runs[r].first + (int32_t)(index - start);
            const bool stack = strcmp(runs[r].name, "st(") == 0;
            const int written = number < 0 ? snprintf(buffer, 16, "%s", runs[r].name) : snprintf(buffer, 16, stack ? "%s%d)" : "%s%d", runs[r].name, number);
            text.bytes = (const uint8_t*)buffer;
            text.length = written > 0 ? (uint32_t)written : 0;
        }
        start += runs[r].count;
    }
    return text;
}

// The register a clobber names: a general one's number (0 to 15), 16 plus an XMM, YMM or ZMM register's number, -1
// for one that needs nothing of the helper (rsp, the flags, memory, the x87's, MMX's and the opmasks, which its call
// clobbers anyway), or -2 for APX's general registers r16 to r31, which it doesn't take.
static int32_t check_asm_clobber(CheckAsmText text)
{
    if (text.length != 0 && (text.bytes[0] == '%' || text.bytes[0] == '#'))
    {
        text.bytes += 1;
        text.length -= 1;
    }
    int32_t result = -1;
    for (uint32_t size = 0; result == -1 && size < 4; size += 1)
    {
        for (uint32_t reg = 0; result == -1 && reg < 16; reg += 1)
        {
            const char* name = check_asm_registers[size][reg];
            if (text.length == strlen(name) && memcmp(text.bytes, name, text.length) == 0)
            {
                result = (int32_t)reg;
            }
        }
    }
    for (uint32_t high = 0; result == -1 && high < 4; high += 1)
    {
        static const char* const names[4] = { "ah", "ch", "dh", "bh" };
        if (text.length == 2 && memcmp(text.bytes, names[high], 2) == 0)
        {
            result = (int32_t)high;
        }
    }
    const bool vector = text.length >= 4 && (text.bytes[0] == 'x' || text.bytes[0] == 'y' || text.bytes[0] == 'z') && memcmp(text.bytes + 1, "mm", 2) == 0;
    const bool apx = result == -1 && text.length >= 3 && text.bytes[0] == 'r' && text.bytes[1] >= '1' && text.bytes[1] <= '3' && text.bytes[2] >= '0' && text.bytes[2] <= '9' &&
                     !(text.bytes[1] == '1' && text.bytes[2] <= '5');
    if (vector)
    {
        uint32_t number = 0;
        for (uint32_t at = 3; at < text.length && number < 64; at += 1)
        {
            number = number * 10 + (uint32_t)(text.bytes[at] - '0');
        }
        result = 16 + (int32_t)number;
    }
    else if (apx)
    {
        result = -2;
    }
    return result;
}

// The x87 register a clobber names, as a bit of the stack's depths: "st" st(0)'s, "st(1)" to "st(7)" theirs; 0 for
// another clobber.
static uint32_t check_asm_x87_clobber(CheckAsmText text)
{
    if (text.length != 0 && (text.bytes[0] == '%' || text.bytes[0] == '#'))
    {
        text.bytes += 1;
        text.length -= 1;
    }
    const bool top = text.length == 2 && memcmp(text.bytes, "st", 2) == 0;
    const bool deeper = text.length == 5 && memcmp(text.bytes, "st(", 3) == 0 && text.bytes[3] >= '0' && text.bytes[3] <= '7' && text.bytes[4] == ')';
    return top ? 1u : deeper ? 1u << (text.bytes[3] - '0') : 0;
}

// The bytes the x87 loads and stores of a value of type `type`: a float's 4, a double's 8 and a long double's 16 (its
// slot's, of which fldt and fstpt take 10); 0 for a type no x87 register holds.
static uint32_t check_asm_x87_bytes(const TypeTable* types, TypeId type)
{
    const TypeId base = type_conversion_operand(types, type);
    return base == BUSTER_TYPE_FLOAT ? 4 : base == BUSTER_TYPE_DOUBLE ? 8 : base == BUSTER_TYPE_LONG_DOUBLE ? 16 : 0;
}

// A flag output's condition (the text after "@cc", to the constraint's end, as clang 23's X86 target matches it): its x86
// condition code (o 0, no 1, b 2, ... g 15), or -1 for a name clang doesn't take (pe and po among them).
static int32_t check_asm_flag_condition(const uint8_t* bytes, uint32_t length)
{
    static const struct
    {
        const char* name;
        uint8_t code;
    } conditions[] = {
        { "o", 0 },  { "no", 1 },  { "b", 2 },   { "c", 2 },   { "nae", 2 }, { "ae", 3 }, { "nb", 3 },  { "nc", 3 },  { "e", 4 },   { "z", 4 },
        { "ne", 5 }, { "nz", 5 },  { "be", 6 },  { "na", 6 },  { "a", 7 },   { "nbe", 7 }, { "s", 8 },  { "ns", 9 },  { "p", 10 },  { "np", 11 },
        { "l", 12 }, { "nge", 12 }, { "ge", 13 }, { "nl", 13 }, { "le", 14 }, { "ng", 14 }, { "g", 15 }, { "nle", 15 },
    };
    int32_t result = -1;
    for (uint32_t i = 0; result < 0 && i < BUSTER_ARRAY_LENGTH(conditions); i += 1)
    {
        result = length == strlen(conditions[i].name) && memcmp(bytes, conditions[i].name, length) == 0 ? conditions[i].code : -1;
    }
    return result;
}

// Constraint `text`'s first alternative, for an output (after its = or +) or an input, of statement `statement`.
static CheckAsmConstraint check_asm_constraint_parse(const CheckInputAsm* statement, const InternTable* names, CheckAsmText text, bool output)
{
    CheckAsmConstraint result = { .tied = BUSTER_CHECK_NONE };
    for (uint32_t at = output ? 1 : 0; at < text.length && text.bytes[at] != ','; at += 1)
    {
        const uint8_t byte = text.bytes[at];
        int64_t low = 0;
        int64_t high = -1;
        if (byte >= '0' && byte <= '9')
        {
            uint32_t tied = 0;
            while (at < text.length && text.bytes[at] >= '0' && text.bytes[at] <= '9' && tied <= statement->output_count)
            {
                tied = tied * 10 + (uint32_t)(text.bytes[at++] - '0');
            }
            at -= 1;
            result.tied = tied;
        }
        else if (byte == '[')
        {
            const uint32_t begin = at + 1;
            while (at < text.length && text.bytes[at] != ']')
            {
                at += 1;
            }
            result.tied = check_asm_named(statement, names, text.bytes + begin, at - begin, false);
        }
        else if (byte == 'r' || byte == 'q' || byte == 'l' || byte == 'g' || byte == 'X')
        {
            result.general |= BUSTER_CHECK_ASM_GENERAL_ALL;
            result.memory |= byte == 'g' || byte == 'X';
            result.immediate |= (byte == 'g' || byte == 'X') && !output;
            if ((byte == 'g' || byte == 'X') && !output)
            {
                low = INT64_MIN;
                high = INT64_MAX;
            }
        }
        else if (byte == 'Q')
        {
            result.general |= 1u << X86_REGISTER_RAX | 1u << X86_REGISTER_RBX | 1u << X86_REGISTER_RCX | 1u << X86_REGISTER_RDX;
        }
        else if (byte == 'R')
        {
            result.general |= 0xffu & ~(1u << X86_REGISTER_RSP);
        }
        else if (strchr("abcdSD", byte) != NULL)
        {
            static const char letters[] = "acdbSD";
            static const uint8_t registers[] = { X86_REGISTER_RAX, X86_REGISTER_RCX, X86_REGISTER_RDX, X86_REGISTER_RBX, X86_REGISTER_RSI, X86_REGISTER_RDI };
            result.general |= 1u << registers[strchr(letters, byte) - letters];
        }
        else if (byte == 'x' || byte == 'v')
        {
            result.vector = true;
        }
        else if (byte == 'y')
        {
            result.mmx = true;
        }
        else if (byte == 'A')
        {
            // rax, or rax and rdx for 16 bytes, as clang 23 places them on x86-64.
            result.general |= 1u << X86_REGISTER_RAX;
            result.pair = true;
        }
        else if (byte == 'k' || (byte == 'Y' && at + 1 < text.length && text.bytes[at + 1] == 'k'))
        {
            // k0 to k7, or Yk's k1 to k7, the registers a write mask takes.
            result.masks |= byte == 'k' ? 0xffu : 0xfeu;
            at += byte == 'Y';
        }
        else if (byte == 't' || byte == 'u')
        {
            // The x87 stack's top and the register under it, st(0) and st(1) (clang's {st} and {st(1)}).
            result.x87 |= byte == 't' ? 1 : 2;
        }
        else if (byte == 'f' && !output)
        {
            // An input in an x87 register of the compiler's choice (clang refuses f for an output): a depth below the
            // fixed ones (check_program_asm_plan_one), which the template doesn't pop.
            result.x87 |= BUSTER_CHECK_ASM_X87_ANY;
        }
        else if (byte == 'm' || byte == 'o' || byte == 'V' || byte == '<' || byte == '>')
        {
            result.memory = true;
        }
        else if (byte == 'i' || byte == 'n')
        {
            low = INT64_MIN;
            high = INT64_MAX;
        }
        else if (byte == 'e')
        {
            low = INT32_MIN;
            high = INT32_MAX;
        }
        else if (byte == 'Z')
        {
            result.unsigned32 = true;
            result.immediate = true;
        }
        else if (strchr("IJKMNO", byte) != NULL)
        {
            static const char letters[] = "IJKMNO";
            static const int64_t lows[] = { 0, 0, -128, 0, 0, 0 };
            static const int64_t highs[] = { 31, 63, 127, 3, 255, 127 };
            low = lows[strchr(letters, byte) - letters];
            high = highs[strchr(letters, byte) - letters];
        }
        else if (byte == '&' || byte == '%' || byte == '?' || byte == '!' || byte == '*' || byte == '#' || byte == ' ' || byte == '\t')
        {
            // An early clobber is kept apart from the inputs anyway: every operand has a register of its own.
        }
        else if (byte == '@' && output && at + 2 < text.length && text.bytes[at + 1] == 'c' && text.bytes[at + 2] == 'c' &&
                 check_asm_flag_condition(text.bytes + at + 3, text.length - (at + 3)) >= 0)
        {
            // A flag output, its condition to the constraint's end (the checker took it as clang does).
            result.flag = (uint8_t)(check_asm_flag_condition(text.bytes + at + 3, text.length - (at + 3)) + 1);
            at = text.length;
        }
        else if (result.refused == NULL)
        {
            result.refused = byte == '@'                                 ? "inline assembly flag outputs" :
                                                                           "this inline assembly constraint";
        }
        if (high >= low)
        {
            result.low = result.ranged && result.low < low ? result.low : low;
            result.high = result.ranged && result.high > high ? result.high : high;
            result.immediate = true;
            result.ranged = true;
        }
    }
    return result;
}

// Whether `constraint` takes the integer constant `extended`, its type's `bits` sign-extended (as clang prints it), as
// clang checks it: a letter's range by that value (so "N"((unsigned char)255) is refused, as clang refuses it), and Z's
// by the zero-extended bits, as clang's backend checks Z (so "Z"(0xffffffffu) is taken, and printed $-1).
static bool check_asm_constant_fits(const CheckAsmConstraint* constraint, int64_t extended, uint32_t bits)
{
    const uint64_t zero_extended = bits >= 64 ? (uint64_t)extended : (uint64_t)extended & ((UINT64_C(1) << bits) - 1);
    return (constraint->ranged && extended >= constraint->low && extended <= constraint->high) || (constraint->unsigned32 && zero_extended <= UINT32_MAX);
}

// The type a matching input of `input`'s type takes its output's register in, for an output of `output`'s type, as GCC
// and clang place it: an integer or a pointer narrower than the output zero-extended from its own width (clang's zext,
// which the helper's narrower loads make too), so its unsigned type, whose value the output's conversion then widens;
// otherwise the output's own, a floating input converted as clang converts it.
static TypeId check_asm_tied_type(const TypeTable* types, TypeId input, TypeId output)
{
    uint32_t width = 0;
    bool is_signed = false;
    const bool integer = check_action_integer_type(types, input, &width, &is_signed) || check_action_kind(types, input) == BUSTER_TYPE_KIND_POINTER;
    const uint64_t size = type_is_complete(types, input) ? type_size(types, input) : 0;
    const uint64_t output_size = type_is_complete(types, output) ? type_size(types, output) : 0;
    const TypeId widened = size == 1 ? BUSTER_TYPE_UCHAR : size == 2 ? BUSTER_TYPE_USHORT : size == 4 ? BUSTER_TYPE_UINT : output;
    return integer && size < output_size ? widened : output;
}

// A general register of `allowed` that no other operand and no clobber has (`taken`), in check_asm_order's order: its
// number, or -1 for none.
static int32_t check_asm_take(uint32_t allowed, uint32_t* taken)
{
    int32_t result = -1;
    for (uint32_t i = 0; result < 0 && i < BUSTER_ARRAY_LENGTH(check_asm_order); i += 1)
    {
        const uint32_t bit = 1u << check_asm_order[i];
        if ((allowed & bit) != 0 && (*taken & bit) == 0)
        {
            *taken |= bit;
            result = check_asm_order[i];
        }
    }
    return result;
}

// Appends `count` bytes to the text being written at the top of `scratch` (check_asm_substitute's).
static void check_asm_emit_bytes(Arena* scratch, const void* bytes, uint64_t count, bool* valid)
{
    uint8_t* at = *valid && count != 0 ? arena_push(scratch, count, 1) : 0;
    *valid &= count == 0 || at != 0;
    if (at != 0)
    {
        memcpy(at, bytes, count);
    }
}

static void check_asm_emit_text(Arena* scratch, const char* text, bool* valid)
{
    check_asm_emit_bytes(scratch, text, strlen(text), valid);
}

static void check_asm_emit_number(Arena* scratch, int64_t value, bool* valid)
{
    char digits[24];
    const int length = snprintf(digits, sizeof(digits), "%lld", (long long)value);
    check_asm_emit_bytes(scratch, digits, (uint64_t)length, valid);
}

// A symbol operand's name and offset (#1845): `g`, `g+8` or `g-8`, as clang prints them.
static void check_asm_emit_symbol(Arena* scratch, const CheckAsmPlace* place, int64_t extra, bool* valid)
{
    const int64_t offset = place->constant + extra;
    check_asm_emit_bytes(scratch, place->symbol.pointer, place->symbol.length, valid);
    check_asm_emit_text(scratch, offset > 0 ? "+" : "", valid);
    if (offset != 0)
    {
        check_asm_emit_number(scratch, offset, valid);
    }
}

// Operand `place`, as GCC and clang print it in AT&T syntax with `modifier` (0 for none): NULL, or the feature a
// modifier the lowering doesn't print names.
static const char* check_asm_print(Arena* scratch, const CheckAsmPlace* place, uint8_t modifier, bool* valid)
{
    const char* refused = NULL;
    // A flag output prints as the register its setcc writes after the template, 32 bits wide as clang's (an i32 vreg's)
    // whatever its object (#1846): `%0` is `%ecx`, `%b0` `%cl`. Its value there is the template's to ignore, as in clang's.
    const CheckAsmPlace named = { .kind = BUSTER_CHECK_ASM_GENERAL, .reg = place->named, .size = 4 };
    place = place->kind == BUSTER_CHECK_ASM_FLAG ? &named : place;
    if (place->kind == BUSTER_CHECK_ASM_GENERAL || place->kind == BUSTER_CHECK_ASM_PAIR)
    {
        // An A operand prints rax's 64-bit name, an __int128's low half's too, as clang's does.
        const uint32_t printed = place->wide || place->kind == BUSTER_CHECK_ASM_PAIR ? 8 : place->size;
        const uint32_t size = modifier == 'b' ? 1 : modifier == 'w' ? 2 : modifier == 'k' ? 4 : modifier == 'q' || modifier == 'V' || modifier == 'a' || modifier == 'A' ? 8 : printed;
        const uint32_t row = size == 1 ? 0 : size == 2 ? 1 : size == 4 ? 2 : 3;
        static const char* const high[4] = { "%ah", "%ch", "%dh", "%bh" };
        if ((modifier == 'h' || (modifier == 0 && place->high)) && place->reg < 4)
        {
            check_asm_emit_text(scratch, high[place->reg], valid);
        }
        else if (modifier == 0 || modifier == 'b' || modifier == 'w' || modifier == 'k' || modifier == 'q' || modifier == 'V' || modifier == 'a' || modifier == 'A')
        {
            check_asm_emit_text(scratch, modifier == 'a' ? "(%" : modifier == 'A' ? "*%" : modifier == 'V' ? "" : "%", valid);
            check_asm_emit_text(scratch, check_asm_registers[row][place->reg], valid);
            check_asm_emit_text(scratch, modifier == 'a' ? ")" : "", valid);
        }
        else if (modifier == 'z')
        {
            check_asm_emit_text(scratch, size == 1 ? "b" : size == 2 ? "w" : size == 4 ? "l" : "q", valid);
        }
        else
        {
            refused = "this inline assembly operand modifier";
        }
    }
    else if (place->kind == BUSTER_CHECK_ASM_VECTOR)
    {
        if (modifier == 0 || modifier == 'x' || modifier == 't' || modifier == 'g')
        {
            // Its own size's name without a modifier (#1846: a packed vector's ymm or zmm), as clang prints it.
            const uint8_t named = modifier != 0 ? modifier : place->size == 64 ? 'g' : place->size == 32 ? 't' : 'x';
            check_asm_emit_text(scratch, named == 't' ? "%ymm" : named == 'g' ? "%zmm" : "%xmm", valid);
            check_asm_emit_number(scratch, place->reg, valid);
        }
        else
        {
            refused = "this inline assembly operand modifier";
        }
    }
    else if (place->kind == BUSTER_CHECK_ASM_MMX || place->kind == BUSTER_CHECK_ASM_MASK)
    {
        // Its register, as clang prints it; clang refuses every modifier of one.
        if (modifier == 0)
        {
            check_asm_emit_text(scratch, place->kind == BUSTER_CHECK_ASM_MMX ? "%mm" : "%k", valid);
            check_asm_emit_number(scratch, place->reg, valid);
        }
        else
        {
            refused = "this inline assembly operand modifier";
        }
    }
    else if (place->kind == BUSTER_CHECK_ASM_HELD)
    {
        // Its register, which it hasn't.
        refused = "inline assembly operands of this type in their constraint's registers";
    }
    else if (place->kind == BUSTER_CHECK_ASM_X87)
    {
        // Its depth's register, as GCC and clang print it: %st and %st(1), which the assembler reads (#147).
        if (modifier == 0)
        {
            check_asm_emit_text(scratch, place->reg == 0 ? "%st" : "%st(", valid);
            if (place->reg != 0)
            {
                check_asm_emit_number(scratch, place->reg, valid);
                check_asm_emit_text(scratch, ")", valid);
            }
        }
        else
        {
            refused = "this inline assembly operand modifier";
        }
    }
    else if (place->kind == BUSTER_CHECK_ASM_SYMBOL || place->kind == BUSTER_CHECK_ASM_SYMBOL_MEMORY)
    {
        const bool memory = place->kind == BUSTER_CHECK_ASM_SYMBOL_MEMORY;
        const bool printed = memory ? modifier == 0 || modifier == 'H' : modifier == 0 || modifier == 'c' || modifier == 'p' || modifier == 'P' || modifier == 'a';
        if (printed)
        {
            check_asm_emit_text(scratch, !memory && modifier == 0 ? "$" : "", valid);
            check_asm_emit_symbol(scratch, place, modifier == 'H' ? 8 : 0, valid);
            check_asm_emit_text(scratch, memory || modifier == 'a' ? "(%rip)" : "", valid);
        }
        else
        {
            refused = "this inline assembly operand modifier";
        }
    }
    else if (place->kind == BUSTER_CHECK_ASM_MEMORY)
    {
        if (modifier == 0 || modifier == 'H' || modifier == 'A')
        {
            check_asm_emit_text(scratch, modifier == 'H' ? "8(%" : modifier == 'A' ? "*(%" : "(%", valid);
            check_asm_emit_text(scratch, check_asm_registers[3][place->reg], valid);
            check_asm_emit_text(scratch, ")", valid);
        }
        else if (modifier == 'z' && (place->size == 1 || place->size == 2 || place->size == 4 || place->size == 8))
        {
            check_asm_emit_text(scratch, place->size == 1 ? "b" : place->size == 2 ? "w" : place->size == 4 ? "l" : "q", valid);
        }
        else
        {
            refused = "this inline assembly operand modifier";
        }
    }
    else if (modifier == 0 || modifier == 'b' || modifier == 'h' || modifier == 'w' || modifier == 'k' || modifier == 'q' || modifier == 'V' || modifier == 'c' ||
             modifier == 'P' || modifier == 'p' || modifier == 'a' || modifier == 'n')
    {
        // The register modifiers print a constant as it is, as clang's do ("shll %b1" of "cI"(3) is shll $3).
        const bool bare = modifier == 'c' || modifier == 'P' || modifier == 'p' || modifier == 'a' || modifier == 'n';
        check_asm_emit_text(scratch, bare ? "" : "$", valid);
        check_asm_emit_number(scratch, modifier == 'n' ? (int64_t)(0 - (uint64_t)place->constant) : place->constant, valid);
    }
    else
    {
        refused = "this inline assembly operand modifier";
    }
    return refused;
}

// Statement `statement`'s template with its operands substituted (as `places` has them), as AT&T syntax prints them,
// written at the top of `scratch` into `*text`, its basic template as it is: NULL, or the feature it refuses. `unique`
// is %='s number, the statement's own in the unit.
static const char* check_asm_substitute(const CheckInputAsm* statement, const CheckAsmPlace* places, const InternTable* names, uint32_t unique,
                                        Arena* scratch, String8* text, bool* valid)
{
    const CheckAsmText source = statement->template_text;
    const uint64_t begin = scratch->length;
    const uint32_t count = statement->output_count + statement->input_count;
    const char* refused = NULL;
    // 0 outside braces, 1 in the AT&T alternative, 2 in a later one, which isn't printed.
    uint32_t dialect = 0;
    for (uint32_t at = 0; *valid && refused == NULL && at < source.length; at += 1)
    {
        const uint8_t byte = source.bytes[at];
        if (statement->basic)
        {
            check_asm_emit_bytes(scratch, &byte, 1, valid);
        }
        else if (byte == '%' && at + 1 < source.length)
        {
            at += 1;
            const uint8_t next = source.bytes[at];
            if (next == '%' || next == '{' || next == '|' || next == '}')
            {
                if (dialect != 2)
                {
                    check_asm_emit_bytes(scratch, &next, 1, valid);
                }
            }
            else if (next == '=')
            {
                if (dialect != 2)
                {
                    check_asm_emit_number(scratch, unique, valid);
                }
            }
            else
            {
                uint8_t modifier = 0;
                if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z'))
                {
                    modifier = next;
                    at += 1;
                }
                uint32_t operand = BUSTER_CHECK_NONE;
                if (at < source.length && source.bytes[at] == '[')
                {
                    const uint32_t name = at + 1;
                    while (at < source.length && source.bytes[at] != ']')
                    {
                        at += 1;
                    }
                    operand = check_asm_named(statement, names, source.bytes + name, at - name, modifier == 'l');
                }
                else
                {
                    operand = 0;
                    const uint32_t digits = at;
                    while (at < source.length && source.bytes[at] >= '0' && source.bytes[at] <= '9' && operand <= count)
                    {
                        operand = operand * 10 + (uint32_t)(source.bytes[at++] - '0');
                    }
                    operand = at > digits ? operand : BUSTER_CHECK_NONE;
                    at -= 1;
                }
                if (modifier == 'l' || operand >= count)
                {
                    // The checker took the template (check_asm_template), so this is an asm goto's label: by its name's
                    // index among the labels, or by its number after the operands and the `+` ones' inputs. It is printed
                    // as a name of the statement's own (check_asm_goto_label), which check_program_asm_plan_one turns into
                    // a branch to the helper's exit for the label.
                    uint32_t read_write = 0;
                    for (uint32_t k = 0; k < statement->output_count; k += 1)
                    {
                        read_write += (statement->operands[k].flags & CHECK_ASM_READ) != 0;
                    }
                    const bool named = at < source.length && source.bytes[at] == ']';
                    const uint32_t label = named ? operand : operand >= count + read_write ? operand - count - read_write : BUSTER_CHECK_NONE;
                    if (label >= statement->label_count)
                    {
                        refused = "asm goto";
                    }
                    else if (dialect != 2)
                    {
                        check_asm_emit_text(scratch, BUSTER_CHECK_ASM_GOTO_PREFIX, valid);
                        check_asm_emit_number(scratch, unique, valid);
                        check_asm_emit_text(scratch, "_", valid);
                        check_asm_emit_number(scratch, label, valid);
                    }
                }
                else if (dialect != 2)
                {
                    refused = check_asm_print(scratch, places + operand, modifier, valid);
                }
            }
        }
        else if (byte == '{' && dialect == 0)
        {
            dialect = 1;
        }
        else if (byte == '|' && dialect != 0)
        {
            dialect = 2;
        }
        else if (byte == '}' && dialect != 0)
        {
            dialect = 0;
        }
        else if (dialect != 2)
        {
            check_asm_emit_bytes(scratch, &byte, 1, valid);
        }
    }
    *text = (String8){ .pointer = (char*)scratch->base + begin, .length = scratch->length - begin };
    return refused;
}

// Whether the helper loads operand `place` before the template: a register operand the template reads (an input's
// value, a matching input's into its output's register, a `+` operand's) and a memory operand's address.
static bool check_asm_loads(const CheckAsmPlace* place)
{
    return place->kind == BUSTER_CHECK_ASM_MEMORY || ((place->kind == BUSTER_CHECK_ASM_GENERAL || (place->kind == BUSTER_CHECK_ASM_VECTOR && !place->packed)) && place->read);
}

// Whether the helper stores operand `place` after the template: a register output, through the address its slot holds.
static bool check_asm_stores(const CheckAsmPlace* place)
{
    return place->written && (place->kind == BUSTER_CHECK_ASM_GENERAL || place->kind == BUSTER_CHECK_ASM_VECTOR);
}

// Whether the helper loads x87 operand `place` onto the stack before the template (an input's value, a matching input's,
// a `+` operand's), and whether it stores it after the template (an output, popped through the address its slot holds).
static bool check_asm_x87_loads(const CheckAsmPlace* place)
{
    return place->kind == BUSTER_CHECK_ASM_X87 && (!place->written || place->read) && !place->shadowed;
}

static bool check_asm_x87_stores(const CheckAsmPlace* place)
{
    return place->kind == BUSTER_CHECK_ASM_X87 && place->written;
}

// Whether the helper loads MMX or opmask operand `place` before the template, through rax (an input's value, a matching
// input's, a `+` operand's), and whether it stores it after the template (an output, through the address its slot
// holds).
static bool check_asm_mmx_loads(const CheckAsmPlace* place)
{
    return (place->kind == BUSTER_CHECK_ASM_MMX || place->kind == BUSTER_CHECK_ASM_MASK) && place->read;
}

static bool check_asm_mmx_stores(const CheckAsmPlace* place)
{
    return (place->kind == BUSTER_CHECK_ASM_MMX || place->kind == BUSTER_CHECK_ASM_MASK) && place->written;
}

// Whether the helper loads A's pair `place` before the template (rax and rdx, from its 16-byte slot), and whether it
// stores it after the template.
static bool check_asm_pair_loads(const CheckAsmPlace* place)
{
    return place->kind == BUSTER_CHECK_ASM_PAIR && place->read;
}

static bool check_asm_pair_stores(const CheckAsmPlace* place)
{
    return place->kind == BUSTER_CHECK_ASM_PAIR && place->written;
}

// Whether `type` is a vector of `size` bytes, a packed operand's (CheckAsmPlace.packed): a matching input of one's.
static bool check_asm_packed_type(const TypeTable* types, TypeId type, uint32_t size)
{
    const uint8_t kind = check_action_kind(types, type);
    return (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR) && type_is_complete(types, type) && type_size(types, type) == size;
}

// Why a vector of `size` bytes in XMM, YMM or ZMM register `reg` can't be placed under `target` (#1846), or NULL: clang
// moves 32 bytes with AVX, 64 with AVX-512F, and XMM16 up's 16 or 32 with AVX-512VL beside it, and can't place one
// without ("could not allocate", or Sema's "invalid output size").
static const char* check_asm_packed_refused(const X86Target* target, uint32_t size, uint32_t reg)
{
    const uint64_t features = target->features;
    const bool avx512 = (features & BUSTER_X86_FEATURE(AVX512F)) != 0;
    const bool vl = avx512 && (features & BUSTER_X86_FEATURE(AVX512VL)) != 0;
    return size == 32 && (features & BUSTER_X86_FEATURE(AVX)) == 0 ? "inline assembly vector operands of 32 bytes without AVX" :
           size == 64 && !avx512                                     ? "inline assembly vector operands of 64 bytes without AVX-512F" :
           reg >= 16 && size != 64 && !vl                            ? "inline assembly vector operands in XMM16 to XMM31 or YMM16 to YMM31 without AVX-512VL" :
                                                                       NULL;
}

// The form that moves packed operand `place` (CheckAsmPlace.packed) from memory into its register, or with `store`, from
// its register into memory: movups's, VEX's with AVX, EVEX's for ZMM and for XMM16 up.
static uint32_t check_asm_packed_form(const CheckAsmPlace* place, bool store)
{
    const bool evex = place->reg >= 16;
    return place->size == 64 ? (store ? X86_FORM_VMOVUPS_ZM512_Z : X86_FORM_VMOVUPS_Z_ZM512) :
           place->size == 32 ? (evex ? (store ? X86_FORM_VMOVUPS_YM256_Y_EVEX : X86_FORM_VMOVUPS_Y_YM256_EVEX) : (store ? X86_FORM_VMOVUPS_YM256_Y : X86_FORM_VMOVUPS_Y_YM256)) :
           evex ? (store ? X86_FORM_VMOVUPS_XM128_X_EVEX : X86_FORM_VMOVUPS_X_XM128_EVEX) :
           place->vex ? (store ? X86_FORM_VMOVUPS_XM128_X : X86_FORM_VMOVUPS_X_XM128) : (store ? X86_FORM_MOVUPS_XM_X : X86_FORM_MOVUPS_X_XM);
}

// Whether the helper or the asm body loads packed operand `place` before the template (an input's, a matching input's, a
// `+` operand's): its address into rax, then the vector through it (check_asm_packed_form).
static bool check_asm_packed_loads(const CheckAsmPlace* place)
{
    return place->kind == BUSTER_CHECK_ASM_VECTOR && place->packed && place->read;
}

// Whether `place` is rsp's explicit register variable's (#1846): an asm body's alone, whose input is moved into rsp after
// every other load, at its width (a narrower one keeps rsp's other bits, as clang's movw and movb do), and whose output
// is stored from rsp after the template.
static bool check_asm_stack(const CheckAsmPlace* place)
{
    return place->kind == BUSTER_CHECK_ASM_GENERAL && place->reg == X86_REGISTER_RSP && !place->global;
}

// Whether the helper stores flag output `place` after the template: its condition's value over its object.
static bool check_asm_flag_stores(const CheckAsmPlace* place)
{
    return place->kind == BUSTER_CHECK_ASM_FLAG;
}

// The helper's records around the template's (`code`): the pushes of `saved`'s callee-saved registers, the reservation
// that aligns rsp to 16 and keeps the block's address (`block`: whether the helper has an operand block, in rdi), the
// loads (the x87 operands' first, the deepest first), the template's records, the stores of the register outputs
// (through `stores`' two registers) and of the x87 ones (from st(0) down), the pops of the x87 inputs the template left
// below them (`pops`), and the epilogue, with the template's branches and references in the helper's indices (its
// first record's, `*first`): the frame unwind_tier0_frame takes (Tier0Code.frame_saved and frame_size). Each
// reference's target is 0, for check_program_asm_names. A failure is TIER0_ERROR_MEMORY.
// The label of statement `statement`'s a template's reference `name` is (check_asm_substitute's name for it), or
// BUSTER_CHECK_NONE for another name.
static uint32_t check_asm_goto_label(String8 name, uint32_t statement, uint32_t labels)
{
    char expected[64];
    const int length = snprintf(expected, sizeof(expected), "%s%u_", BUSTER_CHECK_ASM_GOTO_PREFIX, statement);
    uint32_t label = BUSTER_CHECK_NONE;
    if (length > 0 && name.length > (uint64_t)length && name.length - (uint64_t)length <= 10 && memcmp(name.pointer, expected, (uint64_t)length) == 0)
    {
        uint64_t value = 0;
        bool digits = true;
        for (uint64_t at = (uint64_t)length; at < name.length; at += 1)
        {
            digits &= name.pointer[at] >= '0' && name.pointer[at] <= '9';
            value = value * 10 + (uint64_t)(name.pointer[at] - '0');
        }
        label = digits && value < labels ? (uint32_t)value : BUSTER_CHECK_NONE;
    }
    return label;
}

// An asm goto's (`labels` of them, its template's jumps to them `gotos`, in record order: check_program_asm_plan_one's)
// leaves the template by its fall-through or one of its labels: each way sets the exit register (`exit`) to its number, 0
// for the fall-through and the label's index plus one, and goes to the common tail of stores, whose end returns it in
// eax. The template's jumps to a label are branches to its exit's record, rel8 forms that branch sizing sizes, and no
// reference of theirs is kept.
static Tier0Code check_asm_helper_code(const CheckAsmPlace* places, uint32_t count, const AsmCode* code, uint32_t saved, const uint8_t stores[2],
                                       bool block, uint32_t pops, const CheckAsmGoto* gotos, uint32_t goto_count, uint32_t labels, uint8_t exit,
                                       uint32_t* first, Arena* output)
{
    Tier0Code result = { .error = TIER0_ERROR_MEMORY };
    uint32_t pushes = 0;
    uint32_t loads = 0;
    uint32_t stored = 0;
    uint32_t flags = 0;
    uint32_t mmx_loads = 0;
    uint32_t mmx_stores = 0;
    for (uint32_t r = 0; r < BUSTER_TIER0_CALLEE_SAVED; r += 1)
    {
        pushes += (saved >> tier0_callee_saved[r] & 1) != 0;
    }
    for (uint32_t k = 0; k < count; k += 1)
    {
        loads += check_asm_loads(places + k) || check_asm_x87_loads(places + k);
        stored += check_asm_stores(places + k) || check_asm_x87_stores(places + k);
        flags += check_asm_flag_stores(places + k);
        mmx_loads += check_asm_mmx_loads(places + k) || check_asm_pair_loads(places + k) || check_asm_packed_loads(places + k);
        mmx_stores += check_asm_mmx_stores(places + k) || check_asm_pair_stores(places + k);
    }
    // At the call, rsp is 8 past a multiple of 16; the pushes and the reservation (the block's address's slot) leave it
    // at one at the template, as clang -O0's functions have it in their bodies.
    const uint32_t frame = pushes % 2 == 0 ? 8 : 16;
    // A flag output's store takes 6 records at most: the setcc, its zero extension, the object's address, its store (and
    // the high half's zero for 16 bytes) and the block's address again. An MMX operand's load takes 2 (into rax, then the
    // register), and its store 4 at most (the address, the register's low bytes into a general register, their store
    // and the block's address again).
    // An asm goto's exits: a move and a jump each, the last one's falling into the tail, and the tail's move into eax.
    const uint64_t exits = labels != 0 ? 2 * ((uint64_t)labels + 1) - 1 + 1 : 0;
    // An x87 output that shares its depth with a later one is stored from a copy, one record more (fld %st(0)).
    const uint64_t records = 2 * (uint64_t)pushes + (block ? 1 : 0) + loads + 2 * (uint64_t)mmx_loads + code->count + exits +
                             (stored + flags + mmx_stores != 0 ? 1 + 3 * (uint64_t)stored + 6 * (uint64_t)flags + 4 * (uint64_t)mmx_stores : 0) + pops + 3;
    const uint32_t branch_count = code->branch_count + goto_count + labels;
    const uint32_t reference_count = code->reference_count - goto_count;
    X86Instruction* written = records <= UINT32_MAX ? BUSTER_ARENA_PUSH_ARRAY(output, X86Instruction, records) : 0;
    uint32_t* branches = branch_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, branch_count) : 0;
    uint32_t* targets = branch_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, branch_count) : 0;
    LinkReference* references = reference_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, LinkReference, reference_count) : 0;
    if (written != 0 && (branch_count == 0 || (branches != 0 && targets != 0)) && (reference_count == 0 || references != 0))
    {
        uint32_t at = 0;
        uint32_t frame_saved = 0;
        for (uint32_t r = 0; r < BUSTER_TIER0_CALLEE_SAVED; r += 1)
        {
            if ((saved >> tier0_callee_saved[r] & 1) != 0)
            {
                written[at++] = (X86Instruction){ .form = X86_FORM_PUSH_R64, .rm = tier0_callee_saved[r] };
                frame_saved |= 1u << r;
            }
        }
        written[at++] = (X86Instruction){ .form = X86_FORM_SUB_RM64_IMM8, .rm = X86_REGISTER_RSP, .immediate = (int32_t)frame };
        if (block)
        {
            written[at++] = (X86Instruction){ .form = X86_FORM_MOV_RM64_R64, .reg = X86_REGISTER_RDI, .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE };
        }
        // The x87 inputs onto the stack, from the block rdi holds: the deepest first, so that each is at its depth when
        // the template starts (a `+` operand's value from its value slot, another input's from its slot), each by its
        // type's width.
        for (uint32_t depth = 8; depth-- > 0;)
        {
            for (uint32_t k = 0; k < count; k += 1)
            {
                const CheckAsmPlace* place = places + k;
                if (check_asm_x87_loads(place) && place->reg == depth)
                {
                    const uint32_t form = place->size == 4 ? X86_FORM_FLD_M32 : place->size == 8 ? X86_FORM_FLD_M64 : X86_FORM_FLD_M80;
                    written[at++] = (X86Instruction){ .form = (uint16_t)form, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE,
                                                      .displacement = (int32_t)(place->written ? place->value : place->slot) };
                }
            }
        }
        // The MMX and opmask operands, from the block rdi holds, before the general registers' loads: each value
        // zero-extended into rax, then moved into its register (movq %rax, %mmN; kmovw %eax, %kN, kmovd or kmovq by its
        // width), as clang's code loads it.
        for (uint32_t k = 0; k < count; k += 1)
        {
            const CheckAsmPlace* place = places + k;
            if (check_asm_mmx_loads(place))
            {
                const uint32_t form = place->size == 1 ? X86_FORM_MOVZX_R32_RM8 : place->size == 2 ? X86_FORM_MOVZX_R32_RM16 :
                                      place->size == 4 ? X86_FORM_MOV_R32_RM32 : X86_FORM_MOV_R64_RM64;
                written[at++] = (X86Instruction){ .form = (uint16_t)form, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = (int32_t)(place->written ? place->value : place->slot) };
                const uint32_t move = place->kind == BUSTER_CHECK_ASM_MMX ? X86_FORM_MOVQ_MM_RM64 : place->size <= 2 ? X86_FORM_KMOVW_K_R32 :
                                      place->size == 4 ? X86_FORM_KMOVD_K_R32 : X86_FORM_KMOVQ_K_R64;
                written[at++] = (X86Instruction){ .form = (uint16_t)move, .reg = place->reg, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_REGISTER_DIRECT };
            }
        }
        // A's pair, after those, whose loads use rax: its low half into rax and its high half into rdx.
        for (uint32_t k = 0; k < count; k += 1)
        {
            const CheckAsmPlace* place = places + k;
            if (check_asm_pair_loads(place))
            {
                const uint32_t slot = place->written ? place->value : place->slot;
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = (int32_t)slot };
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_RDX, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = (int32_t)slot + 8 };
            }
        }
        // The vectors of 16 bytes or more (#1846), each through its address, which rax takes before the general loads.
        for (uint32_t k = 0; k < count; k += 1)
        {
            const CheckAsmPlace* place = places + k;
            if (check_asm_packed_loads(place))
            {
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_RAX, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = (int32_t)(place->written ? place->value : place->slot) };
                written[at++] = (X86Instruction){ .form = (uint16_t)check_asm_packed_form(place, false), .reg = place->reg, .rm = X86_REGISTER_RAX,
                                                  .mode = BUSTER_X86_MEMORY_BASE };
            }
        }
        // The loads, from the block rdi holds: the one into rdi last. Only a matching pair shares a register, and of it only
        // the input loads, so one load at most goes into rdi.
        for (uint32_t pass = 0; pass < 2; pass += 1)
        {
            for (uint32_t k = 0; k < count; k += 1)
            {
                const CheckAsmPlace* place = places + k;
                const bool into_rdi = place->kind != BUSTER_CHECK_ASM_VECTOR && place->reg == X86_REGISTER_RDI;
                if (check_asm_loads(place) && into_rdi == (pass == 1))
                {
                    // A `+` register operand's value is in its value slot; an input's value and a memory operand's address in
                    // its slot.
                    const uint32_t slot = place->written && place->kind != BUSTER_CHECK_ASM_MEMORY ? place->value : place->slot;
                    uint32_t form = 0;
                    uint8_t reg = place->reg;
                    if (place->kind == BUSTER_CHECK_ASM_VECTOR)
                    {
                        // XMM16 up by EVEX's forms (#1846).
                        form = place->size == 4 ? (place->reg >= 16 ? X86_FORM_VMOVD_X_RM32_EVEX : X86_FORM_MOVD_X_RM32) :
                                                  (place->reg >= 16 ? X86_FORM_VMOVQ_X_XM64_EVEX : X86_FORM_MOVQ_X_XM64);
                    }
                    else if (place->high)
                    {
                        // A high byte, by its name (#1846).
                        form = X86_FORM_MOV_R8_RM8;
                        reg = (uint8_t)((4 + place->reg) | BUSTER_X86_REGISTER_HIGH_BYTE);
                    }
                    else if (place->kind == BUSTER_CHECK_ASM_MEMORY)
                    {
                        form = X86_FORM_MOV_R64_RM64;
                    }
                    else
                    {
                        // A narrower value zero-extended: the register's other bits are the template's to ignore, as GCC's
                        // and clang's leave them unspecified.
                        form = place->size == 1 ? X86_FORM_MOVZX_R32_RM8 : place->size == 2 ? X86_FORM_MOVZX_R32_RM16 :
                               place->size == 4 ? X86_FORM_MOV_R32_RM32 : X86_FORM_MOV_R64_RM64;
                    }
                    written[at++] = (X86Instruction){ .form = (uint16_t)form, .reg = reg, .rm = X86_REGISTER_RDI, .mode = BUSTER_X86_MEMORY_BASE,
                                                      .displacement = (int32_t)slot };
                }
            }
        }
        const uint32_t base = at;
        if (code->count != 0)
        {
            memcpy(written + at, code->records, (uint64_t)code->count * sizeof(X86Instruction));
        }
        at += code->count;
        // The exits: the fall-through's after the template, then each label's, at `exit_records[l + 1]`.
        const uint32_t tail = at + (uint32_t)exits - (labels != 0 ? 1 : 0);
        uint32_t branch = 0;
        uint32_t template_branch = 0;
        uint32_t jump = 0;
        // The template's own branches and its jumps to the labels, merged in record order, as branch sizing takes them.
        while (template_branch < code->branch_count || jump < goto_count)
        {
            const bool own = jump >= goto_count || (template_branch < code->branch_count && code->branches[template_branch] < gotos[jump].record);
            if (own)
            {
                branches[branch] = base + code->branches[template_branch];
                targets[branch++] = base + code->targets[template_branch++];
            }
            else
            {
                // The label's exit: after the fall-through's move and jump, two records a label before it.
                X86Instruction* record = written + base + gotos[jump].record;
                record->form = (uint16_t)((record->form & ~BUSTER_X86_FORM_FIXUP) - 1);
                record->displacement = 0;
                branches[branch] = base + gotos[jump].record;
                targets[branch++] = base + code->count + 2 + 2 * gotos[jump].label;
                jump += 1;
            }
        }
        for (uint32_t e = 0; e <= labels && labels != 0; e += 1)
        {
            written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R32_IMM32, .rm = exit, .immediate = (int32_t)e };
            if (e != labels)
            {
                branches[branch] = at;
                targets[branch++] = tail;
                written[at++] = (X86Instruction){ .form = X86_FORM_JMP_REL8 };
            }
        }
        for (uint32_t r = 0, kept = 0; r < code->reference_count; r += 1)
        {
            const AsmCodeReference* reference = code->references + r;
            bool jumping = false;
            for (uint32_t j = 0; j < goto_count; j += 1)
            {
                jumping |= gotos[j].record == reference->record;
            }
            if (!jumping)
            {
                references[kept++] = (LinkReference){ .instruction = base + reference->record, .field = reference->field, .kind = reference->kind,
                                                      .addend = reference->addend };
            }
        }
        *first = base;
        if (stored + flags + mmx_stores != 0)
        {
            written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = stores[0], .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE };
        }
        // The flag outputs first, while the flags are the template's (no record before them sets any, and none of these
        // does): each condition's setcc into a register, zero-extended, then stored once over its whole object at its
        // width, as clang's code writes the value 0 or 1 (#1126's review: a volatile object takes one store, and an
        // _Atomic one an exchange, clang's sequentially consistent store). The object's address takes the block's
        // register, whose address the reservation keeps, and which is loaded again after each.
        for (uint32_t k = 0; k < count; k += 1)
        {
            const CheckAsmPlace* place = places + k;
            if (check_asm_flag_stores(place))
            {
                written[at++] = (X86Instruction){ .form = tier0_setcc[place->reg & 15], .rm = stores[1], .mode = BUSTER_X86_REGISTER_DIRECT };
                written[at++] = (X86Instruction){ .form = X86_FORM_MOVZX_R32_RM8, .reg = stores[1], .rm = stores[1], .mode = BUSTER_X86_REGISTER_DIRECT };
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = stores[0], .rm = stores[0], .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = (int32_t)place->slot };
                const uint32_t form = place->size == 1 ? (place->atomic ? X86_FORM_XCHG_RM8_R8 : X86_FORM_MOV_RM8_R8) :
                                      place->size == 2 ? (place->atomic ? X86_FORM_XCHG_RM16_R16 : X86_FORM_MOV_RM16_R16) :
                                      place->size == 4 ? (place->atomic ? X86_FORM_XCHG_RM32_R32 : X86_FORM_MOV_RM32_R32) :
                                                         (place->atomic ? X86_FORM_XCHG_RM64_R64 : X86_FORM_MOV_RM64_R64);
                written[at++] = (X86Instruction){ .form = (uint16_t)form, .reg = stores[1], .rm = stores[0], .mode = BUSTER_X86_MEMORY_BASE };
                if (place->size == 16)
                {
                    written[at++] = (X86Instruction){ .form = X86_FORM_MOV_RM64_IMM32, .rm = stores[0], .mode = BUSTER_X86_MEMORY_BASE, .displacement = 8 };
                }
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = stores[0], .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE };
            }
        }
        for (uint32_t k = 0; k < count; k += 1)
        {
            const CheckAsmPlace* place = places + k;
            if (check_asm_stores(place))
            {
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = stores[1], .rm = stores[0], .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = (int32_t)place->slot };
                // XMM16 up by EVEX's forms, and a high byte by its name, through a register of the first eight (#1846).
                const bool evex = place->kind == BUSTER_CHECK_ASM_VECTOR && place->reg >= 16;
                const uint32_t form = place->packed ? check_asm_packed_form(place, true) :
                                      place->kind == BUSTER_CHECK_ASM_VECTOR ? (place->size == 4 ? (evex ? X86_FORM_VMOVD_RM32_X_EVEX : X86_FORM_MOVD_RM32_X) :
                                                                                                  (evex ? X86_FORM_VMOVQ_XM64_X_EVEX : X86_FORM_MOVQ_XM64_X)) :
                                      place->size == 1 ? X86_FORM_MOV_RM8_R8 : place->size == 2 ? X86_FORM_MOV_RM16_R16 :
                                      place->size == 4 ? X86_FORM_MOV_RM32_R32 : X86_FORM_MOV_RM64_R64;
                const uint8_t reg = place->high ? (uint8_t)((4 + place->reg) | BUSTER_X86_REGISTER_HIGH_BYTE) : place->reg;
                written[at++] = (X86Instruction){ .form = (uint16_t)form, .reg = reg, .rm = stores[1], .mode = BUSTER_X86_MEMORY_BASE };
            }
        }
        // A's pairs, rax and rdx through their addresses.
        for (uint32_t k = 0; k < count; k += 1)
        {
            const CheckAsmPlace* place = places + k;
            if (check_asm_pair_stores(place))
            {
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = stores[1], .rm = stores[0], .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = (int32_t)place->slot };
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_RM64_R64, .reg = X86_REGISTER_RAX, .rm = stores[1], .mode = BUSTER_X86_MEMORY_BASE };
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_RM64_R64, .reg = X86_REGISTER_RDX, .rm = stores[1], .mode = BUSTER_X86_MEMORY_BASE, .displacement = 8 };
            }
        }
        // The MMX outputs, through their addresses: movq's 8 bytes or movd's 4, and a narrower value's bytes through the
        // block's register, whose address the reservation keeps and which is loaded again after it; and so the opmask
        // outputs, kmovw's, kmovd's or kmovq's bits by their width.
        for (uint32_t k = 0; k < count; k += 1)
        {
            const CheckAsmPlace* place = places + k;
            if (check_asm_mmx_stores(place))
            {
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = stores[1], .rm = stores[0], .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = (int32_t)place->slot };
                if (place->kind == BUSTER_CHECK_ASM_MASK)
                {
                    const uint32_t move = place->size <= 2 ? X86_FORM_KMOVW_R32_K : place->size == 4 ? X86_FORM_KMOVD_R32_K : X86_FORM_KMOVQ_R64_K;
                    const uint32_t store = place->size == 1 ? X86_FORM_MOV_RM8_R8 : place->size == 2 ? X86_FORM_MOV_RM16_R16 :
                                           place->size == 4 ? X86_FORM_MOV_RM32_R32 : X86_FORM_MOV_RM64_R64;
                    written[at++] = (X86Instruction){ .form = (uint16_t)move, .reg = stores[0], .rm = place->reg, .mode = BUSTER_X86_REGISTER_DIRECT };
                    written[at++] = (X86Instruction){ .form = (uint16_t)store, .reg = stores[0], .rm = stores[1], .mode = BUSTER_X86_MEMORY_BASE };
                    written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = stores[0], .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE };
                }
                else if (place->size == 8 || place->size == 4)
                {
                    written[at++] = (X86Instruction){ .form = place->size == 8 ? X86_FORM_MOVQ_M64_MM : X86_FORM_MOVD_RM32_MM, .reg = place->reg, .rm = stores[1],
                                                      .mode = BUSTER_X86_MEMORY_BASE };
                }
                else
                {
                    written[at++] = (X86Instruction){ .form = X86_FORM_MOVD_RM32_MM, .reg = place->reg, .rm = stores[0], .mode = BUSTER_X86_REGISTER_DIRECT };
                    written[at++] = (X86Instruction){ .form = place->size == 1 ? X86_FORM_MOV_RM8_R8 : X86_FORM_MOV_RM16_R16, .reg = stores[0], .rm = stores[1],
                                                      .mode = BUSTER_X86_MEMORY_BASE };
                    written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = stores[0], .rm = X86_REGISTER_RSP, .mode = BUSTER_X86_MEMORY_BASE };
                }
            }
        }
        // The x87 outputs, from st(0) down, each popped through its address by its type's width; then the inputs the
        // template left below them, popped: the stack is empty again, as the convention has it at the return.
        for (uint32_t depth = 0; depth < 8; depth += 1)
        {
            for (uint32_t k = 0; k < count; k += 1)
            {
                const CheckAsmPlace* place = places + k;
                bool later = false;
                for (uint32_t o = k + 1; o < count; o += 1)
                {
                    later |= check_asm_x87_stores(places + o) && places[o].reg == depth;
                }
                if (check_asm_x87_stores(place) && place->reg == depth)
                {
                    // Another output at this depth after it takes the same value (#1846): stored from a copy.
                    if (later)
                    {
                        written[at++] = (X86Instruction){ .form = X86_FORM_FLD_ST, .rm = 0, .mode = BUSTER_X86_REGISTER_DIRECT };
                    }
                    written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = stores[1], .rm = stores[0], .mode = BUSTER_X86_MEMORY_BASE,
                                                      .displacement = (int32_t)place->slot };
                    const uint32_t form = place->size == 4 ? X86_FORM_FSTP_M32 : place->size == 8 ? X86_FORM_FSTP_M64 : X86_FORM_FSTP_M80;
                    written[at++] = (X86Instruction){ .form = (uint16_t)form, .rm = stores[1], .mode = BUSTER_X86_MEMORY_BASE };
                }
            }
        }
        for (uint32_t p = 0; p < pops; p += 1)
        {
            written[at++] = (X86Instruction){ .form = X86_FORM_FSTP_ST, .rm = 0, .mode = BUSTER_X86_REGISTER_DIRECT };
        }
        if (labels != 0)
        {
            written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R32_RM32, .reg = X86_REGISTER_RAX, .rm = exit, .mode = BUSTER_X86_REGISTER_DIRECT };
        }
        written[at++] = (X86Instruction){ .form = X86_FORM_ADD_RM64_IMM8, .rm = X86_REGISTER_RSP, .immediate = (int32_t)frame };
        for (uint32_t r = BUSTER_TIER0_CALLEE_SAVED; r-- > 0;)
        {
            if ((saved >> tier0_callee_saved[r] & 1) != 0)
            {
                written[at++] = (X86Instruction){ .form = X86_FORM_POP_R64, .rm = tier0_callee_saved[r] };
            }
        }
        written[at++] = (X86Instruction){ .form = X86_FORM_RET };
        result = (Tier0Code){
            .records = written,
            .count = at,
            .branches = branches,
            .targets = targets,
            .branch_count = branch_count,
            .references = references,
            .reference_count = reference_count,
            .frame_saved = frame_saved,
            .frame_size = frame,
            .error = TIER0_OK,
        };
    }
    return result;
}

// The records of a statement placed in its caller as an asm body (#1839, Tier0AsmBody): the helper's
// (check_asm_helper_code) without its frame, its call and its return. The operands are loaded from the operand block,
// a frame object of the caller's, through BUSTER_TIER0_ASM_BLOCK (the x87's first, the deepest first), in any order, as
// none of them is the block's register; then the template's records; the flag outputs' stores while the flags are the
// template's, each condition's setcc into `stores[1]`, zero-extended, and stored over its object through its address in
// `stores[0]`; the register outputs' stores, each through its address in `stores[1]`; the x87 outputs', from st(0) down;
// and the pops of the x87 inputs the template left. No output is in a store's register. With `passed`, the operands a
// register holds when the template starts (check_asm_loads') are the call row's arguments instead, which Tier 0 moves into
// their registers (CheckProgramAsm.inline_passed): the body loads only the x87's. Returns the count, and the template's
// first record in `*first` (its branches' and labels' records are past it), with 0 records and `*records` 0 without room
// in `output`. A statement of no records but a label's (#1839) has none, and `*records` is then 0 too: `*room` says which.
// An asm goto's (`labels` of them, #1839's item 2) has the helper's exits after the template (check_asm_helper_code's):
// the fall-through's move of 0 into `exit` and its jump to the stores, then each label's move of its index plus one and
// jump (the last one falling into them), and after the stores the exit's move into eax, which its call row returns; the
// caller makes the jumps branches.
static uint32_t check_asm_inline_code(const CheckAsmPlace* places, uint32_t count, const AsmCode* code, const uint8_t stores[2], uint32_t pops,
                                      bool passed, uint32_t labels, uint8_t exit, X86Instruction** records, uint32_t* first, bool* room, Arena* output)
{
    uint32_t loads = 0;
    uint32_t stored = 0;
    uint32_t flags = 0;
    uint32_t mmx_loads = 0;
    uint32_t mmx_stores = 0;
    for (uint32_t k = 0; k < count; k += 1)
    {
        loads += check_asm_loads(places + k) || check_asm_x87_loads(places + k);
        stored += check_asm_stores(places + k) || check_asm_x87_stores(places + k);
        flags += check_asm_flag_stores(places + k);
        mmx_loads += check_asm_mmx_loads(places + k) || check_asm_pair_loads(places + k) || check_asm_packed_loads(places + k);
        mmx_stores += check_asm_mmx_stores(places + k) || check_asm_pair_stores(places + k);
    }
    // An x87 output that shares its depth with a later one is stored from a copy, one record more (fld %st(0)). An MMX or
    // opmask operand's load takes 2 records (into rax, then its register) and A's pair's 2; their stores 3 at most (the
    // address, then the register's bytes through a general register, or A's two halves). An asm goto's exits: a move and a
    // jump each but the last one's, and the exit's move into eax after the stores.
    const uint64_t exits = labels != 0 ? 2 * (uint64_t)labels + 1 : 0;
    const uint64_t capacity = (uint64_t)loads + code->count + exits + 3 * (uint64_t)stored + 5 * (uint64_t)flags + pops + 2 * (uint64_t)mmx_loads + 3 * (uint64_t)mmx_stores +
                              (labels != 0 ? 1 : 0);
    X86Instruction* written = capacity != 0 && capacity <= UINT32_MAX ? BUSTER_ARENA_PUSH_ARRAY(output, X86Instruction, capacity) : 0;
    *room = capacity == 0 || written != 0;
    uint32_t at = 0;
    if (written != 0)
    {
        for (uint32_t depth = 8; depth-- > 0;)
        {
            for (uint32_t k = 0; k < count; k += 1)
            {
                const CheckAsmPlace* place = places + k;
                if (check_asm_x87_loads(place) && place->reg == depth)
                {
                    const uint32_t form = place->size == 4 ? X86_FORM_FLD_M32 : place->size == 8 ? X86_FORM_FLD_M64 : X86_FORM_FLD_M80;
                    written[at++] = (X86Instruction){ .form = (uint16_t)form, .rm = BUSTER_TIER0_ASM_BLOCK, .mode = BUSTER_X86_MEMORY_BASE,
                                                      .displacement = (int32_t)(place->written ? place->value : place->slot) };
                }
            }
        }
        // The MMX and opmask operands (#1846), each zero-extended into rax and moved into its register, then A's pairs into
        // rax and rdx, as the helper loads them: before the general registers' loads, which a body of them always has
        // (check_asm_inline_passes), so that rax is free.
        for (uint32_t k = 0; k < count; k += 1)
        {
            const CheckAsmPlace* place = places + k;
            const uint32_t slot = place->written ? place->value : place->slot;
            if (check_asm_mmx_loads(place))
            {
                const uint32_t form = place->size == 1 ? X86_FORM_MOVZX_R32_RM8 : place->size == 2 ? X86_FORM_MOVZX_R32_RM16 :
                                      place->size == 4 ? X86_FORM_MOV_R32_RM32 : X86_FORM_MOV_R64_RM64;
                written[at++] = (X86Instruction){ .form = (uint16_t)form, .reg = X86_REGISTER_RAX, .rm = BUSTER_TIER0_ASM_BLOCK, .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = (int32_t)slot };
                const uint32_t move = place->kind == BUSTER_CHECK_ASM_MMX ? X86_FORM_MOVQ_MM_RM64 : place->size <= 2 ? X86_FORM_KMOVW_K_R32 :
                                      place->size == 4 ? X86_FORM_KMOVD_K_R32 : X86_FORM_KMOVQ_K_R64;
                written[at++] = (X86Instruction){ .form = (uint16_t)move, .reg = place->reg, .rm = X86_REGISTER_RAX, .mode = BUSTER_X86_REGISTER_DIRECT };
            }
        }
        for (uint32_t k = 0; k < count; k += 1)
        {
            const CheckAsmPlace* place = places + k;
            if (check_asm_pair_loads(place))
            {
                const uint32_t slot = place->written ? place->value : place->slot;
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_RAX, .rm = BUSTER_TIER0_ASM_BLOCK, .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = (int32_t)slot };
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_RDX, .rm = BUSTER_TIER0_ASM_BLOCK, .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = (int32_t)slot + 8 };
            }
        }
        // The vectors of 16 bytes or more (#1846), each through its address, which rax takes, as the helper loads them.
        for (uint32_t k = 0; k < count; k += 1)
        {
            const CheckAsmPlace* place = places + k;
            if (check_asm_packed_loads(place))
            {
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = X86_REGISTER_RAX, .rm = BUSTER_TIER0_ASM_BLOCK, .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = (int32_t)(place->written ? place->value : place->slot) };
                written[at++] = (X86Instruction){ .form = (uint16_t)check_asm_packed_form(place, false), .reg = place->reg, .rm = X86_REGISTER_RAX,
                                                  .mode = BUSTER_X86_MEMORY_BASE };
            }
        }
        for (uint32_t i = 0; i < 2 * count; i += 1)
        {
            // rsp's variable last (#1846), at its width, as the block's other loads may be rsp-relative.
            const CheckAsmPlace* place = places + i % count;
            const bool stack = check_asm_stack(place);
            if (check_asm_loads(place) && !passed && stack == (i >= count))
            {
                const uint32_t slot = place->written && place->kind != BUSTER_CHECK_ASM_MEMORY ? place->value : place->slot;
                // XMM16 up by EVEX's forms, and a high byte by its name (#1846), as the helper loads them.
                const bool evex = place->kind == BUSTER_CHECK_ASM_VECTOR && place->reg >= 16;
                const uint32_t form = place->kind == BUSTER_CHECK_ASM_VECTOR ? (place->size == 4 ? (evex ? X86_FORM_VMOVD_X_RM32_EVEX : X86_FORM_MOVD_X_RM32) :
                                                                                                  (evex ? X86_FORM_VMOVQ_X_XM64_EVEX : X86_FORM_MOVQ_X_XM64)) :
                                      place->high || (stack && place->size == 1) ? X86_FORM_MOV_R8_RM8 :
                                      stack && place->size == 2              ? X86_FORM_MOV_R16_RM16 :
                                      place->kind == BUSTER_CHECK_ASM_MEMORY ? X86_FORM_MOV_R64_RM64 :
                                      place->size == 1 ? X86_FORM_MOVZX_R32_RM8 : place->size == 2 ? X86_FORM_MOVZX_R32_RM16 :
                                      place->size == 4 ? X86_FORM_MOV_R32_RM32 : X86_FORM_MOV_R64_RM64;
                const uint8_t reg = place->high ? (uint8_t)((4 + place->reg) | BUSTER_X86_REGISTER_HIGH_BYTE) : place->reg;
                written[at++] = (X86Instruction){ .form = (uint16_t)form, .reg = reg, .rm = BUSTER_TIER0_ASM_BLOCK, .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = (int32_t)slot };
            }
        }
        *first = at;
        if (code->count != 0)
        {
            memcpy(written + at, code->records, (uint64_t)code->count * sizeof(X86Instruction));
        }
        at += code->count;
        for (uint32_t e = 0; e <= labels && labels != 0; e += 1)
        {
            written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R32_IMM32, .rm = exit, .immediate = (int32_t)e };
            if (e != labels)
            {
                written[at++] = (X86Instruction){ .form = X86_FORM_JMP_REL8 };
            }
        }
        for (uint32_t k = 0; k < count; k += 1)
        {
            const CheckAsmPlace* place = places + k;
            if (check_asm_flag_stores(place))
            {
                written[at++] = (X86Instruction){ .form = tier0_setcc[place->reg & 15], .rm = stores[1], .mode = BUSTER_X86_REGISTER_DIRECT };
                written[at++] = (X86Instruction){ .form = X86_FORM_MOVZX_R32_RM8, .reg = stores[1], .rm = stores[1], .mode = BUSTER_X86_REGISTER_DIRECT };
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = stores[0], .rm = BUSTER_TIER0_ASM_BLOCK, .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = (int32_t)place->slot };
                const uint32_t form = place->size == 1 ? (place->atomic ? X86_FORM_XCHG_RM8_R8 : X86_FORM_MOV_RM8_R8) :
                                      place->size == 2 ? (place->atomic ? X86_FORM_XCHG_RM16_R16 : X86_FORM_MOV_RM16_R16) :
                                      place->size == 4 ? (place->atomic ? X86_FORM_XCHG_RM32_R32 : X86_FORM_MOV_RM32_R32) :
                                                         (place->atomic ? X86_FORM_XCHG_RM64_R64 : X86_FORM_MOV_RM64_R64);
                written[at++] = (X86Instruction){ .form = (uint16_t)form, .reg = stores[1], .rm = stores[0], .mode = BUSTER_X86_MEMORY_BASE };
                if (place->size == 16)
                {
                    written[at++] = (X86Instruction){ .form = X86_FORM_MOV_RM64_IMM32, .rm = stores[0], .mode = BUSTER_X86_MEMORY_BASE, .displacement = 8 };
                }
            }
        }
        for (uint32_t k = 0; k < count; k += 1)
        {
            const CheckAsmPlace* place = places + k;
            if (check_asm_stores(place))
            {
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = stores[1], .rm = BUSTER_TIER0_ASM_BLOCK, .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = (int32_t)place->slot };
                // XMM16 up by EVEX's forms, and a high byte by its name through a store register of the first eight (#1846).
                const bool evex = place->kind == BUSTER_CHECK_ASM_VECTOR && place->reg >= 16;
                const uint32_t form = place->packed ? check_asm_packed_form(place, true) :
                                      place->kind == BUSTER_CHECK_ASM_VECTOR ? (place->size == 4 ? (evex ? X86_FORM_VMOVD_RM32_X_EVEX : X86_FORM_MOVD_RM32_X) :
                                                                                                  (evex ? X86_FORM_VMOVQ_XM64_X_EVEX : X86_FORM_MOVQ_XM64_X)) :
                                      place->size == 1 ? X86_FORM_MOV_RM8_R8 : place->size == 2 ? X86_FORM_MOV_RM16_R16 :
                                      place->size == 4 ? X86_FORM_MOV_RM32_R32 : X86_FORM_MOV_RM64_R64;
                const uint8_t reg = place->high ? (uint8_t)((4 + place->reg) | BUSTER_X86_REGISTER_HIGH_BYTE) : place->reg;
                written[at++] = (X86Instruction){ .form = (uint16_t)form, .reg = reg, .rm = stores[1], .mode = BUSTER_X86_MEMORY_BASE };
            }
        }
        // A's pairs, rax and rdx through their addresses; then the MMX and opmask outputs (#1846), as the helper stores
        // them, a narrow one's bytes and an opmask's bits through stores[0], which the block's frame place leaves free.
        for (uint32_t k = 0; k < count; k += 1)
        {
            const CheckAsmPlace* place = places + k;
            if (check_asm_pair_stores(place))
            {
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = stores[1], .rm = BUSTER_TIER0_ASM_BLOCK, .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = (int32_t)place->slot };
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_RM64_R64, .reg = X86_REGISTER_RAX, .rm = stores[1], .mode = BUSTER_X86_MEMORY_BASE };
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_RM64_R64, .reg = X86_REGISTER_RDX, .rm = stores[1], .mode = BUSTER_X86_MEMORY_BASE, .displacement = 8 };
            }
        }
        for (uint32_t k = 0; k < count; k += 1)
        {
            const CheckAsmPlace* place = places + k;
            if (check_asm_mmx_stores(place))
            {
                written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = stores[1], .rm = BUSTER_TIER0_ASM_BLOCK, .mode = BUSTER_X86_MEMORY_BASE,
                                                  .displacement = (int32_t)place->slot };
                const uint32_t store = place->size == 1 ? X86_FORM_MOV_RM8_R8 : place->size == 2 ? X86_FORM_MOV_RM16_R16 :
                                       place->size == 4 ? X86_FORM_MOV_RM32_R32 : X86_FORM_MOV_RM64_R64;
                if (place->kind == BUSTER_CHECK_ASM_MASK)
                {
                    const uint32_t move = place->size <= 2 ? X86_FORM_KMOVW_R32_K : place->size == 4 ? X86_FORM_KMOVD_R32_K : X86_FORM_KMOVQ_R64_K;
                    written[at++] = (X86Instruction){ .form = (uint16_t)move, .reg = stores[0], .rm = place->reg, .mode = BUSTER_X86_REGISTER_DIRECT };
                    written[at++] = (X86Instruction){ .form = (uint16_t)store, .reg = stores[0], .rm = stores[1], .mode = BUSTER_X86_MEMORY_BASE };
                }
                else if (place->size == 8 || place->size == 4)
                {
                    written[at++] = (X86Instruction){ .form = place->size == 8 ? X86_FORM_MOVQ_M64_MM : X86_FORM_MOVD_RM32_MM, .reg = place->reg, .rm = stores[1],
                                                      .mode = BUSTER_X86_MEMORY_BASE };
                }
                else
                {
                    written[at++] = (X86Instruction){ .form = X86_FORM_MOVD_RM32_MM, .reg = place->reg, .rm = stores[0], .mode = BUSTER_X86_REGISTER_DIRECT };
                    written[at++] = (X86Instruction){ .form = (uint16_t)store, .reg = stores[0], .rm = stores[1], .mode = BUSTER_X86_MEMORY_BASE };
                }
            }
        }
        for (uint32_t depth = 0; depth < 8; depth += 1)
        {
            for (uint32_t k = 0; k < count; k += 1)
            {
                const CheckAsmPlace* place = places + k;
                bool later = false;
                for (uint32_t o = k + 1; o < count; o += 1)
                {
                    later |= check_asm_x87_stores(places + o) && places[o].reg == depth;
                }
                if (check_asm_x87_stores(place) && place->reg == depth)
                {
                    // Another output at this depth after it takes the same value (#1846): stored from a copy.
                    if (later)
                    {
                        written[at++] = (X86Instruction){ .form = X86_FORM_FLD_ST, .rm = 0, .mode = BUSTER_X86_REGISTER_DIRECT };
                    }
                    written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R64_RM64, .reg = stores[1], .rm = BUSTER_TIER0_ASM_BLOCK, .mode = BUSTER_X86_MEMORY_BASE,
                                                      .displacement = (int32_t)place->slot };
                    const uint32_t form = place->size == 4 ? X86_FORM_FSTP_M32 : place->size == 8 ? X86_FORM_FSTP_M64 : X86_FORM_FSTP_M80;
                    written[at++] = (X86Instruction){ .form = (uint16_t)form, .rm = stores[1], .mode = BUSTER_X86_MEMORY_BASE };
                }
            }
        }
        for (uint32_t p = 0; p < pops; p += 1)
        {
            written[at++] = (X86Instruction){ .form = X86_FORM_FSTP_ST, .rm = 0, .mode = BUSTER_X86_REGISTER_DIRECT };
        }
        if (labels != 0)
        {
            written[at++] = (X86Instruction){ .form = X86_FORM_MOV_R32_RM32, .reg = X86_REGISTER_RAX, .rm = exit, .mode = BUSTER_X86_REGISTER_DIRECT };
        }
    }
    *records = written;
    return at;
}

// Whether `form` returns: ret, and its imm16, 16-bit, 32-bit and prefixed forms.
static bool check_asm_return(uint32_t form)
{
    return form == X86_FORM_RET || form == X86_FORM_RET_IMM16 || form == X86_FORM_RETW || form == X86_FORM_RETW_IMM16 || form == X86_FORM_RETD ||
           form == X86_FORM_RETD_IMM16 || form == X86_FORM_REP_RET || form == X86_FORM_REPNE_RET;
}

// The forms common in inline assembly that x86_semantics.c gives only its conservative record (every register, unknown
// control), with the registers each writes, as the instruction set's references define them: none touches the stack,
// and each returns to the next instruction (syscall through the kernel). A form of neither keeps its statement's helper.
typedef struct CheckAsmImplicit CheckAsmImplicit;
struct CheckAsmImplicit
{
    uint16_t form;
    uint32_t writes;
};

#define BUSTER_CHECK_ASM_GPR(r) (1u << X86_REGISTER_##r)
static const CheckAsmImplicit check_asm_implicit[] = {
    { X86_FORM_CPUID, BUSTER_CHECK_ASM_GPR(RAX) | BUSTER_CHECK_ASM_GPR(RBX) | BUSTER_CHECK_ASM_GPR(RCX) | BUSTER_CHECK_ASM_GPR(RDX) },
    { X86_FORM_RDTSC, BUSTER_CHECK_ASM_GPR(RAX) | BUSTER_CHECK_ASM_GPR(RDX) },
    { X86_FORM_RDTSCP, BUSTER_CHECK_ASM_GPR(RAX) | BUSTER_CHECK_ASM_GPR(RCX) | BUSTER_CHECK_ASM_GPR(RDX) },
    { X86_FORM_RDPMC, BUSTER_CHECK_ASM_GPR(RAX) | BUSTER_CHECK_ASM_GPR(RDX) },
    { X86_FORM_XGETBV, BUSTER_CHECK_ASM_GPR(RAX) | BUSTER_CHECK_ASM_GPR(RDX) },
    { X86_FORM_SYSCALL, BUSTER_CHECK_ASM_GPR(RAX) | BUSTER_CHECK_ASM_GPR(RCX) | BUSTER_CHECK_ASM_GPR(R11) },
    // loop decrements rcx (ecx with an address-size prefix) and jrcxz writes nothing: each a branch to the template's own
    // record, whose rel8 displacement the assembler fixed (asm_assemble_code's fixed branches, #1839's item 4).
    { X86_FORM_LOOP_REL8, BUSTER_CHECK_ASM_GPR(RCX) },
    { X86_FORM_JRCXZ_REL8, 0 },
    { X86_FORM_FNSTSW_AX, BUSTER_CHECK_ASM_GPR(RAX) },
    { X86_FORM_FSTSW_AX, BUSTER_CHECK_ASM_GPR(RAX) },
    { X86_FORM_LDMXCSR_M32, 0 },
    { X86_FORM_FLDCW_M16, 0 },
    { X86_FORM_FXCH_ST, 0 },
    { X86_FORM_FNCLEX, 0 },
    { X86_FORM_FNINIT, 0 },
    { X86_FORM_FWAIT, 0 },
    { X86_FORM_EMMS, 0 },
    { X86_FORM_CLFLUSH_M8, 0 },
    { X86_FORM_SERIALIZE, 0 },
    { X86_FORM_XSETBV, 0 },
    { X86_FORM_VZEROUPPER, BUSTER_TIER0_XMM_ALL },
    { X86_FORM_VZEROALL, BUSTER_TIER0_XMM_ALL },
    // fxrstor loads xmm0 to xmm15 (and MXCSR) from its area, beside the x87's state.
    { X86_FORM_FXRSTOR_M, BUSTER_TIER0_XMM_ALL },
    { X86_FORM_FXRSTOR64_M, BUSTER_TIER0_XMM_ALL },
};
#undef BUSTER_CHECK_ASM_GPR

// Whether an asm body's register operands may be its call row's arguments (check_asm_inline_code's `passed`): not with
// #1846's MMX and opmask operands, which its records load through rax before the general registers' loads, nor A's
// rdx:rax pairs, nor register variables in a high byte, in XMM16 up or in rsp, which no argument's register is; a body of one
// loads every operand from its block, in the helper's order.
static bool check_asm_inline_passes(const CheckAsmPlace* places, uint32_t count)
{
    bool takes = true;
    for (uint32_t k = 0; k < count; k += 1)
    {
        takes &= places[k].kind != BUSTER_CHECK_ASM_MMX && places[k].kind != BUSTER_CHECK_ASM_MASK && places[k].kind != BUSTER_CHECK_ASM_PAIR &&
                 !places[k].high && !(places[k].kind == BUSTER_CHECK_ASM_VECTOR && (places[k].reg >= 16 || places[k].packed)) && !check_asm_stack(places + k);
    }
    return takes;
}

// The registers an asm body touches (#1839, Tier0AsmBody.touched, in BUSTER_TIER0_XMM's layout): `taken`'s general ones
// (its operands', its clobbers' and its stores'), the XMM registers of its vector operands and clobbers (`vectors`, a bit
// each), and what x86_semantics.c says its template's records write; or UINT32_MAX for a template it doesn't place: a
// record of unknown effects, or one that touches the stack or control flow (a push, a call, a jump or branch but the
// template's own to its labels, a trap), as unwind_tier0_frame refuses in a function's body, or that writes a register
// holding the frame (rsp, rbp, r15), but on its way out of the function. A form x86_semantics.c doesn't cover takes
// check_asm_implicit's registers, or none for an x87 form's, or keeps the helper. Into `*masks`, the opmask registers its
// records write (CheckProgramAsm.inline_masks). An asm goto's jumps to its labels (`gotos`, #1839's item 2) are branches of
// the body too, to its exits.
static uint32_t check_asm_inline_touched(const CheckAsmPlace* places, uint32_t count, const AsmCode* code, const CheckAsmGoto* gotos, uint32_t goto_count, uint32_t taken,
                                         uint32_t vectors, uint8_t* masks)
{
    uint32_t opmask_writes = 0;
    uint64_t written = (uint64_t)(taken & 0xffffu & ~(1u << X86_REGISTER_RSP)) | ((uint64_t)(vectors & 0xffffu) << 16);
    bool placed = true;
    for (uint32_t k = 0; k < count; k += 1)
    {
        written |= places[k].kind == BUSTER_CHECK_ASM_VECTOR && places[k].reg < 16 ? 1ull << (16 + places[k].reg) : 0;
        // An MMX or opmask operand's load goes through rax, and A's pair is rax and rdx (#1846); an opmask operand's
        // register is written by its load (CheckProgramAsm.inline_masks).
        written |= check_asm_mmx_loads(places + k) || check_asm_packed_loads(places + k) ? 1ull << X86_REGISTER_RAX : 0;
        written |= places[k].kind == BUSTER_CHECK_ASM_PAIR ? 1ull << X86_REGISTER_RAX | 1ull << X86_REGISTER_RDX : 0;
        opmask_writes |= places[k].kind == BUSTER_CHECK_ASM_MASK ? 1u << (places[k].reg & 7) : 0;
    }
    // A loop's or a jrcxz's (#1839's item 4): a rel8 record the assembler gave its displacement to a record of the template's
    // own (asm_assemble_code's fixed branches). A template of one takes no straight path to a return below.
    bool fixed = false;
    for (uint32_t r = 0; r < code->count; r += 1)
    {
        bool sized = false;
        for (uint32_t b = 0; b < code->branch_count; b += 1)
        {
            sized |= code->branches[b] == r;
        }
        fixed |= !sized && x86_form_description(code->records[r].form & BUSTER_X86_FORM_NUMBER)->relative_bytes == 1 && (code->records[r].form & BUSTER_X86_FORM_FIXUP) == 0;
    }
    for (uint32_t r = 0; placed && r < code->count; r += 1)
    {
        const X86Instruction* record = code->records + r;
        const X86Effects* effects = x86_effects(record->form);
        // A record that leaves the function (#1839): a return, or one on a straight path to a return (no branch among them,
        // no label after it on the way), which runs only on the way out, as `leave` and `pop %rbx` before a `ret`. Its
        // effects on the stack, the frame's registers and control are the template's way out of its caller, as in clang's
        // object, and what it writes no later code of the function reads: it stays out of the checks and of the set.
        bool out = false;
        for (uint32_t k = r; !out && k < code->count; k += 1)
        {
            bool branch = false;
            bool target = false;
            for (uint32_t b = 0; b < code->branch_count; b += 1)
            {
                branch |= code->branches[b] == k;
                target |= k > r && code->targets[b] == k;
            }
            for (uint32_t j = 0; j < goto_count; j += 1)
            {
                branch |= gotos[j].record == k;
            }
            out = !target && !fixed && check_asm_return(code->records[k].form);
            if (branch || target)
            {
                break;
            }
        }
        if (out)
        {
            continue;
        }
        uint64_t reads = 0;
        uint64_t writes = 0;
        // With the opmask registers it reads and writes: a record that names one otherwise resolves as everything.
        uint16_t opmasks = 0;
        x86_instruction_effects_opmasks_scalar(record, &reads, &writes, &opmasks);
        opmask_writes |= (uint32_t)opmasks >> 8;
        if (effects->operation == X86_OPERATION_NONE)
        {
            uint32_t implicit = 0;
            while (implicit < BUSTER_ARRAY_LENGTH(check_asm_implicit) && check_asm_implicit[implicit].form != record->form)
            {
                implicit += 1;
            }
            // An x87 form (fsqrt, fprem, fxch, fld %st(1), fxsave...) reads and writes the x87's registers, its status
            // and control words, the flags and memory alone: a general register only by `fnstsw %ax`'s, and xmm0 to
            // xmm15 only by fxrstor's, which the table has.
            const char* mnemonic = x86_form_description(record->form)->mnemonic;
            const bool x87 = mnemonic != NULL && mnemonic[0] == 'f' && record->form != X86_FORM_FNSTSW_AX && record->form != X86_FORM_FSTSW_AX;
            // An MMX form (#1846: paddd, pxor, movq and movd between MMX and general registers, cvtpi2ps...): it touches
            // the MMX registers (the x87's), memory through its operand, and of the other registers at most the general
            // and XMM ones its fields name, which it is taken to write; none touches the stack or control.
            X86Instruction copy = *record;
            const Tier0Fields fields = tier0_operation_fields(&copy);
            bool mmx = false;
            uint64_t named = 0;
            for (uint32_t f = 0; f < fields.count; f += 1)
            {
                const uint32_t class = fields.classes[f];
                const bool addressing = fields.bytes[f] == &copy.index || (fields.bytes[f] == &copy.rm && copy.mode != BUSTER_X86_REGISTER_DIRECT);
                mmx |= class == BUSTER_X86_CLASS_MMX;
                named |= !addressing && class >= BUSTER_X86_CLASS_GPR8 && class <= BUSTER_X86_CLASS_GPR64 ? 1ull << (*fields.bytes[f] & 15) : 0;
                named |= !addressing && class == BUSTER_X86_CLASS_XMM && (*fields.bytes[f] & 31) < 16 ? 1ull << (16 + (*fields.bytes[f] & 15)) : 0;
            }
            placed = implicit < BUSTER_ARRAY_LENGTH(check_asm_implicit) || x87 || mmx;
            written |= implicit < BUSTER_ARRAY_LENGTH(check_asm_implicit) ? check_asm_implicit[implicit].writes : mmx ? named : 0;
        }
        else
        {
            // A local branch of the template's own (its branch columns'), to one of its labels, is a branch of the body.
            bool local = false;
            for (uint32_t b = 0; b < code->branch_count; b += 1)
            {
                local |= code->branches[b] == r;
            }
            for (uint32_t j = 0; j < goto_count; j += 1)
            {
                local |= gotos[j].record == r;
            }
            // A fixed branch's rel8 form, to the template's own record.
            local |= x86_form_description(record->form & BUSTER_X86_FORM_NUMBER)->relative_bytes == 1 && (record->form & BUSTER_X86_FORM_FIXUP) == 0;
            const uint32_t control = x86_effects_control(effects);
            placed = (control == BUSTER_X86_CONTROL_NONE || (local && (control == BUSTER_X86_CONTROL_JUMP || control == BUSTER_X86_CONTROL_BRANCH))) &&
                     (effects->memory & (BUSTER_X86_MEMORY_EFFECT_STACK_READ | BUSTER_X86_MEMORY_EFFECT_STACK_WRITE)) == 0 &&
                     (writes & ((local ? 0 : BUSTER_X86_RESOURCE_CONTROL) | BUSTER_X86_RESOURCE_CALL)) == 0;
            written |= writes & (BUSTER_X86_RESOURCE_GPRS | BUSTER_X86_RESOURCE_LOWER_VECTORS);
        }
    }
    const uint32_t touched = (uint32_t)written;
    placed = placed && (touched & (1u << X86_REGISTER_RSP | 1u << X86_REGISTER_RBP | 1u << X86_REGISTER_R15)) == 0;
    *masks = (uint8_t)opmask_writes;
    return placed ? touched : UINT32_MAX;
}

// The index the templates' instructions are matched against: every form the assembler's syntax takes, whatever the
// unit's target, as GNU as and clang's integrated assembler take a template's instructions. Made on `scratch` once per
// lowering of a unit with asm statements (the assembler guide measures its initialization), or 0 without room.
static const X86MatchIndex* check_asm_matches(Arena* scratch)
{
    X86MatchIndex* index = BUSTER_ARENA_PUSH_ARRAY(scratch, X86MatchIndex, 1);
    if (index != 0)
    {
        const X86Target everything = { .features = (UINT64_C(1) << X86_FEATURE_COUNT) - 1, .architecture = X86_ARCH_OTHER };
        if (!x86_encoder_initialized)
        {
            x86_encoder_initialize();
        }
        if (!x86_semantics_initialized)
        {
            x86_semantics_initialize();
        }
        x86_match_initialize(index, &everything);
    }
    return index;
}

// The text being written at the top of `output` for a template that takes directives (check_asm_unit_helper): appends
// bytes, text and a decimal number, and clears `*valid` without room.
static void check_asm_unit_bytes(Arena* output, const void* bytes, uint64_t count, bool* valid)
{
    uint8_t* at = *valid && count != 0 ? arena_push(output, count, 1) : 0;
    *valid &= count == 0 || at != 0;
    if (at != 0)
    {
        memcpy(at, bytes, count);
    }
}

static void check_asm_unit_text(Arena* output, const char* text, bool* valid)
{
    check_asm_unit_bytes(output, text, strlen(text), valid);
}

static void check_asm_unit_number(Arena* output, int64_t value, bool* valid)
{
    char digits[24];
    const int length = snprintf(digits, sizeof(digits), "%lld", (long long)value);
    check_asm_unit_bytes(output, digits, (uint64_t)length, valid);
}

// Whether template `body`, assembled alone (asm_assemble, a template that takes directives), uses statement `statement`'s
// asm goto labels (`labels` of them, by the names check_asm_substitute printed, their `.L` made `__`) only as a jmp's or a jcc's target: each
// fixup to one is in the template's code (AsmResult.entry_atom), a rel32 field of a jmp (E9) or a jcc (0F 80 to 8F)
// without an addend, as the record path takes them (check_program_asm_plan_one's gotos). A label used otherwise (data, a
// call's target, an addend, a rip-relative address) is refused by name, since the helper's exit isn't the C label.
static bool check_asm_unit_gotos(const AsmResult* body, uint32_t statement, uint32_t labels)
{
    const Link* link = &body->link;
    bool jumps = true;
    for (AtomId a = 1; a < link->atoms.count; a += 1)
    {
        const uint8_t* bytes = link->atoms.bytes[a];
        for (uint32_t f = 0; f < link->atoms.fixup_count[a]; f += 1)
        {
            const FixupId fixup = link->atoms.fixup_begin[a] + f;
            const SymbolId target = link->fixups.target[fixup];
            const NameId name_id = target < link->symbols.count ? link->symbols.name[target] : 0;
            // The name as the substitution printed it (`.L` for the `__` the caller gave it).
            char local[64];
            const uint32_t length = name_id != 0 && name_id < link->names.count && link->names.length[name_id] < sizeof(local) ? link->names.length[name_id] : 0;
            if (length >= 2)
            {
                memcpy(local, link->names.bytes[name_id], length);
            }
            const bool renamed = length >= 2 && local[0] == '_' && local[1] == '_';
            local[0] = '.';
            local[1] = 'L';
            if (renamed && check_asm_goto_label((String8){ .pointer = local, .length = length }, statement, labels) != BUSTER_CHECK_NONE)
            {
                const uint32_t offset = link->fixups.offset[fixup];
                const uint8_t kind = link->fixups.kind[fixup];
                const bool field = a == body->entry_atom && bytes != 0 && (kind == BUSTER_LINK_FIXUP_PC32 || kind == BUSTER_LINK_FIXUP_CALL) && link->fixups.addend[fixup] == -4 &&
                                   offset >= 2 && (uint64_t)offset + 4 <= link->atoms.size[a];
                const bool jmp = field && bytes[offset - 1] == 0xe9;
                const bool jcc = field && bytes[offset - 2] == 0x0f && (bytes[offset - 1] & 0xf0) == 0x80;
                jumps &= jmp || jcc;
            }
        }
    }
    return jumps;
}

// Records `records[0, count)` as the bytes they encode, a `.byte` line of them each (check_asm_unit_helper).
static void check_asm_unit_records(Arena* output, const X86Instruction* records, uint32_t count, bool scalar, bool* valid)
{
    for (uint32_t r = 0; *valid && r < count; r += 1)
    {
        uint8_t bytes[BUSTER_X86_ENCODER_CAPACITY(1)];
        const int64_t length = BUSTER_SCALAR_PATH(scalar) ? x86_encode_scalar(records + r, 1, bytes, sizeof(bytes), 0, 0) : x86_encode(records + r, 1, bytes, sizeof(bytes), 0, 0);
        *valid = length > 0 && length <= 15;
        check_asm_unit_text(output, ".byte ", valid);
        for (int64_t b = 0; *valid && b < length; b += 1)
        {
            check_asm_unit_number(output, bytes[b], valid);
            check_asm_unit_text(output, b + 1 < length ? "," : "\n", valid);
        }
    }
}

// `.Lbuster_goto_S_` and its label's number, or `tail` for the exits' common tail.
static void check_asm_unit_goto(Arena* output, uint32_t statement, uint32_t label, bool* valid)
{
    check_asm_unit_text(output, BUSTER_CHECK_ASM_GOTO_PREFIX, valid);
    check_asm_unit_number(output, statement, valid);
    check_asm_unit_text(output, "_", valid);
    if (label != BUSTER_CHECK_NONE)
    {
        check_asm_unit_number(output, label, valid);
    }
    else
    {
        check_asm_unit_text(output, "tail", valid);
    }
}

// The whole helper of a template that takes directives, as GNU assembly: the record helper's own code
// (check_asm_helper_code's, for a template of no records: its pushes, frame, the block's address and every operand's load,
// the x87's, MMX's, opmasks' and A's pairs' among them) as the bytes it encodes, then the template `text` (already
// substituted, in AT&T syntax unless it switches itself), then the rest of that code as bytes in the helper's default
// text section: an asm goto's exits, each label's (`.Lbuster_goto_S_L`, the name the substitution gave the template's
// jumps to it) setting the exit register and jumping to the tail, then the flag outputs' setcc stores, the register,
// MMX, opmask, pair and x87 outputs' stores, the x87 pops, the exit into eax, and the epilogue. Its code is its default
// text section's atom, at AsmResult.entry_atom. A failure keeps nothing.
static AsmResult check_asm_unit_helper(const CheckAsmPlace* places, uint32_t count, const uint8_t stores[2], bool block, uint32_t saved, uint32_t pops, uint32_t statement,
                                       uint32_t labels, uint8_t exit, String8 text, const X86MatchIndex* index, bool scalar, Arena* output, Arena* scratch)
{
    const uint64_t mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const AsmCode empty = { 0 };
    uint32_t first = 0;
    const Tier0Code code = check_asm_helper_code(places, count, &empty, saved, stores, block, pops, 0, 0, labels, exit, &first, scratch);
    bool valid = code.error == TIER0_OK;
    check_asm_unit_text(output, ".att_syntax prefix\n", &valid);
    check_asm_unit_records(output, code.records, valid ? first : 0, scalar, &valid);
    check_asm_unit_bytes(output, text.pointer, text.length, &valid);
    // The template may have left another syntax or another section behind; the helper's stores and epilogue are its own
    // default text section's.
    if (text.length != 0 && text.pointer[text.length - 1] != '\n')
    {
        check_asm_unit_text(output, "\n", &valid);
    }
    check_asm_unit_text(output, ".att_syntax prefix\n.text\n", &valid);
    // An asm goto's exits (check_asm_helper_code's, after the template): the fall-through's move and jump, then each
    // label's, the last one's move falling into the tail. Their jumps are to the tail's label here.
    const uint32_t exits = labels != 0 ? 2 * labels + 1 : 0;
    for (uint32_t r = 0; valid && r < exits; r += 1)
    {
        if (r >= 2 && r % 2 == 0)
        {
            check_asm_unit_goto(output, statement, r / 2 - 1, &valid);
            check_asm_unit_text(output, ":\n", &valid);
        }
        if (r % 2 == 0)
        {
            check_asm_unit_records(output, code.records + first + r, 1, scalar, &valid);
        }
        else
        {
            check_asm_unit_text(output, "jmp ", &valid);
            check_asm_unit_goto(output, statement, BUSTER_CHECK_NONE, &valid);
            check_asm_unit_text(output, "\n", &valid);
        }
    }
    if (exits != 0)
    {
        check_asm_unit_goto(output, statement, BUSTER_CHECK_NONE, &valid);
        check_asm_unit_text(output, ":\n", &valid);
    }
    check_asm_unit_records(output, valid ? code.records + first + exits : 0, valid ? code.count - first - exits : 0, scalar, &valid);
    const uint64_t length = output->length - mark;
    AsmResult result = valid ? asm_assemble((String8){ .pointer = (char*)output->base + mark, .length = length }, index,
                                            (AsmOptions){ .vectorized = !BUSTER_SCALAR_PATH(scalar), .integrated = true }, output) :
                               (AsmResult){ .error = "arena exhausted placing inline assembly's unit" };
    if (result.error != 0)
    {
        output->length = mark;
        result = (AsmResult){ .error = result.error, .location = result.location };
    }
    arena_pop_to(scratch, scratch_mark);
    return result;
}

// Whether input `checked` is an integer constant an immediate takes (the checker's, without side effects, of an integer
// or a pointer type), and then its value as clang prints it into `*constant`: its type's bits (`*bits` of them),
// sign-extended.
static bool check_asm_integer_constant(const TypeTable* types, const CheckAsmCheckedOperand* checked, int64_t* constant, uint32_t* bits)
{
    const CheckValue* value = &checked->value;
    const TypeId type = checked->conversion.type != 0 ? checked->conversion.type : value->type;
    uint32_t width = 0;
    bool is_signed = false;
    const bool integer = check_action_integer_type(types, type, &width, &is_signed) || check_action_kind(types, type) == BUSTER_TYPE_KIND_POINTER;
    *bits = width != 0 ? width : 64;
    const uint64_t raw = (uint64_t)value->integer;
    *constant = *bits >= 64 ? (int64_t)raw : (int64_t)(raw << (64 - *bits)) >> (64 - *bits);
    return value->constant_kind == CHECK_CONSTANT_INTEGER && value->fault == CHECK_FAULT_NONE && (value->flags & BUSTER_CHECK_SIDE_EFFECT) == 0 && integer;
}

// Whether a cast from `source` to `destination` keeps its operand's value, as clang's IgnoreParenNoopCasts takes it: to
// its own type (an lvalue's conversion among them), or between integers and pointers of one width.
static bool check_asm_noop_cast(const TypeTable* types, TypeId source, TypeId destination)
{
    uint32_t width = 0;
    bool is_signed = false;
    const bool from = check_action_integer_type(types, source, &width, &is_signed) || check_action_kind(types, source) == BUSTER_TYPE_KIND_POINTER;
    const bool to = check_action_integer_type(types, destination, &width, &is_signed) || check_action_kind(types, destination) == BUSTER_TYPE_KIND_POINTER;
    const bool sized = type_is_complete(types, source) && type_is_complete(types, destination);
    return type_unqualified(source) == type_unqualified(destination) || (from && to && sized && type_size(types, source) == type_size(types, destination));
}

// A global register variable's register (#1846, the owner's decision on #1447): `register long sp __asm__("rsp")` at file
// scope, whose reads and writes are the register's, as clang's code makes them (llvm.read_register and write_register):
// X86_REGISTER_RSP or X86_REGISTER_RBP, with its bytes (8, or 4 for esp's and ebp's) in `*bytes`; or -1 for an object
// that is no such variable. The checker took only these four names (check_input_register_label).
static int32_t check_program_global_register(const CheckProgram* program, uint32_t object, uint32_t* bytes)
{
    const CheckInput* input = program->input;
    const uint32_t declaration = object != 0 && object <= input->token_count ? object - 1 : BUSTER_CHECK_NONE;
    const bool global = declaration < input->token_count && check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_REGISTER) &&
                        check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC);
    const CheckInputAttributes* attributes = global ? check_input_attributes_at(input, declaration) : 0;
    int32_t result = -1;
    for (uint32_t i = 0; attributes != 0 && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        const String8 name = attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL && !attribute->ignored ? attribute->text : (String8){ 0 };
        if (name.length == 3 && (name.pointer[0] == 'r' || name.pointer[0] == 'e') && (memcmp(name.pointer + 1, "sp", 2) == 0 || memcmp(name.pointer + 1, "bp", 2) == 0))
        {
            result = name.pointer[1] == 's' ? X86_REGISTER_RSP : X86_REGISTER_RBP;
            *bytes = name.pointer[0] == 'r' ? 8 : 4;
        }
    }
    return result;
}

// The register an asm operand takes when its expression names an explicit register variable (`register long r10
// asm("r10") = a4;` and then "r"(r10), musl's syscall wrappers'), as clang's code gives it (CodeGen's
// AddVariableConstraints): the variable itself, through casts that keep its value (check_asm_noop_cast), so that
// "r"((unsigned long)r10) is in r10 but "r"((int)r10) and "r"(r10 + 0) aren't. Its constraint is then that register,
// whatever its letters: "a"(r10) is in r10, and a matching input is in it rather than in its output's register. A
// general register's number (0 to 15, by any of its names), 16 plus an XMM register's (16 to 47: XMM16 up, #1846), 48
// plus a high byte's register's (ah 48, ch 49, dh 50, bh 51), 64 plus an MMX register's (mm0 64 to mm7 71), 72 plus an
// opmask register's (k0 72 to k7 79), 80 for st (st(0)) and 81 for st(1) (#1846), 90 for rsp (esp, sp:
// BUSTER_CHECK_ASM_VARIABLE_RSP, #1846), 96 plus a YMM register's and 128 plus a ZMM register's (#1846), -1 for an operand
// that names no such variable, or -2 for a register the lowering doesn't place one in: APX's r16 to r31 and the x87's
// deeper registers.
// An object's id is its first declaration's token plus one, which has its attributes.
static int32_t check_asm_variable_register(const CheckProgram* program, uint32_t node, bool* global)
{
    const CheckInput* input = program->input;
    const CheckStream* stream = program->stream;
    const CheckGraph* graph = &input->graph;
    uint32_t at = node;
    bool walking = true;
    while (walking && at < stream->count && (graph->ops[at] == CHECK_OP_CAST || graph->ops[at] == CHECK_OP_CONVERT))
    {
        // Operands are earlier nodes, so the walk ends.
        const uint32_t inner = graph->operand0[at];
        walking = inner < at && check_asm_noop_cast(program->types, check_stream_type(stream, inner), check_stream_type(stream, at));
        at = walking ? inner : at;
    }
    const CheckValue leaf = at < stream->count && graph->ops[at] == CHECK_OP_VALUE ? check_stream_value(stream, at) : (CheckValue){ 0 };
    const CheckValue* value = at < stream->count && graph->ops[at] == CHECK_OP_VALUE ? &leaf : 0;
    const uint32_t declaration = value != 0 && (value->flags & BUSTER_CHECK_REGISTER) != 0 && value->object != 0 && value->object <= input->token_count ?
                                 value->object - 1 : BUSTER_CHECK_NONE;
    const CheckInputAttributes* attributes = declaration < input->token_count && check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_REGISTER) ? check_input_attributes_at(input, declaration) : 0;
    // A global register variable's (#1846, check_program_global_register): its register itself.
    uint32_t global_bytes = 0;
    *global = declaration < input->token_count && check_program_global_register(program, declaration + 1, &global_bytes) >= 0;
    int32_t result = -1;
    for (uint32_t i = 0; attributes != 0 && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        String8 name = attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL && !attribute->ignored ? attribute->text : (String8){ 0 };
        if (name.length != 0 && (name.pointer[0] == '%' || name.pointer[0] == '#'))
        {
            name.pointer += 1;
            name.length -= 1;
        }
        int32_t reg = name.length != 0 ? -2 : -1;
        for (uint32_t size = 0; reg == -2 && size < 4; size += 1)
        {
            for (uint32_t r = 0; reg == -2 && r < 16; r += 1)
            {
                const char* spelling = check_asm_registers[size][r];
                const bool same = name.length == strlen(spelling) && memcmp(name.pointer, spelling, name.length) == 0;
                // rsp, esp and sp are 90 (#1846): clang knows no "spl" as a register variable's name.
                reg = !same ? -2 : r != X86_REGISTER_RSP ? (int32_t)r : size != 0 ? BUSTER_CHECK_ASM_VARIABLE_RSP : -2;
            }
        }
        // xmm0 to xmm31, without leading zeros, as clang's register names spell them (#1846: XMM16 up with AVX-512F), and
        // ymm0 to ymm31 (96 up) and zmm0 to zmm31 (128 up) (#1846).
        const char prefix = name.length != 0 ? name.pointer[0] : 0;
        const bool vector_name = (prefix == 'x' || prefix == 'y' || prefix == 'z') && name.length >= 3 && name.pointer[1] == 'm' && name.pointer[2] == 'm';
        const bool digits = (name.length == 4 || name.length == 5) && vector_name && name.pointer[3] >= '0' && name.pointer[3] <= '9' &&
                            (name.length == 4 || (name.pointer[3] != '0' && name.pointer[4] >= '0' && name.pointer[4] <= '9'));
        const int32_t number = !digits ? 99 : name.length == 4 ? name.pointer[3] - '0' : (name.pointer[3] - '0') * 10 + name.pointer[4] - '0';
        const int32_t first = prefix == 'x' ? 16 : prefix == 'y' ? BUSTER_CHECK_ASM_VARIABLE_YMM : BUSTER_CHECK_ASM_VARIABLE_ZMM;
        reg = reg == -2 && number < 32 ? first + number : reg;
        // ah, ch, dh and bh (#1846): 48 plus their register's number.
        static const char* const high[4] = { "ah", "ch", "dh", "bh" };
        for (uint32_t h = 0; reg == -2 && h < 4; h += 1)
        {
            reg = name.length == 2 && memcmp(name.pointer, high[h], 2) == 0 ? 48 + (int32_t)h : reg;
        }
        // mm0 to mm7 (64 up) and k0 to k7 (72 up), and the x87's st and st(0) (80) and st(1) (81), as clang names them
        // (#1846).
        const bool numbered = name.length == 3 && name.pointer[2] >= '0' && name.pointer[2] <= '7';
        reg = reg == -2 && numbered && name.pointer[0] == 'm' && name.pointer[1] == 'm' ? 64 + (name.pointer[2] - '0') : reg;
        reg = reg == -2 && name.length == 2 && name.pointer[0] == 'k' && name.pointer[1] >= '0' && name.pointer[1] <= '7' ? 72 + (name.pointer[1] - '0') : reg;
        reg = reg == -2 && ((name.length == 2 && memcmp(name.pointer, "st", 2) == 0) || (name.length == 5 && memcmp(name.pointer, "st(0)", 5) == 0)) ? 80 : reg;
        reg = reg == -2 && name.length == 5 && memcmp(name.pointer, "st(1)", 5) == 0 ? 81 : reg;
        result = reg != -1 ? reg : result;
    }
    return result;
}

// An __m64 input's object (#1846, CheckAsmPlace.m64): the node of the lvalue its value is read from, past the casts that
// keep its value (check_asm_noop_cast) and its lvalue conversion, or BUSTER_CHECK_NONE for a value of no object.
static uint32_t check_asm_m64_object(const CheckProgram* program, uint32_t node)
{
    const CheckStream* stream = program->stream;
    const CheckGraph* graph = &program->input->graph;
    uint32_t at = node;
    bool walking = true;
    while (walking && at < stream->count && (graph->ops[at] == CHECK_OP_CAST || graph->ops[at] == CHECK_OP_CONVERT))
    {
        const uint32_t inner = graph->operand0[at];
        walking = inner < at && check_asm_noop_cast(program->types, check_stream_type(stream, inner), check_stream_type(stream, at));
        at = walking ? inner : at;
    }
    const bool object = at < stream->count && check_stream_value(stream, at).category == CHECK_LVALUE;
    return object ? at : BUSTER_CHECK_NONE;
}

// Plans statement `s` (its operands from `operands`, check_asms_run's) into `plan`: its places, and its helper's code
// into `helper`, or the reason the lowering refuses it, its template matched against `index`. False only for an arena
// without room, with both arenas then as they were.
static uint8_t check_lower_type_class(const TypeTable* types, TypeId type);

// The general registers a statement placed in its caller assembles its template with (check_asm_row), before its roles
// replace them: none rax, whose accumulator forms (`addl $1, %eax`, a one-byte xchg) name it implicitly, and none rsp.
static const uint8_t check_asm_row_registers[14] = {
    X86_REGISTER_RCX, X86_REGISTER_RDX, X86_REGISTER_RBX, X86_REGISTER_RSI, X86_REGISTER_RDI, X86_REGISTER_R8,  X86_REGISTER_R9,
    X86_REGISTER_R10, X86_REGISTER_R11, X86_REGISTER_R12, X86_REGISTER_R13, X86_REGISTER_R14, X86_REGISTER_R15, X86_REGISTER_RBP,
};

// An asm statement placed in its caller (#1059): its template's records as a row of Tier 0's X86 entry (Tier0Operation),
// which names its registers by role and which Tier 0 places among the caller's values, as it places an x86 builtin's
// row, with no helper, no call and no frame of its own. Its operands, in clang's order of evaluation (the order a
// helper's operand block is filled in, and the order check_lower_asm pushes them): each output's address, and a `+`
// register output's value after it; then each input's value (a matching input's converted to its output's type) or a
// memory input's address. A register output's register is its `+` value's operand, its matching input's operand, or else
// a scratch register (SCRATCH(k), or the floating stack's free VECTOR(k)), which no operand is in, as an early clobber
// needs; after the template, the row stores each register output through its address, at its width, in order. A memory
// operand's register is its address's operand. The row's result is VOID.
//
// Placed where the records name no register but its operands' (as the roles replace them), so that the row reads and
// writes its operands, its scratch registers, the flags and memory, and nothing else Tier 0 keeps: Tier 0's rule for
// every row (tier0_operation_valid), whose forms' effects are x86_semantics.c's, a general-purpose form's among them
// (tier0_operation_general_effects: no implicit register, stack, control flow, direction flag or x87 state). The
// template is assembled again for this, its operands in registers of check_asm_row_registers and xmm1 to xmm15, so that
// a literal register, which an extended statement's template writes `%%rax`, would be one no role maps: the template
// may write none (and so takes no `%%`, nor a syntax switch, which Intel's bare names would need), nor name a high byte
// register, an /is4 operand or one of the other classes. The clobbers are then the records': `cc` and `memory` need
// nothing, as Tier 0 keeps no flags and no memory values across an entry, and a register clobber names one the records
// don't write. The admitted operands are a general register's of any of the sixteen (r, q, g; not a fixed register,
// a smaller class's or an explicit register variable's), an XMM register's of a float or a double (x and v, not fixed),
// memory, and constants; the statement is no asm goto and its template has no label, branch or symbol. Returns false,
// both arenas as they were, for a statement it doesn't place, which keeps its helper; and sets `*valid` false where an
// arena had no room, so that the plan fails for want of it rather than taking a helper the room decided.
static bool check_asm_row(const CheckProgram* program, uint32_t s, const CheckAsmPlace* places, const CheckAsmConstraint* constraints, const X86MatchIndex* index,
                          bool scalar, Tier0Operation* row, bool* valid, Arena* output, Arena* scratch)
{
    const CheckInput* input = program->input;
    const CheckInputAsm* statement = input->asm_statements + s;
    const TypeTable* types = program->types;
    const uint32_t count = statement->output_count + statement->input_count;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const CheckAsmText text = statement->template_text;
    bool placed = !statement->is_goto && statement->label_count == 0 && count <= BUSTER_CHECK_ASM_OPERANDS;
    for (uint32_t at = 0; placed && at + 1 < text.length; at += 1)
    {
        const bool literal = !statement->basic && text.bytes[at] == '%' && text.bytes[at + 1] == '%';
        const bool syntax = text.bytes[at] == '_' && at + 7 <= text.length && memcmp(text.bytes + at, "_syntax", 7) == 0;
        placed = !literal && !syntax;
    }
    // The operands' kinds, and the registers to assemble the template with: a general register's or a memory operand's
    // from check_asm_row_registers, an XMM register's from xmm1, a matching input its output's.
    CheckAsmPlace* moved = placed && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckAsmPlace, count) : 0;
    *valid = !placed || count == 0 || moved != 0;
    placed = placed && *valid;
    uint32_t general = 0;
    uint32_t vector = 1;
    uint32_t operand_count = 0;
    for (uint32_t k = 0; placed && k < count; k += 1)
    {
        const CheckAsmPlace* place = places + k;
        const bool output = k < statement->output_count;
        const bool tied = !output && place->tied != BUSTER_CHECK_NONE;
        moved[k] = *place;
        if (place->kind == BUSTER_CHECK_ASM_GENERAL && !tied)
        {
            placed = constraints[k].general == BUSTER_CHECK_ASM_GENERAL_ALL && general < BUSTER_ARRAY_LENGTH(check_asm_row_registers);
            moved[k].reg = placed ? check_asm_row_registers[general] : 0;
            general += 1;
        }
        else if (place->kind == BUSTER_CHECK_ASM_MEMORY)
        {
            placed = general < BUSTER_ARRAY_LENGTH(check_asm_row_registers);
            moved[k].reg = placed ? check_asm_row_registers[general] : 0;
            general += 1;
        }
        else if (place->kind == BUSTER_CHECK_ASM_VECTOR && !tied)
        {
            // A float or a double's alone: a vector of 16 bytes or more is moved through its address (CheckAsmPlace.packed).
            placed = constraints[k].fixed_vector == 0 && vector < 16 && !place->packed;
            moved[k].reg = (uint8_t)vector;
            vector += 1;
        }
        else if (tied)
        {
            // Its output's register (assigned above: outputs come first), of one input alone.
            uint32_t matching = 0;
            for (uint32_t i = statement->output_count; i < count; i += 1)
            {
                matching += places[i].tied == place->tied;
            }
            placed = place->tied < statement->output_count && matching == 1 && (place->kind == BUSTER_CHECK_ASM_GENERAL || place->kind == BUSTER_CHECK_ASM_VECTOR);
            moved[k].reg = placed ? moved[place->tied].reg : 0;
        }
        else
        {
            placed = place->kind == BUSTER_CHECK_ASM_CONSTANT && !output;
        }
        // The row's operands: an output's address (and a `+` register one's value), an input's value or address.
        operand_count += place->kind == BUSTER_CHECK_ASM_CONSTANT ? 0 : output && place->read && place->kind != BUSTER_CHECK_ASM_MEMORY ? 2 : 1;
    }
    placed = placed && operand_count <= BUSTER_TIER0_OPERATION_OPERANDS;
    String8 source = { 0 };
    if (placed)
    {
        const char* reason = check_asm_substitute(statement, moved, program->identifiers, s, scratch, &source, valid);
        placed = *valid && reason == NULL;
    }
    AsmCode code = { 0 };
    if (placed)
    {
        code = asm_assemble_code(source, index, (AsmOptions){ .vectorized = !BUSTER_SCALAR_PATH(scalar), .integrated = true }, output, scratch);
        *valid = code.error == NULL || strstr(code.error, "arena exhausted") == NULL;
        placed = code.error == NULL && !code.directive && code.count != 0 && code.branch_count == 0 && code.reference_count == 0 && code.label_count == 0;
    }
    // Each operand's row operand, and each register's role: an operand's, or a scratch register's for a register output
    // that no `+` value or matching input sets.
    Tier0Operation candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.result_class = BUSTER_TYPED_CLASS_VOID;
    uint8_t* addresses = placed && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count) : 0;
    uint8_t* sources = placed && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count) : 0;
    *valid = *valid && (!placed || count == 0 || (addresses != 0 && sources != 0));
    placed = placed && *valid;
    uint32_t operand = 0;
    uint32_t scratch_count = 0;
    uint32_t vector_count = 0;
    for (uint32_t k = 0; placed && k < count; k += 1)
    {
        const CheckAsmPlace* place = moved + k;
        const bool output = k < statement->output_count;
        const bool registered = place->kind == BUSTER_CHECK_ASM_GENERAL || place->kind == BUSTER_CHECK_ASM_VECTOR;
        const bool floating = place->kind == BUSTER_CHECK_ASM_VECTOR;
        const TypeId value_type = !output && place->tied != BUSTER_CHECK_NONE ? places[place->tied].type : place->type;
        const uint8_t class = check_lower_type_class(types, value_type);
        if (output || place->kind == BUSTER_CHECK_ASM_MEMORY)
        {
            addresses[k] = (uint8_t)operand;
            candidate.operand_classes[operand] = BUSTER_TYPED_CLASS_U64;
            operand += 1;
        }
        if (registered && (!output || place->read))
        {
            const uint8_t role = floating ? (uint8_t)BUSTER_TIER0_ROLE_FLOATING(operand) : (uint8_t)BUSTER_TIER0_ROLE_INTEGER(operand);
            placed = class != UINT8_MAX && (floating ? tier0_class_floating(class) : tier0_class_integer(class));
            candidate.operand_classes[operand] = class;
            operand += 1;
            sources[k] = role;
            if (!output && place->tied != BUSTER_CHECK_NONE)
            {
                // A matching input's operand is its output's register.
                sources[place->tied] = role;
            }
        }
        else if (registered)
        {
            // An output that only the template sets: a scratch register, unless a matching input's operand sets it (the
            // input's, which comes later, writes its role).
            bool matched = false;
            for (uint32_t i = statement->output_count; i < count; i += 1)
            {
                matched |= places[i].tied == k;
            }
            const uint32_t taken = floating ? vector_count : scratch_count;
            sources[k] = floating ? (uint8_t)BUSTER_TIER0_ROLE_VECTOR(taken) : (uint8_t)BUSTER_TIER0_ROLE_SCRATCH(taken);
            vector_count += floating && !matched ? 1 : 0;
            scratch_count += !floating && !matched ? 1 : 0;
            placed = (floating ? vector_count <= BUSTER_TIER0_OPERATION_VECTORS : scratch_count <= BUSTER_TIER0_SCRATCH) && class != UINT8_MAX &&
                     (floating ? tier0_class_floating(class) : tier0_class_integer(class));
        }
    }
    candidate.operand_count = (uint8_t)operand;
    // The registers' roles, by the moved places: a memory operand's address register, a register operand's value.
    uint8_t general_roles[16];
    uint8_t vector_roles[16];
    memset(general_roles, 0xff, sizeof(general_roles));
    memset(vector_roles, 0xff, sizeof(vector_roles));
    for (uint32_t k = 0; placed && k < count; k += 1)
    {
        const CheckAsmPlace* place = moved + k;
        const bool output = k < statement->output_count;
        if (place->kind == BUSTER_CHECK_ASM_MEMORY)
        {
            general_roles[place->reg] = (uint8_t)BUSTER_TIER0_ROLE_INTEGER(addresses[k]);
        }
        else if (place->kind == BUSTER_CHECK_ASM_GENERAL && (output || place->tied == BUSTER_CHECK_NONE))
        {
            general_roles[place->reg] = sources[k];
        }
        else if (place->kind == BUSTER_CHECK_ASM_VECTOR && (output || place->tied == BUSTER_CHECK_NONE))
        {
            vector_roles[place->reg] = sources[k];
        }
    }
    // The template's records with their roles, then the outputs' stores.
    for (uint32_t r = 0; placed && r < code.count; r += 1)
    {
        X86Instruction record = code.records[r];
        Tier0Fields fields = tier0_operation_fields(&record);
        placed = !fields.is4 && candidate.record_count < BUSTER_TIER0_OPERATION_RECORDS && (record.decorations & BUSTER_TIER0_DECORATION_ROLE) == 0;
        for (uint32_t f = 0; placed && f < fields.count; f += 1)
        {
            const uint8_t reg = *fields.bytes[f];
            const uint32_t class = fields.classes[f];
            if (tier0_general_register_class(class))
            {
                placed = reg < 16 && general_roles[reg] != 0xff;
                *fields.bytes[f] = placed ? general_roles[reg] : reg;
            }
            else if (class == BUSTER_X86_CLASS_XMM)
            {
                placed = reg < 16 && vector_roles[reg] != 0xff;
                *fields.bytes[f] = placed ? vector_roles[reg] : reg;
            }
            else
            {
                // An opmask register by number (a row without masks may name k0 to k7, which Tier 0 saves around it);
                // any other class, a YMM or ZMM register among them, no role takes.
                placed = class == BUSTER_X86_CLASS_OPMASK && reg < 8;
            }
        }
        if (placed)
        {
            candidate.records[candidate.record_count] = record;
            candidate.record_count += 1;
        }
    }
    for (uint32_t k = 0; placed && k < statement->output_count; k += 1)
    {
        const CheckAsmPlace* place = moved + k;
        if (place->kind == BUSTER_CHECK_ASM_GENERAL || place->kind == BUSTER_CHECK_ASM_VECTOR)
        {
            const bool floating = place->kind == BUSTER_CHECK_ASM_VECTOR;
            const uint32_t form = floating ? (place->size == 4 ? X86_FORM_MOVSS_XM32_X : X86_FORM_MOVSD_XM64_X)
                                  : place->size == 1 ? X86_FORM_MOV_RM8_R8
                                  : place->size == 2 ? X86_FORM_MOV_RM16_R16
                                  : place->size == 4 ? X86_FORM_MOV_RM32_R32
                                                     : X86_FORM_MOV_RM64_R64;
            placed = candidate.record_count < BUSTER_TIER0_OPERATION_RECORDS && (floating ? place->size == 4 || place->size == 8 : place->size != 0);
            if (placed)
            {
                candidate.records[candidate.record_count] = (X86Instruction){
                    .form = (uint16_t)form,
                    .reg = sources[k],
                    .rm = (uint8_t)BUSTER_TIER0_ROLE_INTEGER(addresses[k]),
                    .mode = BUSTER_X86_MEMORY_BASE,
                };
                candidate.record_count += 1;
            }
        }
    }
    placed = placed && *valid && tier0_operation_valid(&candidate);
    arena_pop_to(output, output_mark);
    arena_pop_to(scratch, scratch_mark);
    if (placed)
    {
        *row = candidate;
    }
    return placed;
}

static bool check_program_asm_plan_one(const CheckProgram* program, uint32_t s, const CheckAsmCheckedOperand* operands, const X86MatchIndex* index,
                                       const X86Target* instruction_set, bool scalar, CheckProgramAsm* plan, CheckProgramHelper* helper, Arena* output, Arena* scratch)
{
    (void)scalar;

    const CheckInput* input = program->input;
    const CheckInputAsm* statement = input->asm_statements + s;
    const TypeTable* types = program->types;
    const uint32_t count = statement->output_count + statement->input_count;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    *plan = (CheckProgramAsm){ .helper = BUSTER_CHECK_NONE, .planned = true };
    CheckAsmPlace* places = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckAsmPlace, count) : 0;
    CheckAsmConstraint* constraints = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckAsmConstraint, count) : 0;
    bool valid = count == 0 || (places != 0 && constraints != 0);
    const char* reason = count > BUSTER_CHECK_ASM_OPERANDS ? "inline assembly statements of more than 64 operands" : NULL;
    // The registers no operand may take: the clobbers' (and rsp), and the XMM, MMX and opmask ones the clobbers name.
    uint32_t taken = 1u << X86_REGISTER_RSP;
    uint32_t vectors = 0;
    uint32_t mmx = 0;
    uint32_t masks = 0;
    // The vectors of 16 bytes or more that a template of no instructions evaluates alone (CheckAsmPlace.packed), a bit each.
    uint64_t barriers = 0;
    for (uint32_t c = 0; valid && reason == NULL && c < statement->clobber_count; c += 1)
    {
        char buffer[16];
        const CheckAsmText clobber = check_asm_clobber_name(statement->clobbers[c], buffer);
        const bool mmx_clobber = clobber.length == 3 && clobber.bytes[0] == 'm' && clobber.bytes[1] == 'm' && clobber.bytes[2] >= '0' && clobber.bytes[2] <= '7';
        mmx |= mmx_clobber ? 1u << (clobber.bytes[2] - '0') : 0;
        const bool mask_clobber = clobber.length == 2 && clobber.bytes[0] == 'k' && clobber.bytes[1] >= '0' && clobber.bytes[1] <= '7';
        masks |= mask_clobber ? 1u << (clobber.bytes[1] - '0') : 0;
        const int32_t reg = check_asm_clobber(clobber);
        if (reg == -2)
        {
            reason = "APX's registers in inline assembly";
        }
        else if (reg >= 0 && reg < 16)
        {
            taken |= 1u << reg;
        }
        else if (reg >= 16 && reg < 48)
        {
            vectors |= 1u << (reg - 16);
        }
    }
    // Each operand's constraint and type, and its kind of place.
    for (uint32_t k = 0; valid && reason == NULL && k < count; k += 1)
    {
        const bool output = k < statement->output_count;
        const CheckAsmCheckedOperand* checked = operands + k;
        const CheckValue* value = &checked->value;
        // An input's value is its conversion's; an output's and a memory operand's, its lvalue's.
        const TypeId type = output ? value->type : checked->conversion.type != 0 ? checked->conversion.type : value->type;
        const uint8_t kind = check_action_kind(types, type);
        const uint64_t size = type_is_complete(types, type) ? type_size(types, type) : 0;
        uint32_t width = 0;
        bool is_signed = false;
        const bool integer = check_action_integer_type(types, type, &width, &is_signed) || kind == BUSTER_TYPE_KIND_POINTER;
        const bool floating = kind == BUSTER_TYPE_KIND_FLOATING && (size == 4 || size == 8);
        const bool registered = (integer || floating) && (size == 1 || size == 2 || size == 4 || size == 8);
        // A vector of an XMM, YMM or ZMM register's bytes (#1846: CheckAsmPlace.packed).
        const bool packed = (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR) && (size == 16 || size == 32 || size == 64);
        constraints[k] = check_asm_constraint_parse(statement, program->identifiers, statement->operands[k].constraint, output);
        CheckAsmConstraint* constraint = constraints + k;
        CheckAsmPlace* place = places + k;
        *place = (CheckAsmPlace){
            .type = type,
            .slot = BUSTER_CHECK_NONE,
            .value = BUSTER_CHECK_NONE,
            .tied = output ? BUSTER_CHECK_NONE : constraint->tied,
            .size = registered ? (uint8_t)size : 0,
            .written = output,
            .read = !output || (statement->operands[k].flags & CHECK_ASM_READ) != 0,
        };
        // An integer constant as clang prints it: its type's bits, sign-extended. One its letters' ranges don't take goes in
        // a register where the constraint allows one too ("rI"(40)), as GCC and clang place it.
        int64_t extended = 0;
        uint32_t bits = 64;
        const bool constant = !output && check_asm_integer_constant(types, checked, &extended, &bits);
        const bool fits = constant && constraint->immediate && check_asm_constant_fits(constraint, extended, bits);
        // An explicit register variable's register, whatever the constraint's letters (check_asm_variable_register): an
        // output's even of a memory constraint, which clang's code makes that register too, but not an input's of one,
        // which clang refuses as it lowers it ("cannot handle indirect register inputs yet").
        bool global = false;
        const int32_t variable = check_asm_variable_register(program, checked->node, &global);
        const bool memory_only = constraint->memory && constraint->general == 0 && !constraint->vector && constraint->x87 == 0 && !constraint->mmx && constraint->masks == 0;
        const uint32_t x87 = check_asm_x87_bytes(types, type);
        if (constraint->refused != NULL)
        {
            reason = constraint->refused;
        }
        else if ((value->flags & BUSTER_CHECK_BIT_FIELD) != 0 || (check_stream_flags(program->stream, checked->node) & BUSTER_CHECK_BIT_FIELD) != 0)
        {
            reason = "bit-fields as inline assembly operands";
        }
        else if (constraint->flag != 0)
        {
            // A flag output: the whole object written, of a size an integer store covers.
            const bool alone = constraint->general == 0 && !constraint->memory && !constraint->vector && constraint->x87 == 0 && !constraint->immediate;
            // An _Atomic object takes its value by an exchange, clang's sequentially consistent store, of 8 bytes at most:
            // clang's code stores a 16-byte one by a library call.
            place->kind = BUSTER_CHECK_ASM_FLAG;
            place->reg = (uint8_t)(constraint->flag - 1);
            place->size = size == 1 || size == 2 || size == 4 || size == 8 || size == 16 ? (uint8_t)size : 0;
            place->atomic = type_kind(types, type_strip_aligned(types, type)) == BUSTER_TYPE_KIND_ATOMIC;
            place->read = false;
            place->tied = BUSTER_CHECK_NONE;
            reason = !alone ? "inline assembly flag outputs with other alternatives" :
                     place->size == 0 || (place->atomic && place->size > 8) ? "inline assembly flag outputs of this size" : NULL;
        }
        else if (variable == -2)
        {
            reason = "explicit register variables of this register as inline assembly operands";
        }
        else if (variable >= 0 && !output && memory_only)
        {
            reason = "explicit register variables as inline assembly memory inputs";
        }
        else if (global && (variable == BUSTER_CHECK_ASM_VARIABLE_RSP || variable == X86_REGISTER_RBP))
        {
            // A global register variable (#1846): the register itself, whose value clang's code reads into a register and
            // moves back, and after the template reads and writes back, the same value: the template names it, and nothing
            // is loaded or stored. rbp's gives its function a frame pointer (check_lower_asm), as clang's reads rbp in one.
            place->kind = BUSTER_CHECK_ASM_GENERAL;
            place->tied = BUSTER_CHECK_NONE;
            place->global = true;
            place->written = false;
            place->read = false;
            constraint->general = 1u << (variable == BUSTER_CHECK_ASM_VARIABLE_RSP ? X86_REGISTER_RSP : X86_REGISTER_RBP);
        }
        else if (variable == BUSTER_CHECK_ASM_VARIABLE_RSP && integer && registered)
        {
            // rsp's variable (#1846, the owner's decision on #1447): an input's value moved into rsp before the template
            // and an output's read from it after, at the variable's width (spl, sp, esp or rsp), as clang's code does
            // (check_asm_stack).
            place->kind = BUSTER_CHECK_ASM_GENERAL;
            place->tied = BUSTER_CHECK_NONE;
            constraint->general = 1u << X86_REGISTER_RSP;
        }
        else if ((variable >= 0 && variable < 16 && registered) || (variable >= 48 && variable < 52 && registered))
        {
            // Fixed to it (taken first, below), and not a matching input's: clang gives it the variable's register. A high
            // byte register's (48 to 51) takes its whole register: a byte's is the high byte, a wider value's the
            // register's low bytes, as clang's code takes "ah" for a short's ax.
            const uint32_t reg = variable >= 48 ? (uint32_t)variable - 48 : (uint32_t)variable;
            place->kind = BUSTER_CHECK_ASM_GENERAL;
            place->tied = BUSTER_CHECK_NONE;
            place->high = variable >= 48 && size == 1;
            constraint->general = 1u << reg;
        }
        else if (packed && ((variable >= 16 && variable < 48 && size == 16) ||
                            (variable >= BUSTER_CHECK_ASM_VARIABLE_YMM && variable < BUSTER_CHECK_ASM_VARIABLE_YMM + 32 && size == 32) ||
                            (variable >= BUSTER_CHECK_ASM_VARIABLE_ZMM && variable < BUSTER_CHECK_ASM_VARIABLE_ZMM + 32 && size == 64)))
        {
            // A vector in its own size's register (#1846, the owner's decision on #1447): xmm's 16 bytes, ymm's 32 and zmm's
            // 64, as clang's code takes them (another size's is its "does not match required type").
            const uint32_t number = (uint32_t)variable - (variable < 48 ? 16 : variable < BUSTER_CHECK_ASM_VARIABLE_ZMM ? BUSTER_CHECK_ASM_VARIABLE_YMM :
                                                                                                                        BUSTER_CHECK_ASM_VARIABLE_ZMM);
            place->kind = BUSTER_CHECK_ASM_VECTOR;
            place->tied = BUSTER_CHECK_NONE;
            place->packed = true;
            place->size = (uint8_t)size;
            place->vex = (instruction_set->features & BUSTER_X86_FEATURE(AVX)) != 0;
            constraint->fixed_vector = (uint8_t)(number + 1);
            reason = check_asm_packed_refused(instruction_set, (uint32_t)size, number);
        }
        else if (variable >= 16 && variable < 48 && floating)
        {
            // XMM16 to XMM31 need AVX-512F's EVEX moves, without which clang can't place one ("could not allocate").
            place->kind = BUSTER_CHECK_ASM_VECTOR;
            place->tied = BUSTER_CHECK_NONE;
            constraint->fixed_vector = (uint8_t)(variable - 15);
            reason = variable >= 32 && (instruction_set->features & BUSTER_X86_FEATURE(AVX512F)) == 0 ?
                     "explicit register variables in XMM16 to XMM31 without AVX-512F" : NULL;
        }
        else if (variable >= 64 && variable < 72 && registered)
        {
            // An MMX register's variable (#1846), whatever its constraint's letters, as clang's code places it.
            place->kind = BUSTER_CHECK_ASM_MMX;
            place->size = (uint8_t)size;
            place->tied = BUSTER_CHECK_NONE;
            constraint->fixed_mmx = (uint8_t)(variable - 63);
        }
        else if (variable >= 72 && variable < 80 && integer && registered)
        {
            // An opmask register's variable (#1846), by k's moves and their features.
            const uint64_t needed = size <= 2 ? BUSTER_X86_FEATURE(AVX512F) : BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW);
            place->kind = BUSTER_CHECK_ASM_MASK;
            place->tied = BUSTER_CHECK_NONE;
            constraint->masks = 1u << (variable - 72);
            reason = (instruction_set->features & needed) != needed ? size <= 2 ? "inline assembly opmask operands without AVX-512F" :
                                                                         "inline assembly opmask operands of 4 or 8 bytes without AVX-512BW" : NULL;
        }
        else if ((variable == 80 || variable == 81) && x87 != 0)
        {
            // st or st(1)'s variable (#1846): t's depth or u's, as clang's code takes it.
            place->kind = BUSTER_CHECK_ASM_X87;
            place->tied = BUSTER_CHECK_NONE;
            place->reg = (uint8_t)(variable - 80);
            place->size = (uint8_t)x87;
            constraint->x87 = (uint8_t)(variable - 79);
        }
        else if (variable >= 0)
        {
            reason = "inline assembly operands of this type in their constraint's registers";
        }
        else if (place->tied != BUSTER_CHECK_NONE)
        {
            // A matching input: its output's place, below.
        }
        else if (constraint->immediate && constant && (fits || constraint->general == 0 || !registered))
        {
            place->kind = BUSTER_CHECK_ASM_CONSTANT;
            place->constant = extended;
            reason = fits ? NULL : "inline assembly constants outside their constraint's range";
        }
        else if ((constraint->x87 == 1 || constraint->x87 == 2 || constraint->x87 == BUSTER_CHECK_ASM_X87_ANY) && x87 != 0)
        {
            // A float, a double or a long double at t's depth or u's, or f's, which the planning places below (an
            // integer there is refused below, as clang's code can't place one).
            place->kind = BUSTER_CHECK_ASM_X87;
            place->reg = constraint->x87 == BUSTER_CHECK_ASM_X87_ANY ? BUSTER_CHECK_ASM_X87_ANY : constraint->x87 == 2 ? 1 : 0;
            place->size = (uint8_t)x87;
        }
        else if (constraint->pair && integer && size == 16)
        {
            // An __int128 in rdx:rax, taken whole below.
            place->kind = BUSTER_CHECK_ASM_PAIR;
            place->reg = X86_REGISTER_RAX;
            place->size = 16;
        }
        else if (constraint->general != 0 && registered)
        {
            place->kind = BUSTER_CHECK_ASM_GENERAL;
            place->wide = constraint->pair;
        }
        else if (constraint->mmx && registered)
        {
            place->kind = BUSTER_CHECK_ASM_MMX;
            place->size = (uint8_t)size;
        }
        else if (constraint->mmx && (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR) && size == 8)
        {
            // An __m64 (#1846): its 8 bytes, an object's, read through its address (CheckAsmPlace.m64), as clang's code
            // moves them by movq; an input that isn't an object's, through a copy's (#1846, the owner's decision on #1447).
            place->kind = BUSTER_CHECK_ASM_MMX;
            place->size = 8;
            place->type = BUSTER_TYPE_ULONG;
            place->m64 = true;
            place->object = output ? checked->node : check_asm_m64_object(program, checked->node);
        }
        else if (constraint->masks != 0 && integer && registered)
        {
            // kmovw's widths need AVX-512F, kmovd's and kmovq's AVX-512BW, as clang's code moves them (it can't place one
            // without: "could not allocate output register").
            const uint64_t needed = size <= 2 ? BUSTER_X86_FEATURE(AVX512F) : BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW);
            place->kind = BUSTER_CHECK_ASM_MASK;
            reason = (instruction_set->features & needed) != needed ? size <= 2 ? "inline assembly opmask operands without AVX-512F" :
                                                                         "inline assembly opmask operands of 4 or 8 bytes without AVX-512BW" : NULL;
        }
        else if (constraint->vector && floating)
        {
            place->kind = BUSTER_CHECK_ASM_VECTOR;
        }
        else if (constraint->memory && (output || value->category == CHECK_LVALUE))
        {
            place->kind = BUSTER_CHECK_ASM_MEMORY;
        }
        else if (constraint->vector && packed)
        {
            // A vector of 16, 32 or 64 bytes (#1846, the owner's decision on #1447), in xmm0 up, through its address. In a
            // template of no instructions, it is evaluated alone, as a held operand was (below), but a volatile output,
            // which clang's code loads and stores.
            place->kind = BUSTER_CHECK_ASM_VECTOR;
            place->packed = true;
            place->size = (uint8_t)size;
            place->vex = (instruction_set->features & BUSTER_X86_FEATURE(AVX)) != 0;
            barriers |= !output || (type_qualifiers(type) & BUSTER_TYPE_VOLATILE) == 0 ? UINT64_C(1) << k : 0;
            reason = check_asm_packed_refused(instruction_set, (uint32_t)size, 0);
        }
        else if (constraint->vector && (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR) &&
                 (!output || (type_qualifiers(type) & BUSTER_TYPE_VOLATILE) == 0))
        {
            place->kind = BUSTER_CHECK_ASM_HELD;
        }
        else
        {
            reason = constraint->immediate && !constraint->memory && constraint->general == 0 && !constraint->vector && !constraint->mmx && constraint->masks == 0 ?
                     "inline assembly immediates that aren't integer constants" : "inline assembly operands of this type in their constraint's registers";
        }
    }
    // The fixed registers first, then the outputs' registers of a class, the inputs', the matching inputs (their outputs'),
    // and the memory operands' addresses; the XMM registers apart. An input fixed to an output's fixed register (as
    // "=a"(r) : "a"(n)) shares it, as GCC and clang let it, unless the output is early-clobbered or read too: it is that
    // output's matching input then.
    // An A output of 16 bytes that is neither early-clobbered nor `+` lets an input fixed to rax or rdx share its register,
    // as clang's code shares it (mulq's "=A"(r) : "a"(x)): the template reads the input before it writes the pair.
    uint32_t pair_shared = 0;
    bool stack_taken = false;
    for (uint32_t o = 0; valid && reason == NULL && o < statement->output_count; o += 1)
    {
        const bool open = places[o].kind == BUSTER_CHECK_ASM_PAIR && (statement->operands[o].flags & (CHECK_ASM_EARLY | CHECK_ASM_READ)) == 0;
        pair_shared |= open ? 1u << X86_REGISTER_RAX | 1u << X86_REGISTER_RDX : 0;
    }
    for (uint32_t pass = 0; valid && reason == NULL && pass < 4; pass += 1)
    {
        for (uint32_t k = 0; reason == NULL && k < count; k += 1)
        {
            CheckAsmPlace* place = places + k;
            const uint32_t allowed = constraints[k].general;
            const bool fixed = (allowed & (allowed - 1)) == 0;
            const bool output = k < statement->output_count;
            int32_t reg = -1;
            uint32_t sharing = BUSTER_CHECK_NONE;
            // An A input beside an A output of 16 bytes shares its pair too.
            const bool pair = place->kind == BUSTER_CHECK_ASM_PAIR;
            for (uint32_t o = 0; pass == 0 && !output && ((place->kind == BUSTER_CHECK_ASM_GENERAL && fixed) || pair) && o < statement->output_count; o += 1)
            {
                const bool same = places[o].kind == place->kind && places[o].tied == BUSTER_CHECK_NONE && constraints[o].general == allowed;
                sharing = same && (statement->operands[o].flags & (CHECK_ASM_EARLY | CHECK_ASM_READ)) == 0 ? o : sharing;
            }
            if (sharing != BUSTER_CHECK_NONE && place->tied == BUSTER_CHECK_NONE)
            {
                place->tied = sharing;
            }
            else if (pass == 0 && place->kind == BUSTER_CHECK_ASM_GENERAL && place->tied == BUSTER_CHECK_NONE && allowed == 1u << X86_REGISTER_RSP)
            {
                // rsp's variable (#1846), which no other operand takes: an input beside an rsp output shares it (above).
                reg = !stack_taken ? X86_REGISTER_RSP : -2;
                stack_taken = true;
            }
            else if (pass == 0 && place->kind == BUSTER_CHECK_ASM_GENERAL && place->tied == BUSTER_CHECK_NONE && fixed)
            {
                const bool shares = !output && (pair_shared & allowed) != 0;
                reg = (taken & allowed) == 0 ? check_asm_take(allowed, &taken) : shares ? __builtin_ctz(allowed) : -2;
                pair_shared &= shares ? ~allowed : pair_shared;
            }
            else if (pass == 0 && pair && place->tied == BUSTER_CHECK_NONE)
            {
                // rax and rdx both.
                const uint32_t both = 1u << X86_REGISTER_RAX | 1u << X86_REGISTER_RDX;
                reg = (taken & both) == 0 ? X86_REGISTER_RAX : -2;
                taken |= both;
            }
            else if (pass == 0 && place->kind == BUSTER_CHECK_ASM_VECTOR && constraints[k].fixed_vector != 0)
            {
                // An explicit register variable's XMM register, before any other operand takes one.
                const uint32_t bit = 1u << (constraints[k].fixed_vector - 1);
                reg = (vectors & bit) == 0 ? constraints[k].fixed_vector - 1 : -2;
                vectors |= bit;
            }
            else if (pass == 0 && place->kind == BUSTER_CHECK_ASM_MMX && constraints[k].fixed_mmx != 0)
            {
                // An explicit register variable's MMX register (#1846), before any other operand takes one.
                const uint32_t bit = 1u << (constraints[k].fixed_mmx - 1);
                reg = (mmx & bit) == 0 ? constraints[k].fixed_mmx - 1 : -2;
                mmx |= bit;
            }
            else if (pass == 0 && place->kind == BUSTER_CHECK_ASM_MASK && place->tied == BUSTER_CHECK_NONE && (constraints[k].masks & (constraints[k].masks - 1)) == 0)
            {
                // One opmask register (a variable's, #1846), before the others take one.
                reg = (masks & constraints[k].masks) == 0 ? __builtin_ctz(constraints[k].masks) : -2;
                masks |= constraints[k].masks;
            }
            else if (pass == 1 && place->kind == BUSTER_CHECK_ASM_GENERAL && place->tied == BUSTER_CHECK_NONE && !fixed && output)
            {
                reg = check_asm_take(allowed, &taken);
                reg = reg < 0 ? -3 : reg;
            }
            else if (pass == 2 && place->kind == BUSTER_CHECK_ASM_GENERAL && place->tied == BUSTER_CHECK_NONE && !fixed && !output)
            {
                reg = check_asm_take(allowed, &taken);
                reg = reg < 0 ? -3 : reg;
            }
            else if (pass == 3 && place->kind == BUSTER_CHECK_ASM_MEMORY)
            {
                reg = check_asm_take(BUSTER_CHECK_ASM_GENERAL_ALL, &taken);
                reg = reg < 0 ? -3 : reg;
            }
            else if (pass == 3 && place->kind == BUSTER_CHECK_ASM_VECTOR && place->tied == BUSTER_CHECK_NONE && constraints[k].fixed_vector == 0)
            {
                const uint32_t free = ~vectors & 0xffffu;
                reg = free != 0 ? __builtin_ctz(free) : -3;
                vectors |= free != 0 ? 1u << reg : 0;
            }
            else if (pass == 3 && place->kind == BUSTER_CHECK_ASM_MMX && place->tied == BUSTER_CHECK_NONE && constraints[k].fixed_mmx == 0)
            {
                // mm0 up, but the clobbered ones.
                const uint32_t free = ~mmx & 0xffu;
                reg = free != 0 ? __builtin_ctz(free) : -3;
                mmx |= free != 0 ? 1u << reg : 0;
            }
            else if (pass == 3 && place->kind == BUSTER_CHECK_ASM_MASK && place->tied == BUSTER_CHECK_NONE && (constraints[k].masks & (constraints[k].masks - 1)) != 0)
            {
                // k0 up (Yk's k1 up), but the clobbered ones, as clang's allocation takes them.
                const uint32_t free = ~masks & constraints[k].masks;
                reg = free != 0 ? __builtin_ctz(free) : -3;
                masks |= free != 0 ? 1u << reg : 0;
            }
            else if (pass == 3 && place->tied != BUSTER_CHECK_NONE)
            {
                // A matching input takes its output's register, which a register output of its own has (the checker
                // checked the match's types alike); a `+` output reads its own value instead. On the x87's stack, at the
                // output's depth, as its own type loads (a float, a double or a long double).
                const CheckAsmPlace* target = place->tied < statement->output_count ? places + place->tied : 0;
                const uint32_t x87 = check_asm_x87_bytes(types, place->type);
                if (target == 0 ||
                    (target->kind != BUSTER_CHECK_ASM_GENERAL && target->kind != BUSTER_CHECK_ASM_VECTOR && target->kind != BUSTER_CHECK_ASM_X87 && target->kind != BUSTER_CHECK_ASM_MMX &&
                     target->kind != BUSTER_CHECK_ASM_MASK && target->kind != BUSTER_CHECK_ASM_PAIR) ||
                    target->read || target->global || (target->kind == BUSTER_CHECK_ASM_PAIR && place->size != 0 && place->size != 16) ||
                    (target->packed && !check_asm_packed_type(types, place->type, target->size)))
                {
                    reason = "inline assembly inputs matching an output that isn't a register's of its own";
                }
                else
                {
                    place->kind = target->kind;
                    place->reg = target->reg;
                    place->size = target->kind == BUSTER_CHECK_ASM_X87 ? (uint8_t)x87 : place->size != 0 ? place->size : target->size;
                    place->wide = target->wide;
                    place->high = target->high;
                    place->packed = target->packed;
                    place->vex = target->vex;
                }
            }
            if (reg >= 0)
            {
                place->reg = (uint8_t)reg;
            }
            else if (reg == -2)
            {
                reason = "inline assembly operands whose fixed register is another operand's or a clobber";
            }
            else if (reg == -3)
            {
                reason = "inline assembly operands past the registers left";
            }
        }
    }
    for (uint32_t k = 0; valid && reason == NULL && k < count; k += 1)
    {
        // A matching input of a narrower register class than its output's (the checker matched their kinds).
        reason = places[k].kind != BUSTER_CHECK_ASM_CONSTANT && places[k].kind != BUSTER_CHECK_ASM_MEMORY && places[k].kind != BUSTER_CHECK_ASM_HELD &&
                 places[k].size == 0 ? "inline assembly operands of this type in their constraint's registers" : NULL;
    }
    // The x87's stack, by the rules LLVM's stackifier applies to an asm (X86FloatingPoint.cpp), as clang -c builds it:
    // the inputs' depths (`uses`, a `+` operand's and a matching input's among them) from st(0) without a gap, and so
    // the outputs' (`defs`), alone and with the clobbered x87 registers'; an input in an output's or a clobber's
    // register is popped by the template, and the popped ones lie above the others. The template finds the inputs at
    // their depths, and leaves the outputs, from st(0), above the inputs it didn't pop, which the helper pops after it
    // stores the outputs (`pops`). Two inputs or two outputs at one depth are one register's (below).
    // An f input takes the shallowest depth no t or u input, and no f input before it, has at the template's start (an
    // output alone the template pushes): each is in its own register below the fixed ones, which a template refers to by
    // %N whatever the depth (clang's code chooses by its register allocation), and none is popped by the template, as
    // LLVM's stackifier has an f input.
    uint32_t fixed = 0;
    for (uint32_t k = 0; valid && reason == NULL && k < count; k += 1)
    {
        const bool input = !places[k].written || places[k].read;
        fixed |= places[k].kind == BUSTER_CHECK_ASM_X87 && places[k].reg != BUSTER_CHECK_ASM_X87_ANY && input ? 1u << places[k].reg : 0;
    }
    // The f inputs' depths, which the template never pops.
    uint32_t chosen = 0;
    for (uint32_t k = 0; valid && reason == NULL && k < count; k += 1)
    {
        if (places[k].kind == BUSTER_CHECK_ASM_X87 && places[k].reg == BUSTER_CHECK_ASM_X87_ANY)
        {
            places[k].reg = (uint8_t)__builtin_ctz(~fixed);
            fixed |= 1u << places[k].reg;
            chosen |= places[k].reg < 32 ? 1u << places[k].reg : 0;
            reason = places[k].reg >= 8 ? "inline assembly's x87 operands past the stack's eight registers" : NULL;
        }
    }
    uint32_t uses = 0;
    uint32_t defs = 0;
    uint32_t clobbered = 0;
    for (uint32_t k = 0; valid && reason == NULL && k < count; k += 1)
    {
        const CheckAsmPlace* place = places + k;
        const uint32_t bit = place->kind == BUSTER_CHECK_ASM_X87 ? 1u << place->reg : 0;
        const bool used = bit != 0 && (!place->written || place->read);
        const bool defined = bit != 0 && place->written;
        uses |= used ? bit : 0;
        defs |= defined ? bit : 0;
    }
    // Two inputs at one depth are one register's, as clang's code makes them (#1846): its value is the first matching
    // or `+` input's in clang's order of the inputs (the explicit ones, then the `+` outputs'), as LLVM copies the tied
    // ones last, the first of them last; else the last other input's, the last copy into the register. The others aren't
    // loaded. Two outputs at one depth each take its value after the template.
    for (uint32_t depth = 0; valid && reason == NULL && depth < 8 && (uses >> depth & 1) != 0; depth += 1)
    {
        uint32_t winner = BUSTER_CHECK_NONE;
        bool tied_won = false;
        for (uint32_t i = 0; i < count; i += 1)
        {
            // The inputs in clang's order: the explicit ones, then the `+` outputs'.
            const uint32_t k = i < statement->input_count ? statement->output_count + i : i - statement->input_count;
            const CheckAsmPlace* place = places + k;
            const bool input = place->kind == BUSTER_CHECK_ASM_X87 && place->reg == depth && (!place->written || place->read);
            const bool tied = input && (place->written || place->tied != BUSTER_CHECK_NONE);
            winner = input && !tied_won ? k : winner;
            tied_won |= tied;
        }
        for (uint32_t k = 0; k < count; k += 1)
        {
            CheckAsmPlace* place = places + k;
            place->shadowed |= place->kind == BUSTER_CHECK_ASM_X87 && place->reg == depth && (!place->written || place->read) && k != winner;
        }
    }
    for (uint32_t c = 0; valid && reason == NULL && c < statement->clobber_count; c += 1)
    {
        char buffer[16];
        clobbered |= check_asm_x87_clobber(check_asm_clobber_name(statement->clobbers[c], buffer));
    }
    const uint32_t popped = uses & ~chosen & (defs | clobbered);
    // Depths from st(0) without a gap: a mask of low bits (1, 3, 7...), or none.
    const bool stacked = (uses & (uses + 1)) == 0 && ((defs | clobbered) & ((defs | clobbered) + 1)) == 0 && (defs & (defs + 1)) == 0 &&
                         (popped & (popped + 1)) == 0;
    reason = reason == NULL && !stacked ? "inline assembly's x87 operands and clobbers off the stack's top" : reason;
    const uint32_t pops = (uint32_t)__builtin_popcount(uses) - (uint32_t)__builtin_popcount(popped);
    // The operand block: each register output's address (and a `+` one's value), each memory operand's address and each
    // input's value, 8 bytes each, but an x87 long double value's 16.
    uint32_t block = 0;
    for (uint32_t k = 0; valid && reason == NULL && k < count; k += 1)
    {
        CheckAsmPlace* place = places + k;
        const uint32_t bytes = (place->kind == BUSTER_CHECK_ASM_X87 || place->kind == BUSTER_CHECK_ASM_PAIR) && place->size > BUSTER_CHECK_ASM_SLOT_BYTES ? place->size :
                               BUSTER_CHECK_ASM_SLOT_BYTES;
        if (place->kind != BUSTER_CHECK_ASM_CONSTANT && place->kind != BUSTER_CHECK_ASM_HELD && !place->global)
        {
            place->slot = block;
            block += place->written ? BUSTER_CHECK_ASM_SLOT_BYTES : bytes;
        }
        if (place->written && place->read && place->kind != BUSTER_CHECK_ASM_MEMORY && place->kind != BUSTER_CHECK_ASM_HELD)
        {
            place->value = block;
            block += bytes;
        }
    }
    // The two registers the stores take after the template: none an output's, whose value they would lose.
    uint8_t stores[2] = { X86_REGISTER_RAX, X86_REGISTER_RAX };
    uint32_t avoided = 1u << X86_REGISTER_RSP;
    uint32_t outputs = 0;
    for (uint32_t k = 0; valid && reason == NULL && k < count; k += 1)
    {
        outputs += check_asm_stores(places + k) || check_asm_x87_stores(places + k) || check_asm_flag_stores(places + k) || check_asm_mmx_stores(places + k) ||
                   check_asm_pair_stores(places + k);
        avoided |= check_asm_stores(places + k) && places[k].kind == BUSTER_CHECK_ASM_GENERAL ? 1u << places[k].reg : 0;
        avoided |= check_asm_pair_stores(places + k) ? 1u << X86_REGISTER_RAX | 1u << X86_REGISTER_RDX : 0;
    }
    // A high byte's store takes no REX prefix, so its address's register is one of the first eight (#1846).
    bool high_output = false;
    for (uint32_t k = 0; valid && reason == NULL && k < count; k += 1)
    {
        high_output |= check_asm_stores(places + k) && places[k].high;
    }
    for (uint32_t i = 0; valid && reason == NULL && outputs != 0 && i < 2; i += 1)
    {
        const int32_t reg = check_asm_take(i == 1 && high_output ? BUSTER_CHECK_ASM_GENERAL_ALL & 0xffu : BUSTER_CHECK_ASM_GENERAL_ALL, &avoided);
        stores[i] = reg >= 0 ? (uint8_t)reg : X86_REGISTER_RAX;
        reason = reg < 0 ? "inline assembly operands past the registers left" : NULL;
        taken |= reg >= 0 ? 1u << reg : 0;
    }
    // A flag output's name is the register its setcc writes (check_asm_helper_code).
    for (uint32_t k = 0; valid && reason == NULL && k < count; k += 1)
    {
        places[k].named = places[k].kind == BUSTER_CHECK_ASM_FLAG ? stores[1] : 0;
    }
    // An asm goto's exit register (check_asm_helper_code): none an output's or a store's, which the tail reads after it is set.
    uint8_t exit = X86_REGISTER_RAX;
    if (valid && reason == NULL && statement->is_goto)
    {
        const int32_t reg = check_asm_take(BUSTER_CHECK_ASM_GENERAL_ALL, &avoided);
        exit = reg >= 0 ? (uint8_t)reg : X86_REGISTER_RAX;
        reason = reg < 0 ? "inline assembly operands past the registers left" : NULL;
        taken |= reg >= 0 ? 1u << reg : 0;
    }
    // The frame the helper's pushes leave (check_asm_helper_code): 8 or 16 bytes at the template's entry, rsp 16-byte
    // aligned there. `saved` is Tier0Code.frame_saved's encoding: bit r for tier0_callee_saved[r], not the registers'.
    uint32_t saved_positions = 0;
    for (uint32_t r = 0; r < BUSTER_TIER0_CALLEE_SAVED; r += 1)
    {
        saved_positions |= (taken >> tier0_callee_saved[r] & 1) != 0 ? 1u << r : 0;
    }
    plan->saved = saved_positions;
    plan->frame = __builtin_popcount(saved_positions) % 2 == 0 ? 8 : 16;
    // The template, its operands substituted, assembled by the checker's path.
    String8 text = { 0 };
    if (valid && reason == NULL)
    {
        reason = check_asm_substitute(statement, places, program->identifiers, s, scratch, &text, &valid);
    }
    AsmCode code = { 0 };
    AsmResult unit = { 0 };
    bool unit_assembled = false;
    bool returns = false;
    const uint64_t code_mark = output->length;
    if (valid && reason == NULL)
    {
        code = asm_assemble_code(text, index, (AsmOptions){ .vectorized = !BUSTER_SCALAR_PATH(scalar), .integrated = true }, output, scratch);
        if (code.error != NULL && code.directive)
        {
            // A template that takes a directive besides the syntax switches is assembled by the full assembler
            // (asm_assemble). A template of alignment and section directives alone has no code: its sections and symbols
            // are published, and the statement calls nothing, as clang's emits no instructions for it.
            const uint64_t body_mark = output->length;
            char* body_source = text.length != 0 ? arena_push(output, text.length, 1) : 0;
            bool body_valid = text.length == 0 || body_source != 0;
            if (body_source != 0)
            {
                memcpy(body_source, text.pointer, text.length);
            }
            // An asm goto's labels, which no source defines, as global names of the same length (`__buster_goto_S_L`), so
            // that the assembler keeps a fixup of each use for check_asm_unit_gotos.
            const uint64_t prefix = sizeof(BUSTER_CHECK_ASM_GOTO_PREFIX) - 1;
            for (uint64_t at = 0; statement->is_goto && body_source != 0 && at + prefix <= text.length; at += 1)
            {
                if (memcmp(body_source + at, BUSTER_CHECK_ASM_GOTO_PREFIX, prefix) == 0)
                {
                    body_source[at] = '_';
                    body_source[at + 1] = '_';
                }
            }
            AsmResult body = body_valid ? asm_assemble((String8){ .pointer = body_source, .length = text.length }, index,
                                                       (AsmOptions){ .vectorized = !BUSTER_SCALAR_PATH(scalar), .integrated = true }, output) :
                                          (AsmResult){ .error = "arena exhausted placing inline assembly's unit" };
            valid = body_valid;
            // A template of directives alone has no code to call, but with x87 operands or flag outputs, whose helper
            // passes the values through the x87's stack or sets the outputs' objects, as for a template of no instructions.
            bool stack_or_flags = false;
            for (uint32_t k = 0; k < count; k += 1)
            {
                stack_or_flags |= places[k].kind == BUSTER_CHECK_ASM_X87 || places[k].kind == BUSTER_CHECK_ASM_FLAG;
            }
            const bool entry_code = body.error == NULL && (body.entry_code || stack_or_flags);
            if (body.error == NULL && statement->is_goto && !check_asm_unit_gotos(&body, s, statement->label_count))
            {
                reason = "asm goto labels other than a jmp's or a jcc's target";
            }
            else if (body.error != NULL)
            {
                // A function's template's CFI (#1848): clang splices it into the function's own frame description at the
                // template's place, which a helper of its own, with its own unwind entry, can't be.
                const bool cfi = strstr(body.error, "CFI directive outside a procedure") != NULL;
                reason = cfi ? "inline assembly CFI directives in a function's template (clang adds them to the function's frame)" :
                         strstr(body.error, "arena exhausted") == NULL ? body.error : NULL;
                valid = body.error == NULL || reason != NULL;
            }
            else if (entry_code && body.ret_first)
            {
                // The template returns before any branch: its helper would leave its frame reserved, as the record path's
                // check refuses a record RET before a branch (check_program_asm_plan_one's record path).
                reason = "inline assembly templates that return from their helper";
            }
            else if (!entry_code)
            {
                unit = body;
                unit_assembled = true;
            }
            else
            {
                // The template's own code is a helper's: discard its standalone assembly and assemble the whole helper, its
                // frame and operand block around the template. An asm goto's labels are its jumps' targets alone.
                const uint32_t labels = statement->is_goto ? statement->label_count : 0;
                arena_pop_to(output, body_mark);
                unit = reason == NULL ? check_asm_unit_helper(places, count, stores, block != 0, taken & BUSTER_CHECK_ASM_CALLEE_SAVED, pops, s, labels, exit, text, index, scalar,
                                                              output, scratch) :
                                        (AsmResult){ .error = reason };
                unit_assembled = unit.error == NULL;
                reason = unit.error != NULL && strstr(unit.error, "arena exhausted") == NULL ? unit.error : NULL;
                valid = unit.error == NULL || reason != NULL;
            }
            // A template's CFI would be a frame of its own, which the helper's own unwind entry describes.
            for (uint32_t a = 1; unit_assembled && reason == NULL && a < unit.link.atoms.count; a += 1)
            {
                reason = unit.link.atoms.class[a] == BUSTER_LINK_CLASS_EH_FRAME ? "inline assembly's CFI directives" : NULL;
            }
        }
        else
        {
            reason = code.error != NULL && strstr(code.error, "arena exhausted") == NULL ? code.error : NULL;
            valid = code.error == NULL || reason != NULL;
            // A label with no instruction to follow in its template is a place in the statement's function: an asm body
            // of no records marks it (#1839), and a lowering that can't place one (Tier 1's) refuses it by name below.
            // A template that returns before any branch would leave a helper with its frame still reserved, where clang's
            // inline emission returns from the enclosing function: an asm body's return does that (#1839), and a helper's
            // is refused by name below. A template that jumps over its return (a called snippet's label) stays admitted,
            // as clang's own asm labels do.
            for (uint32_t r = 0; reason == NULL && r < code.count; r += 1)
            {
                const bool ret = check_asm_return(code.records[r].form);
                bool branch = false;
                for (uint32_t b = 0; b < code.branch_count; b += 1)
                {
                    branch |= code.branches[b] == r;
                }
                returns = ret;
                if (ret || branch)
                {
                    break;
                }
            }
        }
    }
    // An asm goto's jumps to its labels: the template's references to the names check_asm_substitute printed for them,
    // each a jmp's or a jcc's rel32 form without an addend (the assembler's reference of a branch to a name it doesn't
    // define), which check_asm_helper_code makes a branch to the label's exit.
    // Any other use of a label (a call's, data's, an addend) is refused by name.
    CheckAsmGoto* gotos = valid && reason == NULL && statement->is_goto && code.reference_count != 0 ?
                          BUSTER_ARENA_PUSH_ARRAY(scratch, CheckAsmGoto, code.reference_count) : 0;
    uint32_t goto_count = 0;
    valid = valid && (reason != NULL || !statement->is_goto || code.reference_count == 0 || gotos != 0);
    for (uint32_t r = 0; valid && reason == NULL && statement->is_goto && r < code.reference_count; r += 1)
    {
        const AsmCodeReference* reference = code.references + r;
        const uint32_t label = check_asm_goto_label(reference->name, s, statement->label_count);
        if (label != BUSTER_CHECK_NONE)
        {
            const uint32_t row = code.records[reference->record].form & ~BUSTER_X86_FORM_FIXUP;
            const bool branch = row > 0 && row < X86_FORM_COUNT && x86_form_descriptions[row].relative_bytes == 4 && x86_form_descriptions[row - 1].relative_bytes == 1 &&
                                strcmp(x86_form_descriptions[row].mnemonic, x86_form_descriptions[row - 1].mnemonic) == 0 &&
                                (strcmp(x86_form_descriptions[row].mnemonic, "jmp") == 0 || x86_form_descriptions[row].mnemonic[0] == 'j');
            const bool plain = reference->field == X86_FIXUP_DISPLACEMENT && reference->addend == 0 && (goto_count == 0 || gotos[goto_count - 1].record != reference->record);
            if (branch && plain)
            {
                gotos[goto_count++] = (CheckAsmGoto){ .record = reference->record, .label = label };
            }
            else
            {
                reason = "asm goto labels other than a jmp's or a jcc's target";
            }
        }
    }
    // A template assembled whole takes the record helper's loads and stores (check_asm_unit_helper): the x87's places, a
    // flag output's setcc, an asm goto's exits, MMX's, the opmasks', A's pairs and high bytes among them (#1839's item 10).
    // A held operand is refused in a helper, as on the record path.
    bool x87 = false;
    bool held = false;
    bool flagged = false;
    bool stack = false;
    bool stack_named = false;
    bool moved_vectors = false;
    bool packed_any = false;
    for (uint32_t k = 0; valid && reason == NULL && k < count; k += 1)
    {
        stack |= check_asm_stack(places + k);
        // rsp named by a global register variable's operand too (#1846), which a helper's rsp isn't.
        stack_named |= places[k].kind == BUSTER_CHECK_ASM_GENERAL && places[k].reg == X86_REGISTER_RSP;
        x87 |= places[k].kind == BUSTER_CHECK_ASM_X87;
        held |= places[k].kind == BUSTER_CHECK_ASM_HELD;
        packed_any |= places[k].packed;
        // A packed volatile output (#1846) is loaded and stored whatever its template, as clang's code does, and so is a
        // matching input's value moved into its output.
        moved_vectors |= places[k].packed && ((barriers >> k & 1) == 0 || places[k].tied != BUSTER_CHECK_NONE);
        flagged |= places[k].kind == BUSTER_CHECK_ASM_FLAG;
    }
    reason = valid && reason == NULL && unit_assembled && stack_named ? "explicit register variables in rsp in templates that take directives" : reason;
    // The MMX registers are the x87's: a statement of both (#1846, the owner's decision on #1447) loads its x87 operands
    // first and its MMX ones after, and stores the MMX outputs before the x87 ones, as clang's code orders them; what the
    // x87's stack holds after an MMX load is the template's to know, as in clang's object.
    reason = valid && reason == NULL && unit_assembled && unit.entry_code && held ? "inline assembly operands of this type in their constraint's registers" : reason;
    // The whole helper doesn't move a vector of 16 bytes or more through its address (#1846, CheckAsmPlace.packed).
    reason = valid && reason == NULL && unit_assembled && packed_any ? "inline assembly vector operands in templates that take directives" : reason;
    // A template of no instructions is no code at all but with x87 operands, whose helper passes their values through
    // the x87's stack as clang's code does (fld and fstp, which quiet a float's or a double's signaling NaN).
    // Only a statement whose places are all written has them to read: none with an arena that had no room for them, nor
    // past a refusal (#929's review).
    // A flag output's object is written whatever the template, as clang's code sets it after one of no instructions too, and
    // so is rsp's variable's (#1846: clang's code reads rsp after `asm("" : "=r"(sp))`), or rsp is set from its input's.
    const bool helped = code.count != 0 || x87 || flagged || stack || moved_vectors;
    // A template of no instructions that defines a label (#1839): a place in its caller's code, an asm body of no records
    // (but its operands' loads and stores) at which the label's symbol is defined. A body's labels keep its function on
    // Tier 0 (the IR's body call carries none), so in a lowering for Tier 1 (CheckProgramOptions.asm_bodies) a template of
    // instructions and labels keeps its helper, which Tier 1 calls, and only this one, which no helper places, is a body.
    const bool marked = code.count == 0 && code.label_count != 0 && !unit_assembled;
    Tier0Operation row;
    uint32_t inline_touched = 0;
    uint8_t inline_masks = 0;
    // The held operands (BUSTER_CHECK_ASM_HELD): refused by a statement with a helper, which would read or write their
    // registers, and in one without an output's object or an input's value, which the lowering evaluates alone.
    for (uint32_t k = 0; valid && reason == NULL && k < count; k += 1)
    {
        CheckAsmPlace* place = places + k;
        reason = place->kind == BUSTER_CHECK_ASM_HELD && helped ? "inline assembly operands of this type in their constraint's registers" : NULL;
        // A packed vector of a template of no instructions is evaluated alone, as a held one (#1846): main.c's barriers.
        const bool barrier = place->packed && !helped;
        place->kind = place->kind != BUSTER_CHECK_ASM_HELD && !barrier ? place->kind : place->written ? BUSTER_CHECK_ASM_MEMORY : BUSTER_CHECK_ASM_VECTOR;
        place->packed = place->packed && !barrier;
    }
    if (valid && reason == NULL && unit_assembled)
    {
        const bool unit_code = unit.entry_code;
        helper->code = (Tier0Code){ .error = TIER0_OK };
        plan->unit = unit;
        plan->unit_assembled = true;
        if (unit_code)
        {
            helper->id = CBUILTIN_NONE;
            helper->assembly = s + 1;
            helper->parameter_count = block != 0 ? 1 : 0;
            helper->parameter_types[0] = block != 0 ? BUSTER_TYPE_ULONG : BUSTER_TYPE_VOID;
            helper->parameter_types[1] = BUSTER_TYPE_VOID;
            helper->parameter_types[2] = BUSTER_TYPE_VOID;
            // An asm goto's helper returns its exit (check_asm_unit_helper), which its statement's code dispatches on.
            helper->result_type = statement->is_goto && statement->label_count != 0 ? BUSTER_TYPE_INT : BUSTER_TYPE_VOID;
            helper->compiler_barrier = true;
            plan->helper = 0;
        }
        else
        {
            // Directives alone: no helper, so the lowering evaluates the operands and calls nothing.
            plan->helper = BUSTER_CHECK_NONE;
        }
    }
    else if (valid && reason == NULL && helped && !x87 && !flagged && code.count != 0 && !program->asm_helpers && !program->asm_bodies &&
             !check_program_helper_forced(program, CHECK_PROGRAM_HELPER_ASM) &&
             check_asm_row(program, s, places, constraints, index, scalar, &row, &valid, output, scratch))
    {
        // Placed in its caller (#1059): the row, and no helper. The helper's assembly is dropped.
        arena_pop_to(output, code_mark);
        plan->row = BUSTER_ARENA_PUSH_ARRAY(output, Tier0Operation, 1);
        valid = plan->row != 0;
        if (valid)
        {
            *plan->row = row;
        }
    }
    else if (valid && reason == NULL && (helped || marked) && !program->asm_helpers && !check_program_helper_forced(program, CHECK_PROGRAM_HELPER_ASM) &&
             (!statement->is_goto || (!program->asm_bodies && !returns && statement->label_count != 0)) && code.reference_count == goto_count &&
             (code.label_count == 0 || (!returns && (!program->asm_bodies || marked))) &&
             (inline_touched = check_asm_inline_touched(places, count, &code, gotos, goto_count, taken, vectors, &inline_masks)) != UINT32_MAX)
    {
        // Placed in its caller as an asm body (#1839): its records run in its call row's place, with no helper.
        X86Instruction* records = 0;
        // Its register operands are its call row's arguments (#1839's measurements: a load of the block's slot, which the
        // lowering stored through a register a moment before, failed store-to-load forwarding once a run), in a lowering
        // for Tier 1 (CheckProgramOptions.asm_bodies) too, whose body call takes them past the block's address (#2144,
        // IrAsmBody.locations).
        const bool passed = check_asm_inline_passes(places, count);
        uint32_t first = 0;
        bool room = false;
        // An asm goto's (#1839's item 2): the helper's exits in the body, its exit number the call row's result.
        const uint32_t labels = statement->is_goto ? statement->label_count : 0;
        const uint32_t records_count = check_asm_inline_code(places, count, &code, stores, pops, passed, labels, exit, &records, &first, &room, output);
        plan->inline_passed = passed;
        const uint32_t branch_count = code.branch_count + goto_count + labels;
        uint32_t* branches = branch_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, branch_count) : 0;
        uint32_t* targets = branch_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, branch_count) : 0;
        valid = room && (branch_count == 0 || (branches != 0 && targets != 0));
        // The template's branches and its jumps to the labels, merged in record order, past the body's loads: a jump to a
        // label is a branch to its exit, two records a label past the fall-through's (check_asm_helper_code's), its rel8
        // form, its reference dropped; then the exits' jumps, each to the stores past the last exit's move.
        const uint32_t exits = first + code.count;
        const uint32_t tail = exits + (labels != 0 ? 2 * labels + 1 : 0);
        uint32_t branch = 0;
        uint32_t own = 0;
        uint32_t jump = 0;
        while (valid && (own < code.branch_count || jump < goto_count))
        {
            const bool mine = jump >= goto_count || (own < code.branch_count && code.branches[own] < gotos[jump].record);
            if (mine)
            {
                branches[branch] = first + code.branches[own];
                targets[branch++] = first + code.targets[own++];
            }
            else
            {
                X86Instruction* record = records + first + gotos[jump].record;
                record->form = (uint16_t)((record->form & ~BUSTER_X86_FORM_FIXUP) - 1);
                record->displacement = 0;
                branches[branch] = first + gotos[jump].record;
                targets[branch++] = exits + 2 + 2 * gotos[jump].label;
                jump += 1;
            }
        }
        for (uint32_t e = 0; valid && e < labels; e += 1)
        {
            branches[branch] = exits + 1 + 2 * e;
            targets[branch++] = tail;
        }
        plan->inline_branches = branches;
        plan->inline_targets = targets;
        plan->inline_branch_count = valid ? branch_count : 0;
        // Its labels, each at its record past the body's loads (#1839): the emission defines each name's symbol at the
        // record's offset in its caller's code (Tier0Code.asm_label_records), as a helper's at its own.
        plan->labels = valid && code.label_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramAsmLabel, code.label_count) : 0;
        plan->inline_label_records = valid && code.label_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, code.label_count) : 0;
        plan->inline_label_names = valid && code.label_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, code.label_count) : 0;
        valid = valid && (code.label_count == 0 || (plan->labels != 0 && plan->inline_label_records != 0 && plan->inline_label_names != 0));
        for (uint32_t k = 0; valid && k < code.label_count; k += 1)
        {
            plan->labels[k] = (CheckProgramAsmLabel){ .spelling = code.labels[k].name, .record = first + code.labels[k].record, .name = BUSTER_CHECK_NONE };
            plan->inline_label_records[k] = first + code.labels[k].record;
            plan->inline_label_names[k] = BUSTER_CHECK_NONE;
        }
        plan->label_count = valid ? code.label_count : 0;
        plan->inline_records = records;
        plan->inline_count = records_count;
        // An asm goto's exit number in eax, its call row's result.
        plan->inline_touched = inline_touched | (labels != 0 ? 1u << X86_REGISTER_RAX : 0);
        plan->inline_masks = inline_masks;
        plan->inline_leaves = returns;
        for (uint32_t k = 0; k < count; k += 1)
        {
            plan->inline_stack |= check_asm_stack(places + k) && check_asm_loads(places + k);
        }
        for (uint32_t r = 0; r < code.count; r += 1)
        {
            plan->inline_leaves |= check_asm_return(code.records[r].form);
        }
        plan->inlined = valid;
    }
    else if (valid && reason == NULL && marked)
    {
        reason = "inline assembly labels in templates of no instructions";
    }
    else if (valid && reason == NULL && helped && stack_named)
    {
        // A helper's rsp is its own: rsp's variable is an asm body's alone (#1846).
        reason = "explicit register variables in rsp in inline assembly that keeps a helper";
    }
    else if (valid && reason == NULL && helped && returns)
    {
        reason = "inline assembly templates that return from their helper";
    }
    else if (valid && reason == NULL && helped)
    {
        // The callee-saved registers the statement touches: its clobbers', its operands' and the stores'.
        uint32_t first = 0;
        const uint32_t labels = statement->is_goto ? statement->label_count : 0;
        helper->code = check_asm_helper_code(places, count, &code, taken & BUSTER_CHECK_ASM_CALLEE_SAVED, stores, block != 0, pops, gotos, goto_count, labels, exit,
                                             &first, output);
        valid = helper->code.error == TIER0_OK;
        // The references' names (but the asm goto's jumps', which are branches now), and the labels at the helper's records.
        const uint32_t names = code.reference_count - goto_count;
        plan->names = valid && names != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, String8, names) : 0;
        plan->labels = valid && code.label_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramAsmLabel, code.label_count) : 0;
        valid = valid && (names == 0 || plan->names != 0) && (code.label_count == 0 || plan->labels != 0);
        for (uint32_t r = 0, kept = 0; valid && r < code.reference_count; r += 1)
        {
            bool jumping = false;
            for (uint32_t j = 0; j < goto_count; j += 1)
            {
                jumping |= gotos[j].record == code.references[r].record;
            }
            if (!jumping)
            {
                plan->names[kept++] = code.references[r].name;
            }
        }
        for (uint32_t k = 0; valid && k < code.label_count; k += 1)
        {
            plan->labels[k] = (CheckProgramAsmLabel){ .spelling = code.labels[k].name, .record = first + code.labels[k].record, .name = BUSTER_CHECK_NONE };
        }
        plan->label_count = valid ? code.label_count : 0;
        helper->id = CBUILTIN_NONE;
        helper->assembly = s + 1;
        helper->parameter_count = block != 0 ? 1 : 0;
        helper->parameter_types[0] = block != 0 ? BUSTER_TYPE_ULONG : BUSTER_TYPE_VOID;
        helper->parameter_types[1] = BUSTER_TYPE_VOID;
        helper->parameter_types[2] = BUSTER_TYPE_VOID;
        // An asm goto's helper returns its exit (check_asm_helper_code), which its statement's code dispatches on.
        helper->result_type = labels != 0 ? BUSTER_TYPE_INT : BUSTER_TYPE_VOID;
        helper->compiler_barrier = true;
        plan->helper = 0;
    }
    plan->places = places;
    plan->block = block;
    plan->reason = reason;
    if (!valid)
    {
        arena_pop_to(output, output_mark);
        *plan = (CheckProgramAsm){ 0 };
    }
    else if (reason != NULL)
    {
        // A refusal keeps nothing but itself.
        arena_pop_to(output, output_mark);
        plan->places = 0;
        plan->names = 0;
        plan->labels = 0;
        plan->unit = (AsmResult){ 0 };
        plan->unit_assembled = false;
        plan->label_count = 0;
        plan->row = 0;
        plan->inline_records = 0;
        plan->inline_count = 0;
        plan->inline_label_records = 0;
        plan->inline_label_names = 0;
        plan->inlined = false;
        plan->inline_branches = 0;
        plan->inline_targets = 0;
        plan->inline_branch_count = 0;
    }
    arena_pop_to(scratch, scratch_mark);
    return valid;
}

// Whether body `b` is a naked function's: some declaration of its function up to its definition has the attribute
// (CheckProgram.object_naked), as clang takes it; clang ignores one after the definition, with a warning.
static bool check_program_asm_naked(const CheckProgram* program, uint32_t b)
{
    const CheckProgramBody body = program->bodies[b];
    return program->object_naked != 0 && body.object < program->object_count && program->object_naked[body.object] <= body.declaration;
}

// The symbol a naked function's operand names (#1845): a static object's or a function's of the unit, by its asm label
// or else its identifier, as clang prints it; an empty name for one whose name the emission makes (a block-scope
// static's, a literal's or a compound literal's), which stays refused.
static String8 check_asm_naked_symbol(const CheckProgram* program, uint32_t object)
{
    const CheckInput* input = program->input;
    const uint32_t declaration = object != 0 && object < program->object_count ? program->object_declarations[object] : BUSTER_CHECK_NONE;
    const bool named = declaration < input->token_count && check_input_linkage(input->declaration_bits, declaration) != 0 &&
                       !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_COMPOUND) &&
                       !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_REGISTER) && program->parser->tokens.names[declaration] != 0;
    const String8 label = named ? check_program_alias_label(input, declaration) : (String8){ 0 };
    const uint32_t identifier = named ? program->parser->tokens.names[declaration] : 0;
    return !named ? (String8){ 0 } : label.length != 0 ? label :
           (String8){ .pointer = (char*)intern_bytes(program->identifiers, identifier), .length = intern_length(program->identifiers, identifier) };
}

// Plans naked body `b` (CheckProgramNaked): its statements, each an asm statement whose operands are integer constants
// (anything else is refused by name, as clang refuses statements other than asm statements), are its whole code: their
// templates, substituted, assembled together in order, the syntax AT&T's again at each statement's start, as clang's
// integrated assembler parses each statement afresh; without a prologue, an epilogue or a trap of the compiler's. Their
// plans have no helper. Its one entry, the lowering's return of a function it doesn't compile, is the code's start. False
// only for an arena without room or an inconsistent input, with both arenas then as they were.
// One unsigned LEB128 of a frame description at `*at`, below `end`; `*ok` false past it.
static uint64_t check_asm_frame_uleb(const uint8_t* bytes, uint64_t end, uint64_t* at, bool* ok)
{
    uint64_t value = 0;
    uint32_t shift = 0;
    bool more = true;
    while (*ok && more)
    {
        *ok = *at < end && shift < 64;
        const uint8_t byte = *ok ? bytes[*at] : 0;
        value |= *ok ? (uint64_t)(byte & 0x7f) << shift : 0;
        shift += 7;
        *at += *ok ? 1 : 0;
        more = (byte & 0x80) != 0;
    }
    return value;
}

// A naked function's CFI (#1848), as clang splices a naked function's template into the FDE of its implicit
// `.cfi_startproc`, with no prologue rows: the rows of the frame description the assembler wrote for its unit, which
// check_program_asm_plan_naked assembled between `.cfi_startproc` and `.cfi_endproc`, decoded from its .eh_frame atom
// into the emission's frame rows (`rows`, `*count` of them, in `output`). Its CIE must be the one every function's has
// (code alignment 1, data alignment -8, the return address in 16, CFA rsp + 8), and its one FDE's instructions those a
// row program writes; anything else is refused by name (the returned reason).
static const char* check_asm_naked_frame_rows(const AsmResult* unit, Arena* output, EmissionFrameRow** rows, uint32_t* count)
{
    static const uint8_t cie[] = { 1, 'z', 'R', 0, 1, 0x78, 16, 1, 0x1b, 0x0c, 7, 8, 0x90, 1 };
    const char* reason = NULL;
    const uint8_t* bytes = NULL;
    uint64_t size = 0;
    for (uint32_t a = 1; a < unit->link.atoms.count; a += 1)
    {
        reason = unit->link.atoms.class[a] == BUSTER_LINK_CLASS_EH_FRAME && bytes != NULL ? "inline assembly CFI of several frame descriptions in a naked function" : reason;
        bytes = unit->link.atoms.class[a] == BUSTER_LINK_CLASS_EH_FRAME ? unit->link.atoms.bytes[a] : bytes;
        size = unit->link.atoms.class[a] == BUSTER_LINK_CLASS_EH_FRAME ? unit->link.atoms.size[a] : size;
    }
    *rows = NULL;
    *count = 0;
    uint32_t cie_length = 0;
    if (bytes != NULL && size >= 8)
    {
        memcpy(&cie_length, bytes, 4);
    }
    const uint64_t fde = (uint64_t)cie_length + 4;
    bool ok = bytes != NULL && reason == NULL && cie_length >= sizeof(cie) + 4 && fde + 24 <= size && memcmp(bytes + 8, cie, sizeof(cie)) == 0;
    for (uint64_t at = 8 + sizeof(cie); ok && at < fde; at += 1)
    {
        ok = bytes[at] == 0;
    }
    uint32_t fde_length = 0;
    if (ok)
    {
        memcpy(&fde_length, bytes + fde, 4);
    }
    const uint64_t end = fde + 4 + fde_length;
    ok = ok && end <= size && fde_length >= 16 && bytes[fde + 16] == 0;
    for (uint64_t at = end; ok && at < size; at += 1)
    {
        ok = bytes[at] == 0;
    }
    reason = reason != NULL ? reason : !ok ? "inline assembly CFI of a naked function whose frame description isn't a function's" : NULL;
    // At most an instruction a row each, in the output arena.
    EmissionFrameRow* list = reason == NULL ? BUSTER_ARENA_PUSH_ARRAY(output, EmissionFrameRow, end - fde) : NULL;
    reason = reason == NULL && list == NULL ? "arena exhausted decoding a naked function's CFI" : reason;
    uint64_t at = fde + 17;
    uint32_t offset = 0;
    uint32_t written = 0;
    while (reason == NULL && at < end)
    {
        const uint8_t op = bytes[at++];
        const uint8_t high = op & 0xc0;
        const uint8_t low = op & 0x3f;
        EmissionFrameRow row = { .offset = offset, .kind = UINT8_MAX };
        if (high == 0x40)
        {
            offset += low;
        }
        else if (high == 0x80)
        {
            row = (EmissionFrameRow){ .offset = offset, .kind = EMISSION_FRAME_OFFSET, .reg = low, .value = -8 * (int64_t)check_asm_frame_uleb(bytes, end, &at, &ok) };
        }
        else if (high == 0xc0)
        {
            row = (EmissionFrameRow){ .offset = offset, .kind = EMISSION_FRAME_RESTORE, .reg = low };
        }
        else if (op == 0x02 || op == 0x03 || op == 0x04)
        {
            const uint32_t width = op == 0x02 ? 1 : op == 0x03 ? 2 : 4;
            uint32_t delta = 0;
            ok = at + width <= end;
            if (ok)
            {
                memcpy(&delta, bytes + at, width);
            }
            at += width;
            offset += delta;
        }
        else if (op == 0x0c || op == 0x0d || op == 0x0e)
        {
            const uint64_t first = check_asm_frame_uleb(bytes, end, &at, &ok);
            const uint64_t second = op == 0x0c ? check_asm_frame_uleb(bytes, end, &at, &ok) : 0;
            row = op == 0x0e ? (EmissionFrameRow){ .offset = offset, .kind = EMISSION_FRAME_CFA_OFFSET, .value = (int64_t)first } :
                  (EmissionFrameRow){ .offset = offset, .kind = op == 0x0c ? EMISSION_FRAME_CFA : EMISSION_FRAME_CFA, .reg = (uint16_t)first, .value = (int64_t)second };
            // def_cfa_register keeps the offset: the rows' CFA carries it, from the last CFA row's.
            for (uint32_t r = written; op == 0x0d && r != 0; r -= 1)
            {
                const EmissionFrameRow* previous = list + r - 1;
                row.value = previous->kind == EMISSION_FRAME_CFA || previous->kind == EMISSION_FRAME_CFA_OFFSET ? previous->value : row.value;
                r = previous->kind == EMISSION_FRAME_CFA || previous->kind == EMISSION_FRAME_CFA_OFFSET ? 1 : r;
            }
            row.value = op == 0x0d && written == 0 ? 8 : row.value;
        }
        else if (op == 0x05 || op == 0x06 || op == 0x07 || op == 0x08 || op == 0x09)
        {
            const uint64_t reg = check_asm_frame_uleb(bytes, end, &at, &ok);
            const uint64_t second = op == 0x05 || op == 0x09 ? check_asm_frame_uleb(bytes, end, &at, &ok) : 0;
            const uint8_t kind = op == 0x05 ? EMISSION_FRAME_OFFSET : op == 0x06 ? EMISSION_FRAME_RESTORE : op == 0x07 ? EMISSION_FRAME_UNDEFINED :
                                 op == 0x08 ? EMISSION_FRAME_SAME_VALUE : EMISSION_FRAME_REGISTER;
            row = (EmissionFrameRow){ .offset = offset, .kind = kind, .reg = (uint16_t)reg, .value = op == 0x05 ? -8 * (int64_t)second : (int64_t)second };
            ok = ok && reg <= UINT16_MAX;
        }
        else if (op == 0x0a || op == 0x0b)
        {
            row = (EmissionFrameRow){ .offset = offset, .kind = op == 0x0a ? EMISSION_FRAME_REMEMBER_STATE : EMISSION_FRAME_RESTORE_STATE };
        }
        else if (op != 0)
        {
            reason = "inline assembly CFI of a naked function beyond a frame's rows (expressions, escapes or signed offsets)";
        }
        reason = reason == NULL && !ok ? "inline assembly CFI of a naked function whose frame description isn't a function's" : reason;
        if (reason == NULL && row.kind != UINT8_MAX)
        {
            list[written++] = row;
        }
    }
    *rows = reason == NULL ? list : NULL;
    *count = reason == NULL ? written : 0;
    return reason;
}

static bool check_program_asm_plan_naked(CheckProgramAsms* result, const CheckProgram* program, uint32_t b, const CheckAsmCheckedOperand* operands,
                                         const uint32_t* firsts, const X86MatchIndex* index, bool scalar, Arena* output, Arena* scratch)
{
    (void)scalar;

    static const char att[] = ".att_syntax prefix\n";
    const CheckInput* input = program->input;
    const CheckProgramBody body = program->bodies[b];
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    CheckProgramNaked* naked = result->naked + b;
    *naked = (CheckProgramNaked){ .planned = true };
    String8* texts = BUSTER_ARENA_PUSH_ARRAY(scratch, String8, (uint64_t)body.count + 1);
    bool valid = texts != 0;
    const char* reason = NULL;
    uint64_t length = 0;
    uint32_t statements = 0;
    for (uint32_t i = 0; valid && reason == NULL && i < body.count; i += 1)
    {
        const CheckControl control = program->controls[body.first + i];
        const uint32_t s = control.target;
        if (control.kind == CHECK_CONTROL_END)
        {
            // The body's closing brace.
        }
        else if (control.kind != CHECK_CONTROL_ASM)
        {
            reason = "statements other than asm statements in naked functions";
        }
        else if (s >= input->asm_count || result->plans == 0 || result->plans[s].planned)
        {
            result->status = CHECK_PROGRAM_INVALID;
            result->token = control.token;
            valid = false;
        }
        else
        {
            const CheckInputAsm* statement = input->asm_statements + s;
            const uint32_t count = statement->output_count + statement->input_count;
            CheckAsmPlace* places = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckAsmPlace, count) : 0;
            valid = count == 0 || places != 0;
            result->plans[s] = (CheckProgramAsm){ .helper = BUSTER_CHECK_NONE, .planned = true };
            reason = statement->is_goto ? "asm goto" : count > BUSTER_CHECK_ASM_OPERANDS ? "inline assembly statements of more than 64 operands" : NULL;
            for (uint32_t k = 0; valid && reason == NULL && k < count; k += 1)
            {
                const bool output = k < statement->output_count;
                const CheckAsmConstraint constraint = check_asm_constraint_parse(statement, program->identifiers, statement->operands[k].constraint, output);
                int64_t constant = 0;
                uint32_t bits = 64;
                const bool immediate = !output && constraint.refused == NULL && constraint.immediate &&
                                       check_asm_integer_constant(program->types, operands + firsts[s] + k, &constant, &bits);
                // An address constant of a symbol ("i"(&g), "i"(f)) and a static object ("m"(g)), which clang prints by
                // its symbol's name (#1845).
                const CheckValue* value = &operands[firsts[s] + k].value;
                const bool clean = !output && constraint.refused == NULL && value->fault == CHECK_FAULT_NONE && (value->flags & BUSTER_CHECK_SIDE_EFFECT) == 0;
                const bool address = clean && !immediate && constraint.immediate && constraint.low == INT64_MIN && constraint.high == INT64_MAX &&
                                     value->constant_kind == CHECK_CONSTANT_ADDRESS;
                const bool object = clean && !immediate && !address && constraint.memory && value->category == CHECK_LVALUE &&
                                    (value->flags & BUSTER_CHECK_STATIC_OBJECT) != 0 && value->bit_width == 0;
                const String8 symbol = address || object ? check_asm_naked_symbol(program, value->object) : (String8){ 0 };
                const bool named = symbol.length != 0;
                places[k] = (CheckAsmPlace){ .constant = named ? value->offset : constant, .slot = BUSTER_CHECK_NONE, .value = BUSTER_CHECK_NONE, .tied = BUSTER_CHECK_NONE,
                                             .kind = !named ? BUSTER_CHECK_ASM_CONSTANT : address ? BUSTER_CHECK_ASM_SYMBOL : BUSTER_CHECK_ASM_SYMBOL_MEMORY,
                                             .symbol = symbol };
                reason = named ? NULL :
                         !immediate ? "naked functions' inline assembly operands other than integer constants or static objects' symbols" :
                         !check_asm_constant_fits(&constraint, constant, bits) ? "inline assembly constants outside their constraint's range" : NULL;
            }
            if (valid && reason == NULL)
            {
                reason = check_asm_substitute(statement, places, program->identifiers, s, scratch, texts + statements, &valid);
                length += texts[statements].length + 1 + (statements != 0 ? sizeof(att) - 1 : 0);
                statements += 1;
            }
        }
    }
    // The templates as one source, each on lines of its own. In the output arena, so that a unit's section names, whose
    // bytes are the source's, live as long as its link.
    char* source = valid && reason == NULL ? arena_push(output, length + 1, 1) : 0;
    valid = valid && (reason != NULL || source != 0);
    uint64_t at = 0;
    for (uint32_t i = 0; valid && reason == NULL && i < statements; i += 1)
    {
        if (i != 0)
        {
            memcpy(source + at, att, sizeof(att) - 1);
            at += sizeof(att) - 1;
        }
        memcpy(source + at, texts[i].pointer, texts[i].length);
        at += texts[i].length;
        source[at++] = '\n';
    }
    AsmCode code = { 0 };
    AsmResult unit = { 0 };
    bool unit_assembled = false;
    if (valid && reason == NULL)
    {
        code = asm_assemble_code((String8){ .pointer = source, .length = at }, index, (AsmOptions){ .vectorized = !BUSTER_SCALAR_PATH(scalar), .integrated = true }, output, scratch);
        if (code.error != NULL && code.directive)
        {
            // The naked function's templates take directives: its whole code is a unit assembled by the full assembler.
            // Its CFI directives (#1848) are its function's frame, as clang's implicit `.cfi_startproc` makes them: the
            // source goes between `.cfi_startproc` and `.cfi_endproc`, and the frame description's rows become its
            // unwind entry's (check_asm_naked_frame_rows), the unit's .eh_frame left out.
            static const char start[] = ".cfi_startproc\n";
            static const char finish[] = ".cfi_endproc\n";
            bool framed = false;
            for (uint64_t i = 0; !framed && i + 5 <= at; i += 1)
            {
                framed = memcmp(source + i, ".cfi_", 5) == 0;
            }
            char* wrapped = framed ? arena_push(output, at + sizeof(start) + sizeof(finish), 1) : NULL;
            valid = !framed || wrapped != NULL;
            if (wrapped != NULL)
            {
                memcpy(wrapped, start, sizeof(start) - 1);
                memcpy(wrapped + sizeof(start) - 1, source, at);
                memcpy(wrapped + sizeof(start) - 1 + at, finish, sizeof(finish) - 1);
            }
            const String8 text = wrapped != NULL ? (String8){ .pointer = wrapped, .length = at + sizeof(start) + sizeof(finish) - 2 } : (String8){ .pointer = source, .length = at };
            unit = valid ? asm_assemble(text, index, (AsmOptions){ .vectorized = !BUSTER_SCALAR_PATH(scalar), .integrated = true }, output) : unit;
            unit_assembled = valid && unit.error == NULL;
            reason = valid && unit.error != NULL && strstr(unit.error, "arena exhausted") == NULL ? unit.error : NULL;
            valid = valid && (unit.error == NULL || reason != NULL);
            if (unit_assembled && framed)
            {
                reason = check_asm_naked_frame_rows(&unit, output, &naked->frame_rows, &naked->frame_row_count);
                naked->framed = reason == NULL;
            }
            for (uint32_t a = 1; unit_assembled && !framed && reason == NULL && a < unit.link.atoms.count; a += 1)
            {
                reason = unit.link.atoms.class[a] == BUSTER_LINK_CLASS_EH_FRAME ? "inline assembly's CFI directives" : NULL;
            }
        }
        else
        {
            reason = code.error != NULL && strstr(code.error, "arena exhausted") == NULL ? code.error : NULL;
            valid = code.error == NULL || reason != NULL;
        }
    }
    if (valid && reason == NULL && unit_assembled)
    {
        naked->unit = unit;
        naked->unit_assembled = true;
    }
    else if (valid && reason == NULL)
    {
        LinkReference* references = code.reference_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, LinkReference, code.reference_count) : 0;
        uint32_t* entries = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, 2);
        naked->names = code.reference_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, String8, code.reference_count) : 0;
        naked->labels = code.label_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramAsmLabel, code.label_count) : 0;
        valid = entries != 0 && (code.reference_count == 0 || (references != 0 && naked->names != 0)) && (code.label_count == 0 || naked->labels != 0);
        for (uint32_t r = 0; valid && r < code.reference_count; r += 1)
        {
            const AsmCodeReference* reference = code.references + r;
            references[r] = (LinkReference){ .instruction = reference->record, .field = reference->field, .kind = reference->kind, .addend = reference->addend };
            naked->names[r] = reference->name;
        }
        for (uint32_t k = 0; valid && k < code.label_count; k += 1)
        {
            naked->labels[k] = (CheckProgramAsmLabel){ .spelling = code.labels[k].name, .record = code.labels[k].record, .name = BUSTER_CHECK_NONE };
        }
        if (valid)
        {
            // Its one entry's first record, and the epilogue's after it (Tier0Code.entry_records), which a naked function
            // has none of: its code's end.
            entries[0] = 0;
            entries[1] = code.count;
            naked->label_count = code.label_count;
            naked->code = (Tier0Code){
                .records = code.records,
                .count = code.count,
                .branches = code.branches,
                .targets = code.targets,
                .branch_count = code.branch_count,
                .references = references,
                .reference_count = code.reference_count,
                .entry_records = entries,
                .entry_count = 1,
                .error = TIER0_OK,
            };
        }
    }
    if (!valid)
    {
        arena_pop_to(output, output_mark);
        *naked = (CheckProgramNaked){ 0 };
    }
    else if (reason != NULL)
    {
        // A refusal keeps nothing but itself.
        arena_pop_to(output, output_mark);
        *naked = (CheckProgramNaked){ .reason = reason, .planned = true };
    }
    arena_pop_to(scratch, scratch_mark);
    return valid;
}

// The entry of `spelling` in the names' table (`table`, `mask` + 1 slots of an index plus one, or 0 for none), added to
// `names` if it isn't there, with `flags`' binding (the last directive's, as GNU as's). `label` marks a definition. With
// `upgrade`, an existing name takes a non-local binding too: a reference's default global doesn't make a label of another
// template's global.
static uint32_t check_asm_name(CheckAsmName* names, uint32_t* count, uint32_t* table, uint32_t mask, String8 spelling, uint8_t flags, bool upgrade)
{
    uint32_t slot = (uint32_t)link_hash((const uint8_t*)spelling.pointer, (uint32_t)spelling.length) & mask;
    while (table[slot] != 0 && (names[table[slot] - 1].spelling.length != spelling.length ||
                                memcmp(names[table[slot] - 1].spelling.pointer, spelling.pointer, spelling.length) != 0))
    {
        slot = (slot + 1) & mask;
    }
    if (table[slot] == 0)
    {
        names[*count] = (CheckAsmName){ .spelling = spelling, .flags = flags };
        *count += 1;
        table[slot] = *count;
    }
    else
    {
        CheckAsmName* name = &names[table[slot] - 1];
        if (upgrade && (flags & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL)
        {
            name->flags = (name->flags & ~BUSTER_LINK_SYMBOL_BINDING) | (flags & BUSTER_LINK_SYMBOL_BINDING);
        }
        // A non-default visibility is a directive's, and stays with the name even where its binding doesn't upgrade the
        // name: `.hidden puts` makes the undefined reference hidden, as clang's assembler does.
        if ((flags & BUSTER_LINK_SYMBOL_VISIBILITY) != BUSTER_LINK_SYMBOL_DEFAULT)
        {
            name->flags = (name->flags & ~BUSTER_LINK_SYMBOL_VISIBILITY) | (flags & BUSTER_LINK_SYMBOL_VISIBILITY);
        }
    }
    return table[slot] - 1;
}

// `symbol`'s name in `unit`, as the source spells it; an empty string for one with none.
static String8 check_asm_unit_name(const AsmResult* unit, uint32_t symbol)
{
    const NameId name = unit->link.symbols.name[symbol];
    return name != 0 && name < unit->link.names.count ? (String8){ .pointer = (char*)unit->link.names.bytes[name], .length = unit->link.names.length[name] } :
                                                         (String8){ 0 };
}

// Gives every name the planned templates define or refer to its entry in `result->names`, once: each label its index,
// and each reference's target its index (the emission makes it the name's symbol's). The labels come first, so that a
// reference finds a label wherever its template is. A label two templates define, and a `.L` name (an assembler-local
// one) no template defines, refuse the statement or the naked function that names it, as GNU as and clang reject them.
// A template that takes directives contributes its assembled unit's symbols; the record path's labels and references
// keep their record indices. The scan is scalar, over a unit's few labels and references: main.c's units have 6. False
// only for an arena without room.
static bool check_program_asm_names(CheckProgramAsms* result, const CheckProgramHelpers* helpers, const CheckProgram* program, Arena* output, Arena* scratch)
{
    const uint32_t statements = result->plans != 0 ? program->input->asm_count : 0;
    const uint32_t bodies = result->naked != 0 ? program->body_count : 0;
    uint64_t total = 0;
    for (uint32_t s = 0; s < statements; s += 1)
    {
        const CheckProgramAsm* plan = result->plans + s;
        total += plan->planned && plan->reason == NULL ? plan->label_count +
                                                             (plan->unit_assembled ? plan->unit.link.symbols.count + (uint64_t)plan->unit.absolute_count :
                                                                                     plan->helper != BUSTER_CHECK_NONE ?
                                                                                                                               helpers->items[plan->helper].code.reference_count :
                                                                                                                               0) :
                                                         0;
    }
    for (uint32_t b = 0; b < bodies; b += 1)
    {
        const CheckProgramNaked* naked = result->naked + b;
        total += naked->planned && naked->reason == NULL ? naked->label_count +
                                                               (naked->unit_assembled ? naked->unit.link.symbols.count + (uint64_t)naked->unit.absolute_count :
                                                                                        naked->code.reference_count) :
                                                           0;
    }
    // The file-scope assembly (check_program_asm_plan_file), a code after the naked functions'.
    const AsmResult* file = result->file_assembled ? &result->file_unit : 0;
    total += file != 0 ? file->link.symbols.count + (uint64_t)file->absolute_count : 0;
    uint32_t slots = 2;
    while (slots < 2 * total && slots < (UINT32_C(1) << 31))
    {
        slots *= 2;
    }
    const uint64_t scratch_mark = scratch->length;
    uint32_t* table = total != 0 && total < (UINT32_C(1) << 30) ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots) : 0;
    CheckAsmName* names = table != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckAsmName, total) : 0;
    const bool valid = total == 0 || (table != 0 && names != 0);
    uint32_t count = 0;
    if (valid && total != 0)
    {
        memset(table, 0, (uint64_t)slots * sizeof(uint32_t));
        // The templates' absolute (`.set`) symbols, once each: they are the template's alone, so another template's
        // reference to one is refused below by name rather than becoming an undefined global at link time.
        for (uint32_t c = 0; c < statements + bodies + 1; c += 1)
        {
            CheckProgramAsm* plan = c < statements ? result->plans + c : 0;
            CheckProgramNaked* naked = c >= statements && c < statements + bodies ? result->naked + (c - statements) : 0;
            const bool planned = plan != 0 ? plan->planned && plan->reason == NULL : naked != 0 ? naked->planned && naked->reason == NULL : file != 0;
            const AsmResult* unit = !planned ? 0 : plan != 0 ? (plan->unit_assembled ? &plan->unit : 0) : naked != 0 ? (naked->unit_assembled ? &naked->unit : 0) : file;
            for (uint32_t a = 0; unit != 0 && a < unit->absolute_count; a += 1)
            {
                const uint32_t name = check_asm_name(names, &count, table, slots - 1, unit->absolutes[a].name, BUSTER_LINK_SYMBOL_LOCAL, true);
                // `.set`/`.equ` may reassign, and two templates' definitions of one name are each their own: a label of
                // that name is refused when labels are added below.
                names[name].label = true;
                names[name].absolute = true;
            }
        }
        // Each pass over the codes: the statements' helpers', then the naked functions'. A unit's labels (its defined
        // symbols) are its pass 0's, and its other symbols its pass 1's.
        for (uint32_t pass = 0; pass < 2; pass += 1)
        {
            for (uint32_t c = 0; c < statements + bodies + 1; c += 1)
            {
                CheckProgramAsm* plan = c < statements ? result->plans + c : 0;
                CheckProgramNaked* naked = c >= statements && c < statements + bodies ? result->naked + (c - statements) : 0;
                // An asm body's labels too (#1839), which has no code of its own: its references keep a helper.
                const bool planned = plan != 0   ? plan->planned && plan->reason == NULL && (plan->helper != BUSTER_CHECK_NONE || plan->unit_assembled || plan->inlined) :
                                     naked != 0 ? naked->planned && naked->reason == NULL :
                                                  file != 0;
                const AsmResult* unit = !planned ? 0 : plan != 0 ? (plan->unit_assembled ? &plan->unit : 0) : naked != 0 ? (naked->unit_assembled ? &naked->unit : 0) : file;
                const Tier0Code* code = !planned || unit != 0 || (plan != 0 && plan->inlined) ? 0 : plan != 0 ? &helpers->items[plan->helper].code : &naked->code;
                CheckProgramAsmLabel* labels = plan != 0 ? plan->labels : naked != 0 ? naked->labels : 0;
                const uint32_t label_count = !planned || unit != 0 ? 0 : plan != 0 ? plan->label_count : naked->label_count;
                const String8* spellings = plan != 0 ? plan->names : naked != 0 ? naked->names : 0;
                const char** reason = plan != 0 ? &plan->reason : naked != 0 ? &naked->reason : &result->file_reason;
                for (uint32_t k = 0; pass == 0 && k < label_count; k += 1)
                {
                    const uint32_t name = check_asm_name(names, &count, table, slots - 1, labels[k].spelling, BUSTER_LINK_SYMBOL_LOCAL, true);
                    *reason = names[name].label ? "inline assembly labels another template defines too" : *reason;
                    names[name].label = true;
                    labels[k].name = name;
                    if (plan != 0 && plan->inlined)
                    {
                        plan->inline_label_names[k] = name;
                    }
                }
                for (uint32_t r = 0; pass == 1 && code != 0 && r < code->reference_count; r += 1)
                {
                    const String8 spelling = spellings[r];
                    const uint32_t name = check_asm_name(names, &count, table, slots - 1, spelling, BUSTER_LINK_SYMBOL_GLOBAL, false);
                    const bool local = spelling.length >= 2 && spelling.pointer[0] == '.' && spelling.pointer[1] == 'L';
                    *reason = names[name].absolute ? "inline assembly absolute symbols are per-template" :
                              local && !names[name].label ? "inline assembly's local labels that no template defines" :
                                                                                         *reason;
                    names[name].referenced = true;
                    code->references[r].target = name;
                }
                for (uint32_t u = 1; unit != 0 && u < unit->link.symbols.count; u += 1)
                {
                    const String8 spelling = check_asm_unit_name(unit, u);
                    const bool defined = unit->link.symbols.atom[u] != 0;
                    if (spelling.length == 0 || defined != (pass == 0))
                    {
                        continue;
                    }
                    const uint8_t flags = unit->link.symbols.flags[u] & (BUSTER_LINK_SYMBOL_TYPE | BUSTER_LINK_SYMBOL_BINDING | BUSTER_LINK_SYMBOL_VISIBILITY);
                    const uint32_t name = check_asm_name(names, &count, table, slots - 1, spelling, flags,
                                                         defined || (flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK);
                    if (unit->directives != 0)
                    {
                        names[name].directives |= unit->directives[u];
                        if ((unit->directives[u] & BUSTER_ASM_DIRECTIVE_BINDING) != 0)
                        {
                            // A binding directive is the template's, even when a reference to the name was seen first.
                            names[name].flags = (uint8_t)((names[name].flags & ~BUSTER_LINK_SYMBOL_BINDING) | (flags & BUSTER_LINK_SYMBOL_BINDING));
                        }
                        if ((unit->directives[u] & BUSTER_ASM_DIRECTIVE_SIZE) != 0)
                        {
                            names[name].size = unit->link.symbols.size[u];
                        }
                    }
                    if (defined)
                    {
                        *reason = names[name].label ? "inline assembly labels another template defines too" : *reason;
                        names[name].label = true;
                        names[name].file = unit == file;
                    }
                    else
                    {
                        const bool local = spelling.length >= 2 && spelling.pointer[0] == '.' && spelling.pointer[1] == 'L';
                        *reason = names[name].absolute ? "inline assembly absolute symbols are per-template" :
                                  local && !names[name].label ? "inline assembly's local labels that no template defines" :
                                                                                             *reason;
                        // An undefined global a fixup of this unit refers to is a reference; one no fixup refers to is
                        // a declaration, as `.globl sym2` alone. A unit's few symbols and fixups make the scan free.
                        for (uint32_t f = 0; f < unit->link.fixups.count; f += 1)
                        {
                            names[name].referenced |= unit->link.fixups.target[f] == u;
                        }
                    }
                }
            }
        }
    }
    result->names = names;
    result->name_count = count;
    arena_pop_to(scratch, scratch_mark);
    return valid;
}

// The unit's file-scope assembly (#619), as clang emits a module's: its statements' templates in source order, a line
// each, assembled by asm_assemble, the `assemble` command's assembler, into `result->file_unit`, in the output arena
// (its section names are the source's bytes). Its labels and references join the templates' names
// (check_program_asm_names), so that its labels define symbols C declares and its references reach C's symbols, and
// the emission publishes its atoms before the functions' (check_program_emit_asm_unit). Refused by name, which the
// emission reports at the first statement (`file_reason`, `file_token`): a template the assembler doesn't take. Its
// section groups (COMDAT, #1848) are the emission's too (check_program_emit_asm_groups), in -c's object as clang's, and
// selected among the units of a link of several (check_units_keep).
// False only for an arena without room.
static bool check_program_asm_plan_file(CheckProgramAsms* result, const CheckProgram* program, const X86MatchIndex* index, bool scalar, Arena* output)
{
    const CheckInput* input = program->input;
    uint64_t length = 0;
    uint32_t first = BUSTER_CHECK_NONE;
    for (uint32_t i = 0; i < input->asm_count; i += 1)
    {
        const CheckInputAsm* statement = input->asm_statements + i;
        length += statement->body == BUSTER_CHECK_NONE ? (uint64_t)statement->template_text.length + 1 : 0;
        first = statement->body == BUSTER_CHECK_NONE && first == BUSTER_CHECK_NONE ? statement->token : first;
    }
    char* source = first != BUSTER_CHECK_NONE && length < UINT32_MAX ? arena_push(output, length, 1) : 0;
    const bool valid = first == BUSTER_CHECK_NONE || source != 0;
    uint64_t at = 0;
    for (uint32_t i = 0; source != 0 && i < input->asm_count; i += 1)
    {
        const CheckInputAsm* statement = input->asm_statements + i;
        if (statement->body == BUSTER_CHECK_NONE)
        {
            memcpy(source + at, statement->template_text.bytes, statement->template_text.length);
            at += statement->template_text.length;
            source[at] = '\n';
            at += 1;
        }
    }
    const AsmResult assembled = source != 0 ? asm_assemble((String8){ .pointer = source, .length = length }, index, (AsmOptions){ .vectorized = !scalar, .integrated = true }, output) :
                                              (AsmResult){ 0 };
    const char* refused = source == 0 ? NULL : assembled.error != 0 ? "file-scope assembly the assembler doesn't take" : NULL;
    // Every section is published as the assembler made it: notes, the arrays of constructors and destructors in their
    // classes, merged strings, custom flags, a group's sections (COMDAT), and CFI's frames, an EH_FRAME atom of their own
    // beside the functions' (#1848).
    result->file_unit = refused == NULL ? assembled : (AsmResult){ 0 };
    result->file_assembled = source != 0 && refused == NULL;
    result->file_reason = refused;
    result->file_token = first;
    return valid;
}

// Plans every asm statement of the program's bodies (CheckProgramAsms), appending a helper for each whose template
// assembles to instructions to `helpers`, after the builtin ones, with the next symbols, and every naked function's code
// (whatever its statements); then the names the templates define and refer to. False for an arena without room or an
// inconsistent input; the refusals are the plans' own, which the lowering reports in its order.
static bool check_program_asm_plan(CheckProgramAsms* result, CheckProgramHelpers* helpers, const CheckProgram* program, bool scalar, Arena* output, Arena* scratch)
{
    (void)scalar;

    *result = (CheckProgramAsms){ .status = CHECK_PROGRAM_OK, .token = BUSTER_CHECK_NONE };
    const CheckInput* input = program->input;
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    // The helpers as they came, restored on failure: a later step without room (the names') must not leave the
    // statements' helpers appended to them in the popped output.
    const CheckProgramHelpers helpers_mark = *helpers;
    // A body's controls: its statements' (entry_count of them), then its statement expressions' (to count). A unit
    // without asm statements, the usual one, has none to walk for.
    uint32_t statements = 0;
    for (uint32_t b = 0; input->asm_count != 0 && b < program->body_count; b += 1)
    {
        for (uint32_t i = 0; i < program->bodies[b].count; i += 1)
        {
            statements += program->controls[program->bodies[b].first + i].kind == CHECK_CONTROL_ASM;
        }
    }
    // The naked functions: a pass over the bodies of a unit that declares one, which needs no asm statement to be one.
    uint32_t naked_count = 0;
    for (uint32_t b = 0; program->object_naked != 0 && b < program->body_count; b += 1)
    {
        naked_count += check_program_asm_naked(program, b);
    }
    const CheckAsms* asms = program->asms;
    bool valid = statements == 0 || (asms != 0 && asms->status == CHECK_ASM_OK && input->asm_count != 0);
    result->plans = valid && statements != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramAsm, input->asm_count) : 0;
    result->naked = valid && naked_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramNaked, program->body_count) : 0;
    uint32_t* firsts = valid && statements != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)input->asm_count + 1) : 0;
    CheckProgramHelper* added = valid && statements != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckProgramHelper, statements) : 0;
    // The file-scope statements, which have no body.
    uint32_t file_count = 0;
    for (uint32_t s = 0; s < input->asm_count; s += 1)
    {
        file_count += input->asm_statements[s].body == BUSTER_CHECK_NONE;
    }
    const X86MatchIndex* index = valid && (statements != 0 || naked_count != 0 || file_count != 0) ? check_asm_matches(scratch) : 0;
    uint32_t count = 0;
    if ((statements != 0 && (result->plans == 0 || firsts == 0 || added == 0 || index == 0)) || (naked_count != 0 && (result->naked == 0 || index == 0)) ||
        (file_count != 0 && index == 0))
    {
        result->status = valid ? CHECK_PROGRAM_RESOURCE : CHECK_PROGRAM_INVALID;
        valid = false;
    }
    if (valid && naked_count != 0)
    {
        memset(result->naked, 0, (uint64_t)program->body_count * sizeof(CheckProgramNaked));
    }
    if (valid && statements != 0)
    {
        memset(result->plans, 0, (uint64_t)input->asm_count * sizeof(CheckProgramAsm));
        uint64_t first = 0;
        for (uint32_t s = 0; s < input->asm_count; s += 1)
        {
            firsts[s] = (uint32_t)first;
            first += (uint64_t)input->asm_statements[s].output_count + input->asm_statements[s].input_count;
        }
        valid = first == asms->count;
        result->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_INVALID;
    }
    for (uint32_t b = 0; valid && naked_count != 0 && b < program->body_count; b += 1)
    {
        if (check_program_asm_naked(program, b))
        {
            valid = check_program_asm_plan_naked(result, program, b, statements != 0 ? asms->operands : 0, firsts, index, BUSTER_SCALAR_PATH(scalar), output, scratch);
            result->status = valid ? CHECK_PROGRAM_OK : result->status == CHECK_PROGRAM_OK ? CHECK_PROGRAM_RESOURCE : result->status;
        }
    }
    for (uint32_t b = 0; valid && statements != 0 && b < program->body_count; b += 1)
    {
        uint32_t rows = 0;
        uint32_t bodies = 0;
        // A naked body's statements are its code, which its plan has.
        for (uint32_t i = 0; valid && (result->naked == 0 || !result->naked[b].planned) && i < program->bodies[b].count; i += 1)
        {
            const CheckControl control = program->controls[program->bodies[b].first + i];
            const uint32_t s = control.target;
            if (control.kind == CHECK_CONTROL_ASM && (s >= input->asm_count || result->plans[s].planned))
            {
                result->status = CHECK_PROGRAM_INVALID;
                result->token = control.token;
                valid = false;
            }
            else if (control.kind == CHECK_CONTROL_ASM)
            {
                CheckProgramHelper* helper = added + count;
                *helper = (CheckProgramHelper){ 0 };
                // The body's instruction set, its function's target attributes' (#180), which an opmask operand's moves need.
                const uint32_t body_target = program->body_targets != 0 ? program->body_targets[b] : 0;
                const X86Target* instruction_set = body_target == 0 || body_target == BUSTER_CHECK_PROGRAM_TARGET_VERSIONS ? &program->target :
                                                   &program->targets[body_target - 1];
                valid = check_program_asm_plan_one(program, s, asms->operands + firsts[s], index, instruction_set, BUSTER_SCALAR_PATH(scalar), result->plans + s, helper, output,
                                                   scratch);
                if (valid && result->plans[s].helper != BUSTER_CHECK_NONE)
                {
                    result->plans[s].helper = helpers->count + count;
                    count += 1;
                }
                result->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_RESOURCE;
                result->token = valid ? BUSTER_CHECK_NONE : control.token;
                rows += valid && result->plans[s].row != NULL;
                bodies += valid && result->plans[s].inlined;
            }
        }
        result->row_capacity = rows > result->row_capacity ? rows : result->row_capacity;
        result->body_capacity = bodies > result->body_capacity ? bodies : result->body_capacity;
    }
    if (valid && count != 0)
    {
        CheckProgramHelper* items = (uint64_t)program->symbol_count + helpers->count + count < UINT32_MAX ?
                                    BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramHelper, (uint64_t)helpers->count + count) : 0;
        valid = items != 0;
        result->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_RESOURCE;
        if (valid)
        {
            if (helpers->count != 0)
            {
                memcpy(items, helpers->items, (uint64_t)helpers->count * sizeof(CheckProgramHelper));
            }
            memcpy(items + helpers->count, added, (uint64_t)count * sizeof(CheckProgramHelper));
            for (uint32_t k = 0; k < count; k += 1)
            {
                items[helpers->count + k].symbol = program->symbol_count + 1 + helpers->count + k;
            }
            helpers->items = items;
            helpers->count += count;
        }
    }
    if (valid && file_count != 0 && !check_program_asm_plan_file(result, program, index, BUSTER_SCALAR_PATH(scalar), output))
    {
        result->status = CHECK_PROGRAM_RESOURCE;
        valid = false;
    }
    if (valid && !check_program_asm_names(result, helpers, program, output, scratch))
    {
        result->status = CHECK_PROGRAM_RESOURCE;
        valid = false;
    }
    if (!valid)
    {
        const CheckProgramStatus status = result->status;
        const uint32_t token = result->token;
        arena_pop_to(output, output_mark);
        *helpers = helpers_mark;
        *result = (CheckProgramAsms){ .status = status == CHECK_PROGRAM_OK ? CHECK_PROGRAM_RESOURCE : status, .token = token };
    }
    arena_pop_to(scratch, scratch_mark);
    return valid;
}

#endif
