// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// C's inline assembly (#147, c_check_program_asm.c), lowered by both of the checker's paths and run natively against
// clang -O0's build of the same source (check_test_program_source's harness): every shape the lowering places, and
// each refusal by the feature it names. Include after c_check_program_test.c's harness.
#ifndef BUSTER_C_CHECK_PROGRAM_ASM_TEST_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_ASM_TEST_INCLUDED

#include <cpuid.h>

// The planner without room (#929's review): a statement of x87 operands and a template of no instructions, one of an
// x87 operand and an instruction, and one refused at its first operand (an __int128 in a general register) before its x87 operand, whose
// later places the planner never writes. check_program_asm_plan of them, by both paths, with the output arena's room cut
// to none and then 8 bytes more at a time until the plan succeeds: each run before it must fail for want of room
// (CHECK_PROGRAM_RESOURCE) with both arenas as they were, among them a run whose room ends before each statement's
// places; then the plans must be the whole arena's: an asm body each for the first two (#1839), the third refused by name,
// a helper for the fourth, whose form x86_semantics.c doesn't cover (rorx), and an asm body for the fifth, whose named label
// the names step adds (#2090: its run without room must leave the helpers as they were).
static void check_test_program_asm_room(CheckTest* test)
{
    static const char source[] =
        "float through(float x){__asm__ volatile(\"\" : \"+t\"(x));return x;}"
        "long double root(long double x){__asm__(\"fsqrt\" : \"+t\"(x));return x;}"
        "int flagged(int a, float x){__int128 z;__asm__(\"cmpl $0, %2\" : \"=r\"(z), \"+t\"(x) : \"r\"(a));return (int)z+(int)x;}"
        "int rotated(int n){int r;__asm__(\"rorxl $3, %1, %0\" : \"=r\"(r) : \"r\"(n));return r;}"
        "int counted(int n){__asm__(\".Lcount%=:\\n\\tdecl %0\\n\\tjnz .Lcount%=\" : \"+r\"(n));return n;}"
        "int jumped(int n){asm goto(\"testl %0, %0\\n\\tjz %l1\" : : \"r\"(n) : : zero);return 1;zero:return 0;}";
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const PredefinesOptions predefines = check_test_program_predefines();
    bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) &&
                        pp_run_source(&pp, "asm-room", (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "asm room: source preprocessing");
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 26);
        Arena output = arena_reserve(UINT64_C(1) << 26);
        Arena scratch = arena_reserve(UINT64_C(1) << 26);
        const ParseOptions parsing = { .scalar = path == 0 };
        const CheckOptions checking = { .scalar = path == 0 };
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers plans = { 0 };
        CheckAsms asms = { 0 };
        CheckProgram program = { 0 };
        bool ready = input_arena.base != 0 && output.base != 0 && scratch.base != 0 && check_test_types_open(&types) &&
                     parse_initialize(&parser, &parsing, pp_options.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0 &&
                     check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
                     check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &checking, &stream) && stream.report_count == 0 &&
                     check_initializers_run(&plans, &input, &stream, &parser, &types.table, &input_arena, &scratch) &&
                     check_asms_run(&asms, &input, &stream, &types.table, &input_arena, &scratch);
        const CheckProgramOptions options = { .initializers = &plans, .asms = &asms, .identifiers = &identifiers, .scalar = path == 0, .predefines = &predefines };
        ready = ready && check_program(&program, &parser, &input, &stream, &types.table, options, &output, &scratch) && input.asm_count == 6;
        char description[256];
        snprintf(description, sizeof(description), "asm room, %s: the program is ready", path == 0 ? "scalar" : "vector");
        check_test_expect(test, ready, description);
        const uint64_t output_mark = output.length;
        const uint64_t scratch_mark = scratch.length;
        const uint64_t capacity = output.capacity;
        CheckProgramAsms planned = { 0 };
        CheckProgramHelpers helpers = { 0 };
        bool done = false;
        bool kept = true;
        uint32_t failures = 0;
        for (uint64_t room = 0; ready && !done && room <= (UINT64_C(1) << 20); room += 8)
        {
            helpers = (CheckProgramHelpers){ 0 };
            output.capacity = output_mark + room;
            done = check_program_asm_plan(&planned, &helpers, &program, path == 0, &output, &scratch);
            output.capacity = capacity;
            failures += !done;
            kept &= done || (planned.status == CHECK_PROGRAM_RESOURCE && output.length == output_mark && scratch.length == scratch_mark && helpers.count == 0);
        }
        snprintf(description, sizeof(description), "asm room, %s: %u runs without room fail with the arenas as they were, then one plans", path == 0 ? "scalar" : "vector",
                 failures);
        check_test_expect(test, ready && done && kept && failures != 0, description);
        const bool plans_right = done && planned.plans != NULL && planned.plans[0].reason == NULL && planned.plans[0].inlined &&
                                 planned.plans[0].helper == BUSTER_CHECK_NONE && planned.plans[1].reason == NULL && planned.plans[1].inlined &&
                                 planned.plans[1].helper == BUSTER_CHECK_NONE && planned.plans[2].reason != NULL &&
                                 strcmp(planned.plans[2].reason, "inline assembly operands of this type in their constraint's registers") == 0 && planned.plans[3].reason == NULL &&
                                 !planned.plans[3].inlined && planned.plans[3].helper != BUSTER_CHECK_NONE && planned.plans[4].reason == NULL &&
                                 planned.plans[4].inlined && planned.plans[4].helper == BUSTER_CHECK_NONE && planned.plans[4].label_count == 1 && planned.plans[5].reason == NULL &&
                                 planned.plans[5].inlined && planned.plans[5].helper == BUSTER_CHECK_NONE && planned.plans[5].inline_branch_count == 2 && helpers.count == 1;
        snprintf(description, sizeof(description), "asm room, %s: the x87 statements are asm bodies, the __int128 operand is refused by name, rorx has a helper, the named label and the asm goto are asm bodies",
                 path == 0 ? "scalar" : "vector");
        check_test_expect(test, plans_right, description);
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&input_arena);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

// #1846's operand kinds placed in their callers as asm bodies (#1839's item 6), by both paths: MMX operands (`y`, and
// movd between an MMX and a general register), opmask operands (`k`, `Yk`), A's pair, a high byte's and an XMM17
// register variable. Each is an asm body with no helper, its register operands loaded from its block rather than passed
// as its call's arguments (check_asm_inline_passes), the opmask ones' registers among the masks it writes, and the MMX
// and opmask loads' rax among the registers it touches.
static void check_test_program_asm_kinds(CheckTest* test)
{
    static const char source[] =
        "long mmx(long a, long b){__asm__(\"paddd %1, %0\" : \"+y\"(a) : \"y\"(b));return a;}"
        "int mmx_general(int a){int r;__asm__(\"movd %1, %%mm3\\n\\tpaddd %%mm3, %%mm3\\n\\tmovd %%mm3, %0\\n\\temms\" : \"=r\"(r) : \"r\"(a) : \"mm3\");return r;}"
        "__attribute__((target(\"avx512f\"))) unsigned short mask(unsigned short a, unsigned short b){unsigned short r;"
        "__asm__(\"kandw %1, %2, %0\" : \"=k\"(r) : \"k\"(a), \"Yk\"(b));return r;}"
        "unsigned __int128 pair(unsigned long a, unsigned long b){unsigned __int128 r;__asm__(\"mulq %2\" : \"=A\"(r) : \"a\"(a), \"r\"(b));return r;}"
        "int high(int n){register unsigned char h __asm__(\"ah\")=(unsigned char)n;__asm__(\"addb $3, %0\" : \"+q\"(h));return h;}"
        "__attribute__((target(\"avx512f\"))) double upper(double x){register double r __asm__(\"xmm17\")=x;__asm__(\"vaddsd %0, %0, %0\" : \"+v\"(r));return r;}";
    enum
    {
        STATEMENTS = 6,
        MASK = 2,
    };
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const PredefinesOptions predefines = check_test_program_predefines();
    bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) &&
                        pp_run_source(&pp, "asm-kinds", (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "asm kinds: source preprocessing");
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 26);
        Arena output = arena_reserve(UINT64_C(1) << 26);
        Arena scratch = arena_reserve(UINT64_C(1) << 26);
        const ParseOptions parsing = { .scalar = path == 0 };
        const CheckOptions checking = { .scalar = path == 0 };
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers plans = { 0 };
        CheckAsms asms = { 0 };
        CheckProgram program = { 0 };
        bool ready = input_arena.base != 0 && output.base != 0 && scratch.base != 0 && check_test_types_open(&types) &&
                     parse_initialize(&parser, &parsing, pp_options.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0 &&
                     check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
                     check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &checking, &stream) && stream.report_count == 0 &&
                     check_initializers_run(&plans, &input, &stream, &parser, &types.table, &input_arena, &scratch) &&
                     check_asms_run(&asms, &input, &stream, &types.table, &input_arena, &scratch);
        const CheckProgramOptions options = { .initializers = &plans, .asms = &asms, .identifiers = &identifiers, .scalar = path == 0, .predefines = &predefines };
        ready = ready && check_program(&program, &parser, &input, &stream, &types.table, options, &output, &scratch) && input.asm_count == STATEMENTS;
        CheckProgramAsms planned = { 0 };
        CheckProgramHelpers helpers = { 0 };
        ready = ready && check_program_asm_plan(&planned, &helpers, &program, path == 0, &output, &scratch) && planned.plans != NULL;
        char description[256];
        snprintf(description, sizeof(description), "asm kinds, %s: the statements are planned, with no helper (%u)", path == 0 ? "scalar" : "vector", helpers.count);
        check_test_expect(test, ready && helpers.count == 0, description);
        for (uint32_t s = 0; ready && s < STATEMENTS; s += 1)
        {
            const CheckProgramAsm* plan = planned.plans + s;
            // mmx_general's operands are general registers alone: its body's operands are its call's arguments.
            const bool passes = s == 1;
            bool right = plan->planned && plan->reason == NULL && plan->inlined && plan->helper == BUSTER_CHECK_NONE && plan->inline_passed == passes;
            right = right && (s != MASK || ((plan->inline_masks & 0xfe) != 0 && (plan->inline_touched & (1u << X86_REGISTER_RAX)) != 0));
            snprintf(description, sizeof(description), "asm kinds, %s: statement %u is an asm body%s", path == 0 ? "scalar" : "vector", s,
                     passes ? " of its call's arguments" : " that loads its block");
            check_test_expect(test, right, description);
        }
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&input_arena);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

// The statements placed in their caller (#1059, check_asm_row) and the ones that keep their helper, as both paths plan
// them, with the same rows: each placed statement's row by its records (its template's forms, its operands' roles, the
// register outputs' stores) and its operands' classes; each other one's helper, and the plans' helper count. Placed:
// a `+` register (its address, then its value), two early-clobbered outputs in scratch registers, an XMM output from an
// XMM input, the fences and pause of no operand, a locked xadd of a `+` register and a `+` memory operand, a matching
// input converted to its output's type, a constant, and a clobber of a register the records don't write. Each other one
// is an asm body in its caller (#1839, check_asm_inline_code): fixed registers (cpuid's, whose inputs are its call's
// arguments, whose body runs cpuid and stores its four outputs through their addresses, and which touches rbx; rdtsc's; a shift's count
// in cl), a literal register (`%%rsp`, read), a syntax switch, more records than a row takes, an x87 operand, a form
// x86_semantics.c doesn't cover (ldmxcsr), an explicit register variable, and a register class narrower than the sixteen
// (Q's), a branch to a numeric label (a branch of the body), and a named label (#1839: the body's label, at the record
// after its movl, the template's end).
static void check_test_program_asm_placement(CheckTest* test)
{
    static const char source[] =
        "unsigned swapped(unsigned x){__asm__(\"bswap %0\" : \"+r\"(x));return x;}"
        "int pair(long a){int lo,hi;__asm__(\"movl %k2, %0\\n\\tleal 1(%2), %1\" : \"=&r\"(lo), \"=&r\"(hi) : \"r\"(a));return lo+hi;}"
        "double root(double x){double r;__asm__(\"sqrtsd %1, %0\" : \"=x\"(r) : \"x\"(x));return r;}"
        "void fences(void){__asm__ volatile(\"mfence\\n\\tlfence\\n\\tsfence\\n\\tpause\" ::: \"memory\");}"
        "int fetched(int* p, int v){__asm__ volatile(\"lock; xaddl %0, %1\" : \"+r\"(v), \"+m\"(*p) : : \"memory\");return v;}"
        "long widened(unsigned char c){long r;__asm__(\"addq %2, %0\" : \"=r\"(r) : \"0\"(c), \"i\"(7));return r;}"
        "void clobbering(void){__asm__ volatile(\"mfence\" ::: \"rax\", \"cc\", \"memory\");}"
        "unsigned identified(void){unsigned a,b,c,d;__asm__ volatile(\"cpuid\" : \"=a\"(a),\"=b\"(b),\"=c\"(c),\"=d\"(d) : \"0\"(0u),\"2\"(0u));return b;}"
        "unsigned stamped(void){unsigned lo,hi;__asm__ volatile(\"rdtsc\" : \"=a\"(lo), \"=d\"(hi));return lo^hi;}"
        "int shifted(int x, int n){__asm__(\"shll %b1, %0\" : \"+r\"(x) : \"c\"(n));return x;}"
        "long stack(void){long s;__asm__(\"movq %%rsp, %0\" : \"=r\"(s));return s;}"
        "int branched(int n){int r;__asm__(\"testl %1, %1\\n\\tjle 1f\\n\\tmovl $5, %0\\n1:\" : \"=r\"(r) : \"r\"(n), \"0\"(0));return r;}"
        "int intel(int n){__asm__ volatile(\".intel_syntax noprefix\\n\\tmfence\\n\\t.att_syntax\" : : \"r\"(n));return n;}"
        "int many(int n){__asm__(\"incl %0\\n\\tincl %0\\n\\tincl %0\\n\\tincl %0\\n\\tincl %0\\n\\tincl %0\\n\\tincl %0\\n\\tincl %0\" : \"+r\"(n));return n;}"
        "long double rooted(long double x){__asm__(\"fsqrt\" : \"+t\"(x));return x;}"
        "void loaded(unsigned m){__asm__ volatile(\"ldmxcsr %0\" : : \"m\"(m));}"
        "long fixed(long a){register long r10 __asm__(\"r10\") = a;__asm__(\"incq %0\" : \"+r\"(r10));return r10;}"
        "int high(int x){__asm__(\"incl %0\" : \"+Q\"(x));return x;}"
        "int named(int n){int r;__asm__(\"testl %1, %1\\n\\tjle .Lnamed%=\\n\\tmovl $5, %0\\n.Lnamed%=:\" : \"=r\"(r) : \"r\"(n), \"0\"(0));return r;}";
    static const uint16_t first_forms[] = {
        X86_FORM_BSWAP_R32, X86_FORM_MOV_RM32_R32, X86_FORM_SQRTSD_X_XM64, X86_FORM_MFENCE, X86_FORM_LOCK_XADD_M32_R32, X86_FORM_ADD_RM64_IMM8, X86_FORM_MFENCE,
    };
    static const uint8_t record_counts[] = { 2, 4, 2, 4, 2, 2, 1 };
    static const uint8_t operand_counts[] = { 2, 3, 2, 0, 3, 2, 0 };
    enum
    {
        PLACED = 7,
        KEPT = 12,
        BRANCHED = 11,
        NAMED = 18,
    };
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const PredefinesOptions predefines = check_test_program_predefines();
    bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) &&
                        pp_run_source(&pp, "asm-placement", (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "asm placement: source preprocessing");
    Tier0Operation rows[2][PLACED];
    memset(rows, 0, sizeof(rows));
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 26);
        Arena output = arena_reserve(UINT64_C(1) << 26);
        Arena scratch = arena_reserve(UINT64_C(1) << 26);
        const ParseOptions parsing = { .scalar = path == 0 };
        const CheckOptions checking = { .scalar = path == 0 };
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers plans = { 0 };
        CheckAsms asms = { 0 };
        CheckProgram program = { 0 };
        bool ready = input_arena.base != 0 && output.base != 0 && scratch.base != 0 && check_test_types_open(&types) &&
                     parse_initialize(&parser, &parsing, pp_options.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0 &&
                     check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
                     check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &checking, &stream) && stream.report_count == 0 &&
                     check_initializers_run(&plans, &input, &stream, &parser, &types.table, &input_arena, &scratch) &&
                     check_asms_run(&asms, &input, &stream, &types.table, &input_arena, &scratch);
        const CheckProgramOptions options = { .initializers = &plans, .asms = &asms, .identifiers = &identifiers, .scalar = path == 0, .predefines = &predefines };
        ready = ready && check_program(&program, &parser, &input, &stream, &types.table, options, &output, &scratch) && input.asm_count == PLACED + KEPT;
        CheckProgramAsms planned = { 0 };
        CheckProgramHelpers helpers = { 0 };
        ready = ready && check_program_asm_plan(&planned, &helpers, &program, path == 0, &output, &scratch) && planned.plans != NULL;
        char description[256];
        snprintf(description, sizeof(description), "asm placement, %s: the statements are planned", path == 0 ? "scalar" : "vector");
        check_test_expect(test, ready, description);
        for (uint32_t s = 0; ready && s < PLACED + KEPT; s += 1)
        {
            const CheckProgramAsm* plan = planned.plans + s;
            const Tier0Operation* row = plan->row;
            const bool placed = s < PLACED;
            const bool helped = false;
            bool right = plan->planned && plan->reason == NULL && (row != NULL) == placed && (plan->helper != BUSTER_CHECK_NONE) == helped &&
                         plan->inlined == (!placed && !helped) && (plan->inline_count != 0) == plan->inlined;
            if (right && s == BRANCHED)
            {
                // The branch's body: its inputs are its call's arguments, so it starts at testl, then jle and movl, the jle a branch of the body to
                // its label at the template's end, the first store.
                right = plan->inline_branch_count == 1 && plan->inline_branches[0] == 1 && plan->inline_targets[0] == 3 &&
                        plan->inline_records[0].form == X86_FORM_TEST_RM32_R32;
            }
            if (right && s == NAMED)
            {
                // The named label's body: testl, jle to the label and movl, the label at the template's end (record 3),
                // its spelling the substituted `.Lnamed` and the statement's number.
                right = plan->label_count == 1 && plan->labels[0].record == 3 && plan->inline_label_records != 0 && plan->inline_label_records[0] == 3 &&
                        plan->labels[0].spelling.length > 7 && memcmp(plan->labels[0].spelling.pointer, ".Lnamed", 7) == 0 && plan->inline_branch_count == 1 &&
                        plan->inline_targets[0] == 3;
            }
            if (right && s == PLACED)
            {
                // cpuid's body (#1839): its inputs are its call's arguments, which Tier 0 moves into eax and ecx, so it starts
                // at cpuid; then each output's address from the block into the store register, and its store.
                const X86Instruction* records = plan->inline_records;
                right = plan->inline_passed && plan->inline_count == 9 && records[0].form == X86_FORM_CPUID && records[1].rm == BUSTER_TIER0_ASM_BLOCK &&
                        records[2].form == X86_FORM_MOV_RM32_R32 && records[2].reg == X86_REGISTER_RAX &&
                        (plan->inline_touched & (1u << X86_REGISTER_RBX | 1u << X86_REGISTER_RDX)) == (1u << X86_REGISTER_RBX | 1u << X86_REGISTER_RDX) &&
                        (plan->inline_touched & (1u << X86_REGISTER_RSP | 1u << X86_REGISTER_RBP | 1u << X86_REGISTER_R15)) == 0;
            }
            if (right && placed)
            {
                rows[path][s] = *row;
                right = row->record_count == record_counts[s] && row->operand_count == operand_counts[s] && row->records[0].form == first_forms[s] &&
                        row->result_class == BUSTER_TYPED_CLASS_VOID && tier0_operation_valid(row);
            }
            snprintf(description, sizeof(description), "asm placement, %s: statement %u %s", path == 0 ? "scalar" : "vector", s,
                     placed ? "is a row of its records in its caller" : helped ? "keeps its helper" : "is an asm body in its caller");
            check_test_expect(test, right, description);
            for (uint32_t r = 0; !right && row != NULL && r < row->record_count; r += 1)
            {
                const X86Instruction* record = row->records + r;
                fprintf(stderr, "test-checker: asm placement row %u record %u: %s reg=0x%x rm=0x%x mode=0x%x immediate=%d (%u operands)\n", s, r,
                        x86_form_description(record->form)->mnemonic, record->reg, record->rm, record->mode, record->immediate, row->operand_count);
            }
        }
        if (ready)
        {
            // The roles: bswap's register is the `+` value's operand (1, after its address), stored through operand 0; the
            // early clobbers' are SCRATCH(0) and SCRATCH(1), stored through operands 0 and 1, the input operand 2 (mov's
            // source, lea's base); sqrtsd's output VECTOR(0), its input
            // FLOATING(1); the xadd's register INTEGER(1), its memory operand's base INTEGER(2); the matching input's
            // operand (1) is its output's U64 class, converted from its unsigned char.
            const Tier0Operation* swap = &rows[path][0];
            const Tier0Operation* early = &rows[path][1];
            const Tier0Operation* root = &rows[path][2];
            const Tier0Operation* xadd = &rows[path][4];
            const Tier0Operation* widened = &rows[path][5];
            const bool roles = swap->records[0].rm == BUSTER_TIER0_ROLE_INTEGER(1) && swap->records[1].form == X86_FORM_MOV_RM32_R32 &&
                               swap->records[1].reg == BUSTER_TIER0_ROLE_INTEGER(1) && swap->records[1].rm == BUSTER_TIER0_ROLE_INTEGER(0) &&
                               swap->operand_classes[0] == BUSTER_TYPED_CLASS_U64 && swap->operand_classes[1] == BUSTER_TYPED_CLASS_U32 &&
                               early->records[0].rm == BUSTER_TIER0_ROLE_SCRATCH(0) && early->records[0].reg == BUSTER_TIER0_ROLE_INTEGER(2) &&
                               early->records[1].reg == BUSTER_TIER0_ROLE_SCRATCH(1) && early->records[1].rm == BUSTER_TIER0_ROLE_INTEGER(2) &&
                               early->records[2].reg == BUSTER_TIER0_ROLE_SCRATCH(0) && early->records[2].rm == BUSTER_TIER0_ROLE_INTEGER(0) &&
                               early->records[3].reg == BUSTER_TIER0_ROLE_SCRATCH(1) && early->records[3].rm == BUSTER_TIER0_ROLE_INTEGER(1) &&
                               root->records[0].reg == BUSTER_TIER0_ROLE_VECTOR(0) && root->records[0].rm == BUSTER_TIER0_ROLE_FLOATING(1) &&
                               root->records[1].form == X86_FORM_MOVSD_XM64_X && root->operand_classes[1] == BUSTER_TYPED_CLASS_F64 &&
                               xadd->records[0].reg == BUSTER_TIER0_ROLE_INTEGER(1) && xadd->records[0].rm == BUSTER_TIER0_ROLE_INTEGER(2) &&
                               xadd->operand_classes[2] == BUSTER_TYPED_CLASS_U64 && widened->operand_classes[1] == BUSTER_TYPED_CLASS_I64 &&
                               widened->records[0].immediate == 7;
            snprintf(description, sizeof(description), "asm placement, %s: the rows' roles are the operands', scratch's and stores' as planned", path == 0 ? "scalar" : "vector");
            check_test_expect(test, roles, description);
            snprintf(description, sizeof(description), "asm placement, %s: no helper, of %u (the named label's is an asm body too)", path == 0 ? "scalar" : "vector", helpers.count);
            check_test_expect(test, helpers.count == 0, description);
        }
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&input_arena);
    }
    check_test_expect(test, preprocessed && memcmp(rows[0], rows[1], sizeof(rows[0])) == 0, "asm placement: both paths plan the same rows");
    pp_release(&pp);
    intern_release(&identifiers);
}

// Flag outputs (#147): every condition clang takes, after compares of values around each one's edges, into objects of
// every size an integer store covers (bool, char, short, int, long, __int128, a float's bits and a struct of 4 bytes),
// a `+` one, several beside register outputs and an x87 one, and one of a template of no instructions, whose condition
// is of the flags the code before it left, which each compiler's differs in: the object must be written whatever it was
// (0 or 1 of all of its 4 bytes, from 0x12345678), the helper's argument setup leaving the flags alone.
static void check_test_program_asm_flags(CheckTest* test, const char* clang)
{
    static const char* const conditions[] = { "a", "ae", "b", "be", "c", "e", "z", "g", "ge", "l", "le", "na", "nae", "nb",
                                              "nbe", "nc", "ne", "nz", "ng", "nge", "nl", "nle", "no", "np", "ns", "o", "p", "s" };
    char source[16384];
    uint64_t length = (uint64_t)snprintf(source, sizeof(source), "static int values[]={0,1,-1,2,-2,0x7fffffff,-0x7fffffff-1,1000,-1000};\n");
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(conditions); c += 1)
    {
        length += (uint64_t)snprintf(source + length, sizeof(source) - length,
                                     "static int cc_%s(int a,int b){int r=7;__asm__(\"cmpl %%2, %%1\" : \"=@cc%s\"(r) : \"r\"(a), \"r\"(b));return r;}\n",
                                     conditions[c], conditions[c]);
    }
    // Each pair of the values, the extremes among them, compared as they are: the harness's n (-3 to 19) rotates the first
    // one's choice rather than offsetting it, which would overflow INT_MAX (#1126's review).
    length += (uint64_t)snprintf(source + length, sizeof(source) - length,
                                 "int tested(int n){unsigned s=0;for(int i=0;i<9;i++)for(int j=0;j<9;j++){int a=values[(unsigned)(i+n+27)%%9u],b=values[j];\n");
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(conditions); c += 1)
    {
        length += (uint64_t)snprintf(source + length, sizeof(source) - length, "s=s*3+(unsigned)cc_%s(a,b);\n", conditions[c]);
    }
    snprintf(source + length, sizeof(source) - length, "}return (int)s;}\n");
    check_test_program_source(test, source, "asm-flag-conditions", clang);
    check_test_program_source(test,
        "struct four{unsigned char b[4];};"
        "int tested(int n){_Bool zb=1;char zc=9;short zs=-1;long zl=-1;unsigned long long zu=~0ull;__int128 zi=-1;float zf=2.5f;struct four zt={{9,9,9,9}};"
        "__asm__(\"testl %1, %1\" : \"=@ccz\"(zb) : \"r\"(n));__asm__(\"testl %1, %1\" : \"=@ccnz\"(zc) : \"r\"(n));"
        "__asm__(\"cmpl $5, %1\" : \"=@ccl\"(zs) : \"r\"(n));__asm__(\"cmpl $5, %1\" : \"=@ccge\"(zl) : \"r\"(n));"
        "__asm__(\"cmpl $3, %1\" : \"=@ccb\"(zu) : \"r\"(n));__asm__(\"cmpl $3, %1\" : \"=@ccae\"(zi) : \"r\"(n));"
        "__asm__(\"testl %1, %1\" : \"=@ccs\"(zf) : \"r\"(n));__asm__(\"testl %1, %1\" : \"=@ccns\"(zt) : \"r\"(n));"
        "unsigned fb;__builtin_memcpy(&fb,&zf,4);unsigned tb;__builtin_memcpy(&tb,&zt,4);"
        "return zb+zc*2+zs*4+(int)zl*8+(int)zu*16+(int)(zi>>64)*32+(int)zi*64+(int)fb*128+(int)tb*256;}",
        "asm-flag-objects", clang);
    check_test_program_source(test,
        "int tested(int n){int z=5;long q=9;int r;__asm__(\"movl %3, %1\\n\\taddl $1, %1\\n\\tcmpl $4, %3\" : \"+@ccg\"(z), \"=&r\"(r), \"=@ccle\"(q) : \"r\"(n));"
        "long double x=n<0?-n:n;int e;__asm__(\"fsqrt\\n\\tcmpl $2, %2\" : \"+t\"(x), \"=@cce\"(e) : \"r\"(n));"
        "int k;__asm__(\"xorl %1, %1\\n\\tsubl $1, %1\" : \"=@ccc\"(k), \"+r\"(r) : : \"cc\");"
        "int w=0x12345678;__asm__(\"\" : \"=@ccz\"(w));int u=0x12345678;__asm__(\"\" : \"=@ccnz\"(u));"
        "return z+r*3+(int)q*5+(int)(x*16)+e*7+k*11+r*13+(w==0||w==1)*17+(u==0||u==1)*19+(w+u==1)*23;}",
        "asm-flag-mixed", clang);
    // Each flag output stored once over its object (#1126's review), counted where it lands: the objects lie on a page
    // the program makes read-only, whose write fault each store takes, and which the fault's handler opens for that store
    // alone (a single step, whose trap closes it again). A volatile object of each width takes one store, two outputs of a
    // statement one each, an _Atomic one its exchange, an asm goto's output one on either way out; then their values.
    check_test_program_includes = true;
    check_test_program_source(test,
        "#define _GNU_SOURCE\n#include <signal.h>\n#include <sys/mman.h>\n#include <ucontext.h>\n"
        "static volatile int faults;static unsigned char* page;\n"
        "static void on_write(int signal,siginfo_t* info,void* context){(void)signal;(void)info;faults+=1;mprotect(page,4096,PROT_READ|PROT_WRITE);"
        "((ucontext_t*)context)->uc_mcontext.gregs[REG_EFL]|=0x100;}\n"
        "static void on_step(int signal,siginfo_t* info,void* context){(void)signal;(void)info;mprotect(page,4096,PROT_READ);"
        "((ucontext_t*)context)->uc_mcontext.gregs[REG_EFL]&=~0x100;}\n"
        "int tested(int n){struct sigaction write_action={0},step_action={0},old_write,old_step;\n"
        "write_action.sa_sigaction=on_write;write_action.sa_flags=SA_SIGINFO;step_action.sa_sigaction=on_step;step_action.sa_flags=SA_SIGINFO;\n"
        "page=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);if(page==MAP_FAILED)return -1;\n"
        "sigaction(SIGSEGV,&write_action,&old_write);sigaction(SIGTRAP,&step_action,&old_step);\n"
        "volatile unsigned* v=(volatile unsigned*)page;_Atomic unsigned* a=(_Atomic unsigned*)(page+64);volatile unsigned short* s=(volatile unsigned short*)(page+128);\n"
        "volatile unsigned long* l=(volatile unsigned long*)(page+192);volatile unsigned char* c=(volatile unsigned char*)(page+256);"
        "_Atomic unsigned long* al=(_Atomic unsigned long*)(page+320);\n"
        "unsigned h=(unsigned)n;\n"
        "for(int i=-2;i<3;i++){int x=i+n;*v=7;*a=7;*s=7;*l=7;*c=7;*al=7;mprotect(page,4096,PROT_READ);faults=0;\n"
        "__asm__ volatile(\"testl %1,%1\" : \"=@ccz\"(*v) : \"r\"(x) : \"cc\");h=h*31+(unsigned)faults;faults=0;\n"
        "__asm__ volatile(\"testl %1,%1\" : \"=@ccnz\"(*a) : \"r\"(x) : \"cc\");h=h*31+(unsigned)faults;faults=0;\n"
        "__asm__ volatile(\"cmpl $1,%2\" : \"=@ccl\"(*s), \"=@ccg\"(*l) : \"r\"(x) : \"cc\");h=h*31+(unsigned)faults;faults=0;\n"
        "__asm__ volatile(\"cmpl $0,%1\" : \"=@ccs\"(*al) : \"r\"(x) : \"cc\");h=h*31+(unsigned)faults;faults=0;\n"
        "__asm__ goto(\"testl %1,%1\\n\\tjs %l[negative]\" : \"=@ccz\"(*c) : \"r\"(x) : \"cc\" : negative);h=h*31+(unsigned)faults*7;goto done;\n"
        "negative:h=h*31+(unsigned)faults*11;\n"
        "done:mprotect(page,4096,PROT_READ|PROT_WRITE);h=h*31+*v+*a*2+*s*4+(unsigned)*l*8+*c*16+(unsigned)*al*32;}\n"
        "sigaction(SIGSEGV,&old_write,0);sigaction(SIGTRAP,&old_step,0);munmap(page,4096);return (int)h;}\n",
        "asm-flag-single-stores", clang);
    check_test_program_includes = false;
    check_test_program_boundary_reason(test, "int tested(int n){_Atomic __int128 z;__asm__(\"testl %1, %1\" : \"=@ccz\"(z) : \"r\"(n));return (int)z;}",
                                       "asm-flag-output-atomic-size", true, false, 0, 0, "inline assembly flag outputs of this size");
}

// asm goto (#147): a label by name and by number (after the operands and a `+` operand's input), a jcc's and a jmp's,
// several labels and one named twice, a label before the statement (a loop), outputs whose values reach every label,
// a flag output beside them, statements in a loop of live values and in a function with a variable-length array, and a
// template that falls through.
static void check_test_program_asm_goto(CheckTest* test, const char* clang)
{
    check_test_program_source(test,
        "static int sign(int x){__asm__ goto(\"testl %0, %0\\n\\tjz %l[zero]\\n\\tjs %l2\" : : \"r\"(x) : \"cc\" : zero, negative);return 1;zero:return 0;negative:return -1;}"
        "static int pick(int x){__asm__ goto(\"cmpl $1, %0\\n\\tje %l1\\n\\tcmpl $2, %0\\n\\tje %l[two]\\n\\tcmpl $3, %0\\n\\tje %l[one]\\n\\tjmp %l[other]\" : : \"r\"(x) : \"cc\" : one, two, other);"
        "return 100;one:return 1;two:return 2;other:return 3;}"
        "static int count_down(int x){int steps=0;again:steps+=1;x-=1;__asm__ goto(\"testl %0, %0\\n\\tjg %l[again]\" : : \"r\"(x) : \"cc\" : again);return steps;}"
        "static int fall(int x){__asm__ goto(\"nop\" : : \"r\"(x) : : never);return x+5;never:return -5;}"
        "int tested(int n){unsigned s=0;for(int i=-3;i<6;i++)s=s*7+(unsigned)(sign(i+n)+3)+(unsigned)pick(i+n)*11+(unsigned)fall(i);return (int)s+count_down(n+4)*13;}",
        "asm-goto-labels", clang);
    check_test_program_source(test,
        "static int out(int x){int r;__asm__ goto(\"movl %1, %0\\n\\taddl $10, %0\\n\\tcmpl $3, %1\\n\\tjl %l[small]\" : \"=&r\"(r) : \"r\"(x) : \"cc\" : small);"
        "return r;small:return r+1000;}"
        "static int both(int x){int y=x;long z=0;__asm__ goto(\"addl $1, %0\\n\\tmovq $7, %1\\n\\tcmpl $5, %0\\n\\tjge %l3\" : \"+r\"(y), \"=r\"(z) : : \"cc\" : big);"
        "return y+(int)z;big:return y*2+(int)z;}"
        "static int flags(int x){int f;__asm__ goto(\"cmpl $0, %1\\n\\tjs %l[neg]\" : \"=@ccz\"(f) : \"r\"(x) : : neg);return f;neg:return 10+f;}"
        "static int live(int x){int a=x*3,b=x*5,c=x*7,t=0;for(int i=0;i<x+4;i++){__asm__ goto(\"testl $1, %0\\n\\tjnz %l[odd]\" : : \"r\"(i) : \"cc\" : odd);"
        "t+=a;continue;odd:t+=b+c;}return t+a+b+c;}"
        "static int vla(int x){int k=x<1?1:x;int v[k];for(int i=0;i<k;i++)v[i]=i*x;int s=0;for(int i=0;i<k;i++){__asm__ goto(\"cmpl $4, %0\\n\\tjg %l[skip]\" : : \"r\"(v[i]) : \"cc\" : skip);"
        "s+=v[i];skip:;}return s;}"
        "int tested(int n){unsigned s=0;for(int i=-2;i<9;i++)s=s*5+(unsigned)out(i+n)+(unsigned)both(i)*3+(unsigned)flags(i-n)*7+(unsigned)live(i&3)+(unsigned)vla(i&7);return (int)s;}",
        "asm-goto-outputs", clang);
    // Bodies that branch straight to their labels (#2351): a label right after the statement, a template's own branch to
    // its end, an always_inline function's statement spliced twice, one under a value of its expression (whose dispatch
    // stays), and one in a loop of live values in callee-saved registers.
    check_test_program_source(test,
        "static int next(int x){int s=1;__asm__ goto(\"cmpl $2, %0\\n\\tje %l[n]\" : : \"r\"(x) : \"cc\" : n);s=2;n:return s+x;}"
        "static int ends(int x){__asm__ goto(\"cmpl $3, %0\\n\\tje 1f\\n\\tcmpl $4, %0\\n\\tje %l[four]\\n1:\" : : \"r\"(x) : \"cc\" : four);return x;four:return 40;}"
        "static inline __attribute__((always_inline)) int is_zero(int x){__asm__ goto(\"testl %0, %0\\n\\tjz %l[z]\" : : \"r\"(x) : \"cc\" : z);return 0;z:return 1;}"
        "static int twice(int x){return is_zero(x)*3+is_zero(x-1);}"
        "static int under(int x){int r=x*3+({__asm__ goto(\"testl %0, %0\\n\\tjs %l[m]\" : : \"r\"(x) : \"cc\" : m);1;});return r;m:return -7;}"
        "static int kept(int x){long a=x*3,b=x*5,c=x*7,d=x*11,e=x*13,f=x*17;long t=0;for(int i=0;i<x+6;i++){__asm__ goto(\"testl $2, %0\\n\\tjnz %l[two]\" : : \"r\"(i) : \"cc\", \"rbx\", \"r12\" : two);"
        "t+=a+b;continue;two:t+=c*d-e+f;}return (int)(t+a+b+c+d+e+f);}"
        "int tested(int n){unsigned s=0;for(int i=-3;i<8;i++)s=s*7+(unsigned)next(i+n)+(unsigned)ends(i)*5+(unsigned)twice(i)*11+(unsigned)under(i-n)*13+(unsigned)kept(i&3);return (int)s;}",
        "asm-goto-direct", clang);
}

static void check_test_program_asm(CheckTest* test, const char* clang)
{
    // F3-prefixed scans work on baseline too. Compare with clang on the same machine: REP BSR may execute as LZCNT.
    check_test_program_source(test,
        "int tested(int n){unsigned x=(unsigned)n|256u,lo,hi;"
        "__asm__(\"rep bsfl %1,%0\" : \"=r\"(lo) : \"r\"(x) : \"cc\");"
        "__asm__(\"rep bsrl %1,%0\" : \"=r\"(hi) : \"r\"(x) : \"cc\");return (int)(lo+hi*32u);}",
        "asm-rep-scans", clang);
    check_test_program_asm_room(test);
    check_test_program_asm_kinds(test);
    check_test_program_asm_placement(test);
    // Statements placed in their caller (#1059), each run against clang's build: values live across them in Tier 0's
    // depth registers and register homes, in a loop and under an expression 18 values deep (past the depth registers,
    // in the deep slots), a + register and a memory operand on the stack below them.
    check_test_program_source(test,
        "int tested(int n){unsigned long a=n,b=n+1,c=n+2,d=n+3,e=n+4,f=n+5,g=n+6,h=n+7;unsigned s=0;"
        "for(int i=0;i<5;i++){unsigned x=(unsigned)(i*n)+(unsigned)a;__asm__(\"bswap %0\" : \"+r\"(x));s+=x^(unsigned)(a+b+c+d+e+f+g+h);a+=1;h-=1;}"
        "unsigned long t=a*(b+(c*(d+(e*(f+(g*(h+(a*(b+(c*(d+(e*(f+(g*(h+(a*(b+({unsigned long u=n;__asm__(\"negq %0\" : \"+r\"(u));u;}))))))))))))))))));"
        "int m=n;__asm__ volatile(\"lock; addl %1, %0\" : \"+m\"(m) : \"r\"((int)(t&7)) : \"memory\");"
        "return (int)(s^(unsigned)t^(unsigned)(a*3+h)^(unsigned)m);}",
        "asm-placed-live-values", clang);
    // Tied operands and early clobbers: a matching input narrower than its output (zero-extended), an output the template
    // writes before it reads its inputs (=&r), two early-clobbered outputs, a short + register and narrow outputs (a byte
    // from %b, a bool from setz), a constant, and an address computed from two registers.
    check_test_program_source(test,
        "int tested(int n){unsigned char c=(unsigned char)n;long w;__asm__(\"addq $3, %0\" : \"=r\"(w) : \"0\"(c));"
        "int r;__asm__(\"movl %1, %0\\n\\tsubl %2, %0\" : \"=&r\"(r) : \"r\"(n), \"r\"(7));int lo,hi;"
        "__asm__(\"movl %k2, %0\\n\\tleal 5(%2), %1\" : \"=&r\"(lo), \"=&r\"(hi) : \"r\"((long)n));short s=(short)n;__asm__(\"incw %0\" : \"+r\"(s));"
        "unsigned char b;__asm__(\"movb %b1, %0\" : \"=r\"(b) : \"r\"(n+200));_Bool z;__asm__(\"testl %1, %1\\n\\tsetz %0\" : \"=r\"(z) : \"r\"(n) : \"cc\");"
        "long p;__asm__(\"leaq %c3(%1,%2,4), %0\" : \"=r\"(p) : \"r\"((long)n), \"r\"((long)r), \"i\"(9));"
        "return (int)((unsigned)w+(unsigned)r*3u+(unsigned)lo*5u+(unsigned)hi*7u+(unsigned)s*11u+(unsigned)b*13u+(unsigned)z*17u+(unsigned)p*19u);}",
        "asm-placed-tied-early", clang);
    // Memory operands: a + one under a locked add, a + register and a struct member's under xadd and xchg, an output of
    // memory, an array element whose index has a side effect (evaluated once), and a memory input.
    check_test_program_source(test,
        "struct S{int a;long b;};int tested(int n){int v=n;struct S s={n,2L*n};__asm__ volatile(\"lock; addl %1, %0\" : \"+m\"(v) : \"r\"(5) : \"memory\");"
        "int old=10;__asm__ volatile(\"lock; xaddl %0, %1\" : \"+r\"(old), \"+m\"(s.a) : : \"memory\");__asm__(\"movq %1, %0\" : \"=m\"(s.b) : \"r\"((long)v*3));"
        "int arr[4]={1,2,3,4};int i=0;__asm__(\"addl $100, %0\" : \"+m\"(arr[i++]));int m;__asm__(\"movl %1, %0\" : \"=r\"(m) : \"m\"(arr[1]));"
        "unsigned x=(unsigned)n;__asm__(\"xchgl %0, %1\" : \"+r\"(x), \"+m\"(arr[2]));"
        "return (int)((unsigned)v+(unsigned)old*3u+(unsigned)s.a*5u+(unsigned)s.b*7u+(unsigned)arr[0]*11u+(unsigned)i*13u+(unsigned)m*17u+x*19u+(unsigned)arr[2]*23u);}",
        "asm-placed-memory", clang);
    // The flags: a template that changes them (cc) inside a condition's chain, a compare and setl of the template's own,
    // and a not in a loop's body, between the loop's compares.
    check_test_program_source(test,
        "int tested(int n){unsigned z=(unsigned)n;int r=0;if(n>3&&({__asm__(\"addl $1, %0\" : \"+r\"(z) : : \"cc\");z;})>5u)r=1;"
        "int lt;__asm__(\"cmpl %2, %1\\n\\tsetl %b0\\n\\tmovzbl %b0, %0\" : \"=&r\"(lt) : \"r\"(n), \"r\"(10) : \"cc\");"
        "for(int i=0;i<(n&7);i++){__asm__(\"notl %0\" : \"+r\"(z) : : \"cc\");}return r*1000+lt*100+(int)(z&0xff);}",
        "asm-placed-flags", clang);
    // XMM operands of floats and doubles, with floating values live across them: sqrtsd into an output of its own, a +
    // float's mulss, conversions to and from a general register, and a + double under an expression of live values.
    check_test_program_source(test,
        "int tested(int n){double d=(double)n,a=1.5,b=2.5,c=3.5;double r;__asm__(\"sqrtsd %1, %0\" : \"=x\"(r) : \"x\"(d*d));"
        "float f=(float)n;__asm__(\"mulss %1, %0\" : \"+x\"(f) : \"x\"(2.0f));double s;__asm__(\"cvtsi2sdl %1, %0\" : \"=x\"(s) : \"r\"(n));"
        "long t;__asm__(\"cvttsd2si %1, %0\" : \"=r\"(t) : \"x\"(d+0.75));"
        "double u=a+b*c+({double v=d;__asm__(\"addsd %0, %0\" : \"+x\"(v));v;})*(a-c);return (int)(r*1000+f*100+s*10+u)+(int)t;}",
        "asm-placed-floating", clang);
    // Fences, pause and prefetches in a loop, beside a store and a load across them.
    check_test_program_source(test,
        "int data[8];int tested(int n){int s=0;for(int i=0;i<8;i++){data[i]=n+i;__asm__ volatile(\"mfence\" ::: \"memory\");"
        "__asm__ volatile(\"prefetcht0 %0\\n\\tprefetchw %0\" : : \"m\"(data[(i+1)&7]));__asm__ volatile(\"lfence\\n\\tpause\\n\\tsfence\" ::: \"memory\");s+=data[i];}"
        "return s;}",
        "asm-placed-fences", clang);
    // Asm bodies in their callers (#1839), against clang's build: always_inline wrappers of rdtsc and cpuid, which Tier 0's
    // splicing copies into a caller with a body of its own, in a loop of eight live values; cpuid under an expression 11
    // values deep, whose rbx holds one of them (saved around the body), and 18 deep (the deep slots); and a syscall's.
    check_test_program_source(test,
        "static inline __attribute__((always_inline)) unsigned long stamp(void){unsigned lo,hi;__asm__ volatile(\"rdtsc\" : \"=a\"(lo), \"=d\"(hi));"
        "return ((unsigned long)hi<<32)|lo;}"
        "static inline __attribute__((always_inline)) unsigned vendor(unsigned leaf){unsigned a,b,c,d;"
        "__asm__ volatile(\"cpuid\" : \"=a\"(a), \"=b\"(b), \"=c\"(c), \"=d\"(d) : \"0\"(leaf), \"2\"(0u));return b^c^d;}"
        "static unsigned long ebx(void){unsigned x,y,z,w;__asm__ volatile(\"cpuid\" : \"=a\"(x), \"=b\"(y), \"=c\"(z), \"=d\"(w) : \"0\"(0u), \"2\"(0u));"
        "return (unsigned long)(y!=0);}"
        "int tested(int n){unsigned long a=n,b=n+1,c=n+2,d=n+3,e=n+4,f=n+5,g=n+6,h=n+7;unsigned s=0;"
        "for(int i=0;i<4;i++){unsigned v=vendor(0);unsigned long t0=stamp();unsigned long t1=stamp();__asm__ volatile(\"lfence\" ::: \"memory\");"
        "s=s*3+(v!=0)+(t1>=t0)+(unsigned)(a+b+c+d+e+f+g+h);a+=1;h-=1;}"
        "unsigned long t=a*(b+(c*(d+(e*(f+(g*(h+(a*(b+(c*({unsigned x,y,z,w;__asm__ volatile(\"cpuid\" : \"=a\"(x), \"=b\"(y), \"=c\"(z), \"=d\"(w) : \"0\"(0u), \"2\"(0u));"
        "(unsigned long)(y!=0)+n;})))))))))));"
        "unsigned long u=a*(b+(c*(d+(e*(f+(g*(h+(a*(b+(c*(d+(e*(f+(g*(h+(a*(b+ebx()+({unsigned long k=(unsigned long)n;__asm__(\"imulq $3, %1, %0\" : \"=r\"(k) : \"r\"(k));k;})"
        ")))))))))))))))));"
        "long r;__asm__ volatile(\"syscall\" : \"=a\"(r) : \"0\"(39L) : \"rcx\", \"r11\", \"memory\");"
        "return (int)(s^(unsigned)t^(unsigned)u^(unsigned)(r>0));}",
        "asm-bodies", clang);
    // Asm bodies of templates with their own labels and branches (#1839): a backward loop (decl and jnz), a forward jmp over a store, a template whose branches go to its end (the stores), and a byte copy's loop of
    // two pointers, each in a loop of live values, against clang's build.
    check_test_program_source(test,
        "static int spin(int n){int c=0;__asm__(\"testl %1, %1\\n\\tjle 2f\\n1:\\n\\taddl $3, %0\\n\\tdecl %1\\n\\tjnz 1b\\n2:\" : \"+r\"(c), \"+r\"(n) : : \"cc\");return c;}"
        "static int skip(int x){int r=x;__asm__(\"cmpq $10, %1\\n\\tjg 1f\\n\\tmovl $-1, %0\\n\\tjmp 2f\\n1:\\n\\tleal 100(%1), %0\\n2:\" : \"=&r\"(r) : \"r\"((long)x) : \"cc\");return r;}"
        "static void copy(char* d, const char* s, unsigned long n){__asm__ volatile(\"testq %2, %2\\n\\tjz 2f\\n1:\\n\\tmovb (%1), %%al\\n\\tmovb %%al, (%0)\\n\\tincq %0\\n\\tincq %1\\n\\tdecq %2\\n\\tjnz 1b\\n2:\""
        " : \"+r\"(d), \"+r\"(s), \"+r\"(n) : : \"rax\", \"cc\", \"memory\");}"
        "int tested(int n){unsigned long a=n,b=n+1,c=n+2,d=n+3,e=n+4,f=n+5,g=n+6,h=n+7;unsigned s=0;char buf[16]={0};"
        "for(int i=-2;i<12;i++){s=s*7+(unsigned)spin(i+n)+(unsigned)skip(i+n)+(unsigned)(a+b+c+d+e+f+g+h);a+=1;h-=1;"
        "copy(buf,\"0123456789abcdef\",(unsigned long)(i<0?0:i));s+=(unsigned)buf[i<0?0:i];}"
        "return (int)s;}",
        "asm-body-branches", clang);
    // Templates of no instructions, the compiler barriers: their operands evaluated, and nothing called. A comment with
    // operands substituted into it assembles to nothing too.
    check_test_program_source(test,
        "int tested(int n){int x=n*3;__asm__ volatile(\"\" : : \"r\"(x) : \"memory\");__asm__ volatile(\"\" ::: \"memory\");int y=n;"
        "__asm__(\"\" : \"+r\"(y));__asm__ volatile(\"\" : : \"m\"(x), \"r\"(&y) : \"memory\");__asm__ volatile(\"# %0 %1\" : : \"r\"(x), \"m\"(y));"
        "int t;__asm__(\"\" : \"=r\"(t) : \"0\"(n+5));return x+y*2+t*3;}",
        "asm-barriers", clang);
    // Registers of a class, a matching input, an early clobber, an integer constant, and the size modifiers.
    check_test_program_source(test,
        "int tested(int n){int x=n;__asm__(\"addl $1, %0\" : \"+r\"(x));long a=n,b=2*n,r;__asm__(\"leaq (%1,%2), %0\" : \"=r\"(r) : \"r\"(a), \"r\"(b));"
        "int t;__asm__(\"incl %0\" : \"=r\"(t) : \"0\"(n));int i;__asm__(\"movl %1, %0\\n\\taddl %2, %0\" : \"=&r\"(i) : \"i\"(40), \"r\"(n));"
        "long w=0;__asm__(\"movq %q1, %0\\n\\tmovb %b1, %b0\" : \"=&r\"(w) : \"r\"(n));"
        "return x*7+(int)r*3+t*5+i+(int)(w&0xffff);}",
        "asm-registers-and-modifiers", clang);
    // Operands by name, and g taking a constant.
    check_test_program_source(test,
        "int tested(int n){int r;__asm__(\"movl %[in], %[out]\\n\\tsubl $3, %[out]\" : [out] \"=r\"(r) : [in] \"r\"(n));int s=n;"
        "__asm__(\"addl %[k], %[v]\" : [v] \"+r\"(s) : [k] \"g\"(7));return r*11+s;}",
        "asm-named-operands", clang);
    // Fixed registers: cpuid's, with rbx (callee-saved) among them, and matching inputs.
    check_test_program_source(test,
        "int tested(int n){unsigned a,b,c,d;__asm__ volatile(\"cpuid\" : \"=a\"(a),\"=b\"(b),\"=c\"(c),\"=d\"(d) : \"0\"(0u),\"2\"(0u));"
        "return (int)((b^c^d)&0xffff)+n;}",
        "asm-cpuid-fixed-registers", clang);
    // clang's cpuid.h on x86-64: dialect alternatives, %q1, and rbx swapped with an operand's register; into array elements.
    check_test_program_source(test,
        "#define CPUID(l,s,a,b,c,d) __asm(\"  xchg{q|}  {%%|}rbx,%q1\\n  cpuid\\n  xchg{q|}  {%%|}rbx,%q1\" : \"=a\"(a),\"=r\"(b),\"=c\"(c),\"=d\"(d) : \"0\"(l),\"2\"(s))\n"
        "int tested(int n){unsigned a,b,c,d;CPUID(0,0,a,b,c,d);unsigned w[4];CPUID(1,0,w[0],w[1],w[2],w[3]);"
        "return (int)((a^b^c^d)&0xfff)+(int)(w[0]&0xfff)+n;}",
        "asm-cpuid-header-form", clang);
    // The x87's control and status words and MXCSR through memory operands, outputs and inputs.
    check_test_program_source(test,
        "int tested(int n){unsigned short c,d;__asm__ volatile(\"fnstcw %0\" : \"=m\"(c));d=c;__asm__ volatile(\"fldcw %0\" : : \"m\"(d));"
        "unsigned short s;__asm__ volatile(\"fnclex\\n\\tfnstsw %0\" : \"=m\"(s));unsigned m,k;__asm__ volatile(\"stmxcsr %0\" : \"=m\"(m));k=m;"
        "__asm__ volatile(\"ldmxcsr %0\" : : \"m\"(k));return (int)c+(int)(s&0x3f)+(int)(m&0xffc0)+n;}",
        "asm-control-words", clang);
    // syscall (getpid), with an input sharing its output's fixed register, a + operand, and register clobbers.
    check_test_program_source(test,
        "int tested(int n){long r;__asm__ volatile(\"syscall\" : \"=a\"(r) : \"a\"(39L) : \"rcx\",\"r11\",\"memory\");long q=39;"
        "__asm__ volatile(\"syscall\" : \"+a\"(q) : : \"rcx\",\"r11\",\"memory\");return (r==q)*100+(r>0)+n;}",
        "asm-syscall", clang);
    check_test_program_source(test,
        "int tested(int n){unsigned lo,hi;__asm__ volatile(\"pause\\n\\tlfence\\n\\tmfence\\n\\tsfence\\n\\trdtsc\" : \"=a\"(lo), \"=d\"(hi) : : \"memory\");"
        "return (lo!=0 || hi!=0)+n;}", "asm-pause-fences-rdtsc", clang);
    check_test_program_source(test,
        "int tested(int n){__asm__ volatile(\"rep nop\\n\\trep; nop\" : : : \"memory\");return n+1;}",
        "asm-rep-nop", clang);
    // Branches to the template's own labels: %='s labels forward, a numeric one backward in a loop.
    check_test_program_source(test,
        "int tested(int n){int r;__asm__(\"testl %1, %1\\n\\tjle .Lneg%=\\n\\tmovl $5, %0\\n\\tjmp .Lend%=\\n.Lneg%=:\\n\\tmovl $7, %0\\n.Lend%=:\" : \"=r\"(r) : \"r\"(n));"
        "int s=0,k=(n&7)+1;__asm__(\"1:\\n\\taddl $2, %0\\n\\tdecl %1\\n\\tjnz 1b\" : \"+r\"(s), \"+r\"(k));return r*100+s+n;}",
        "asm-local-branches", clang);
    // Named labels placed in their caller (#1839): a loop back to a %= label, a label of a template of no instructions (a
    // place in the caller's code), and a global label another template refers to as data, each address within the
    // caller's code.
    check_test_program_source(test,
        "int tested(int n){int r=0,k=(n&7)+1;__asm__(\".Lcount%=:\\n\\tincl %1\\n\\tdecl %0\\n\\tjnz .Lcount%=\" : \"+r\"(k), \"+r\"(r));"
        "__asm__ volatile(\"buster_place_%=:\" : : : \"memory\");__asm__(\"buster_named_here:\\n\\taddl $3, %0\" : \"+r\"(r));long p,q;"
        "__asm__(\"leaq buster_named_here(%%rip), %0\" : \"=r\"(p));q=(long)&tested;"
        "return r*10+(p!=q&&p!=0)+n;}",
        "asm-named-labels", clang);
    check_test_program_source(test, "int tested(int n){__asm__(\"lonely:\");return n;}", "asm-label-no-instructions", clang);
    // Callee-saved clobbers inside a loop of live values, and a statement expression's asm under a live value.
    check_test_program_source(test,
        "int tested(int n){long a=n,b=n+1,c=n+2,d=n+3,e=n+4,f=n+5,g=n+6;long r;"
        "for(int i=0;i<3;i++){__asm__(\"movq %1, %%rbx\\n\\tmovq %%rbx, %%r12\\n\\tleaq 1(%%r12), %0\" : \"=r\"(r) : \"r\"(a+i) : \"rbx\",\"r12\");a=r-i;}"
        "long t=a+({long u;__asm__(\"movq %1, %0\\n\\tnegq %0\" : \"=r\"(u) : \"r\"(b));u;})+c*d;return (int)(t+e*f+g);}",
        "asm-callee-saved-clobbers", clang);
    // Every callee-saved register but rbp clobbered with values of the template's own, under eight live values, which
    // Tier 0 keeps in callee-saved registers across the call: the helper must restore them.
    check_test_program_source(test,
        "int tested(int n){long a=n,b=n+1,c=n+2,d=n+3,e=n+4,f=n+5,g=n+6,h=n+7;for(int i=0;i<2;i++){"
        "__asm__ volatile(\"movq $-1, %%rbx\\n\\tmovq $-2, %%r12\\n\\tmovq $-3, %%r13\\n\\tmovq $-4, %%r14\\n\\tmovq $-5, %%r15\" : : : "
        "\"rbx\",\"r12\",\"r13\",\"r14\",\"r15\");a+=b;b+=c;c+=d*i;}return (int)(a*3+b*5+c*7+d*11+e*13+f*17+g*19+h*23);}",
        "asm-callee-saved-live", clang);
    // Memory operands: members, elements at a runtime index, a global, and through a pointer.
    check_test_program_source(test,
        "struct S{int a;long b[3];};int g;int tested(int n){struct S s={n,{1,2,3}};int arr[4]={n,n+1,n+2,n+3};"
        "__asm__(\"incl %0\" : \"+m\"(s.a));__asm__(\"addq $5, %0\" : \"+m\"(s.b[1]));__asm__(\"negl %0\" : \"+m\"(arr[n&3]));"
        "__asm__(\"movl %1, %0\" : \"=m\"(g) : \"r\"(n*9));int*p=&arr[1];__asm__(\"subl $2, %0\" : \"+m\"(*p));"
        "return s.a+(int)s.b[1]+arr[0]+arr[1]+arr[2]+arr[3]+g;}",
        "asm-memory-operands", clang);
    // Outputs of every width into globals and a pointer, and the high byte (%h).
    check_test_program_source(test,
        "long counter;unsigned char small;short mid;int tested(int n){__asm__(\"addq %1, %0\" : \"+r\"(counter) : \"r\"((long)n));"
        "__asm__(\"movb %1, %0\" : \"=q\"(small) : \"q\"((unsigned char)(n*3)));__asm__(\"movw %1, %0\\n\\tincw %0\" : \"=r\"(mid) : \"r\"((short)n));"
        "char*p=0;__asm__(\"leaq 16(%1), %0\" : \"=r\"(p) : \"r\"(p));unsigned short v=(unsigned short)(n*257+3);unsigned char hi;"
        "__asm__(\"movb %h1, %0\" : \"=q\"(hi) : \"Q\"(v));return (int)counter+small+mid+(int)(long)p+hi;}",
        "asm-output-widths", clang);
    // rsp's alignment at the template (16, as clang -O0's function bodies have it), and basic asm statements.
    check_test_program_source(test,
        "int tested(int n){long sp;__asm__ volatile(\"movq %%rsp, %0\" : \"=r\"(sp));"
        "__asm__(\"nop\");__asm__ volatile(\"nop\\n\\tnop\");return (int)(sp&15)+n;}",
        "asm-alignment", clang);
    // XGETBV is executable only with XSAVE and OSXSAVE, even on an x86-64 host.
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx) && (ecx & (3u << 26)) == (3u << 26))
    {
        check_test_program_source(test,
            "int tested(int n){unsigned lo,hi;__asm__ volatile(\"xgetbv\" : \"=a\"(lo),\"=d\"(hi) : \"c\"(0));"
            "return (int)(lo&0xff)+(int)hi+n;}", "asm-xgetbv", clang);
    }
    else
    {
        printf("  asm-xgetbv: skipped execution (XSAVE or OSXSAVE unavailable)\n");
    }
    // The x87's, MMX's and SSE's state of 512 bytes, saved and restored by fxsave and fxrstor and their 64-bit forms, in
    // aligned arrays, as main.c's test of DAZ saves it: MXCSR's control bits and its mask, which the state holds at bytes
    // 24 and 28, against stmxcsr's.
    check_test_program_source(test,
        "int tested(int n){unsigned char s[512] __attribute__((aligned(16))),t[512] __attribute__((aligned(16)));"
        "__asm__ volatile(\"fxsave %0\" : \"=m\"(s));__asm__ volatile(\"fxrstor %0\" : : \"m\"(s));"
        "__asm__ volatile(\"fxsave64 %0\" : \"=m\"(t));__asm__ volatile(\"fxrstor64 %0\" : : \"m\"(t));"
        "unsigned csr,mask,wide,now;__builtin_memcpy(&csr,s+24,4);__builtin_memcpy(&mask,s+28,4);__builtin_memcpy(&wide,t+24,4);"
        "__asm__ volatile(\"stmxcsr %0\" : \"=m\"(now));return ((csr^now)&0xffc0)==0?(int)(csr&0xffc0)+((wide^now)&0xffc0?1:2)+(int)((mask>>6)&1)*4+n:-1;}",
        "asm-fx-state", clang);
    // XMM operands of double and float.
    check_test_program_source(test,
        "int tested(int n){double d=n*0.5,e=n+0.25;__asm__(\"addpd %1, %0\" : \"+x\"(d) : \"x\"(e));float f=(float)n,g;"
        "__asm__(\"movaps %1, %0\\n\\taddps %1, %0\" : \"=&x\"(g) : \"x\"(f));return (int)(d*4)+(int)(g*2);}",
        "asm-xmm-operands", clang);
    // Operands evaluated in clang's order, each once: the outputs' lvalues (a `+` one's value loaded through its own),
    // then the inputs, in a template of instructions and in one of none.
    check_test_program_source(test,
        "int seq[16];int at;int g1,g2,g3;int*out1(void){seq[at++]=1;return &g1;}int*out2(void){seq[at++]=2;return &g2;}"
        "int*out3(void){seq[at++]=5;return &g3;}int in1(void){seq[at++]=3;return 4;}int in2(void){seq[at++]=4;return 5;}"
        "int in3(void){seq[at++]=6;return 7;}int tested(int n){at=0;g2=n;"
        "__asm__(\"movl %2, %0\\n\\taddl %3, %1\" : \"=r\"(*out1()), \"+r\"(*out2()) : \"r\"(in1()), \"r\"(in2()));int r=g1*100+g2*10;"
        "__asm__ volatile(\"\" : \"=r\"(*out3()) : \"r\"(in3()), \"m\"(*out1()));int code=0;for(int i=0;i<at;i++)code=code*10+seq[i];"
        "return code+r;}",
        "asm-operand-order", clang);
    // rdi and rsi as operands (the block's address comes in rdi, loaded last), constants sign-extended from their types
    // as clang prints them (0xffffffffu is $-1), a memory input after a register output, and an XMM matching input.
    check_test_program_source(test,
        "int tested(int n){long a=n,b=3*n,r;__asm__(\"leaq (%1,%2), %0\" : \"=r\"(r) : \"D\"(a), \"S\"(b));long m,k;"
        "__asm__(\"movq %1, %0\" : \"=r\"(m) : \"i\"(0xffffffffu));__asm__(\"movq %1, %0\" : \"=r\"(k) : \"i\"(-5));long e=n*5,d;"
        "__asm__(\"movq %1, %0\" : \"=r\"(d) : \"m\"(e));float f=(float)n,g=2.5f,h;__asm__(\"addps %1, %0\" : \"=x\"(h) : \"x\"(f), \"0\"(g));"
        "return (int)r+(int)(m>>32)*1000+(int)k*10+(int)d*7+(int)(h*2);}",
        "asm-fixed-registers-and-constants", clang);
    // Matching inputs of other widths than their outputs', in templates of no instructions and of one: a narrower one
    // zero-extended from its own width, as GCC and clang place it ("=r"(long) : "0"(-1) is 4294967295), and a wider one's
    // low bits; the inputs run below 0.
    check_test_program_source(test,
        "int tested(int n){long a;__asm__(\"\" : \"=r\"(a) : \"0\"(n));int b;__asm__(\"\" : \"=r\"(b) : \"0\"((signed char)n));"
        "long c;__asm__(\"\" : \"=r\"(c) : \"0\"((short)(n*3)));unsigned long d;__asm__(\"\" : \"=r\"(d) : \"0\"((unsigned char)(n+200)));"
        "long e;__asm__(\"nop\" : \"=r\"(e) : \"0\"(n));int t;__asm__(\"\" : \"=r\"(t) : \"0\"((long)n*0x100000001L));"
        "return (int)(a>>32)*1000+(int)a+b*3+(int)(c>>16)*7+(int)c+(int)d+(int)(e>>32)*100+t;}",
        "asm-tied-widths", clang);
    // Z's constants, which clang's backend checks by their zero-extended bits and prints sign-extended, as every constant.
    check_test_program_source(test,
        "int tested(int n){long a,b,c,d;__asm__(\"movq %1, %0\" : \"=r\"(a) : \"Z\"(0xffffffffu));__asm__(\"movq %1, %0\" : \"=r\"(b) : \"Z\"(0x80000000u));"
        "__asm__(\"movq %1, %0\" : \"=r\"(c) : \"Z\"((signed char)-1));__asm__(\"movl %1, %k0\" : \"=r\"(d) : \"Z\"((unsigned long)0xffffffff));"
        "return n+(int)(a>>32)*3+(int)(b>>40)*5+(int)c*7+(int)(d>>8);}",
        "asm-z-constants", clang);
    // A constraint of a register and bounded immediates: a constant its letters take is the immediate ("rI"(31), which only
    // %c1's displacement assembles; "cI"(3)), and another goes in the register ("rI"(40); "cI"(40), which %b1 prints %cl;
    // "Kr"(-200)), as GCC and clang place them.
    check_test_program_source(test,
        "int tested(int n){long p=n;int a,b=n,x=n,y=n;__asm__(\"leal %c1(%2), %0\" : \"=r\"(a) : \"rI\"(31), \"r\"(p));"
        "__asm__(\"addl %1, %0\" : \"+r\"(b) : \"rI\"(40));__asm__(\"shll %b1, %0\" : \"+r\"(x) : \"cI\"(3));"
        "__asm__(\"shll %b1, %0\" : \"+r\"(y) : \"cI\"(40));int c=n;__asm__(\"subl %1, %0\" : \"+r\"(c) : \"Kr\"(-200));"
        "return a+b*3+x*5+y+c*7;}",
        "asm-register-or-immediate", clang);
    // 32-bit addresses (#2339): int operands print as %eax-style registers in an address, which clang's integrated
    // assembler encodes with a 67; the assembler does too, in the template's helper, as a record has no room for it.
    check_test_program_source(test,
        "static int add(int a,int b){int r;__asm__(\"leal (%1,%2), %0\" : \"=r\"(r) : \"r\"(a), \"r\"(b));return r;}"
        "static int scaled(int a,int b){int r;__asm__(\"leal 3(%1,%2,4), %0\" : \"=r\"(r) : \"r\"(a), \"r\"(b));return r;}"
        "int tested(int n){return add(n,2*n)*7+scaled(n,5)+add(-n,n);}",
        "asm-address32", clang);
    // Naked functions: their templates, in AT&T syntax, are their whole code, with an integer constant operand.
    check_test_program_source(test,
        "__attribute__((naked)) long add3(long a,long b,long c){__asm__(\"leaq (%rdi,%rsi), %rax\\n\\taddq %rdx, %rax\\n\\tret\");}"
        "__attribute__((naked)) long seven(void){__asm__(\"movl %0, %%eax\\n\\tret\" : : \"i\"(7));}"
        "int tested(int n){return (int)add3(n,2*n,7)+(int)seven()*100;}",
        "asm-naked-att", clang);
    // A label in a naked function's template, a second entry that another function's template takes the address of.
    check_test_program_source(test,
        "static __attribute__((naked,noinline)) long twice(long x){__asm__(\"leaq (%rdi,%rdi), %rax\\n\\tret\\nthrice_entry:\\n\\tleaq (%rdi,%rdi,2), %rax\\n\\tret\\n\");}"
        "static long thrice_address(void){long a;__asm__(\"leaq thrice_entry(%%rip), %0\" : \"=r\"(a));return a;}"
        "int tested(int n){long (*f)(long)=(long (*)(long))thrice_address();return (int)(twice(n)*10+f(n));}",
        "asm-naked-entries", clang);
    // A function's template's CFI is refused by name: clang adds it to the function's own frame description.
    check_test_program_boundary_reason(test, "int tested(int n){__asm__ volatile(\"pushq %%rbx\\n\\t.cfi_adjust_cfa_offset 8\\n\\tpopq %%rbx\\n\\t.cfi_adjust_cfa_offset -8\" ::: \"memory\");return n;}",
                                       "asm-template-cfi", true, false, 0, 0, "inline assembly CFI directives in a function's template (clang adds them to the function's frame)");
    // interpreter_native_bridge's shape: a frame of its own, the flags, a string copy, SSE2's movq, the x87's load and
    // store, a call through a register and a numeric label's branch.
    check_test_program_source(test,
        "struct frame{long (*target)(long,double);long argument;double value;long result;double back;char bytes[16];char copy[16];long double ext;};"
        "_Static_assert(__builtin_offsetof(struct frame,bytes)==40,\"bytes\");"
        "_Static_assert(__builtin_offsetof(struct frame,copy)==56,\"copy\");"
        "_Static_assert(__builtin_offsetof(struct frame,ext)==80,\"ext\");"
        "static long target(long a,double d){return a*10+(long)(d*2);}"
        "static __attribute__((naked,noinline)) void bridge(struct frame* f){__asm__("
        "\"pushq %rbp\\n\\tmovq %rsp, %rbp\\n\\tpushq %r12\\n\\tmovq %rdi, %r12\\n\\tandq $-16, %rsp\\n\\tpushfq\\n\\tpopfq\\n\\tcld\\n\\t\""
        "\"leaq 40(%r12), %rsi\\n\\tleaq 56(%r12), %rdi\\n\\tmovq $16, %rcx\\n\\trep movsb\\n\\t\""
        "\"movq 8(%r12), %rdi\\n\\tmovq 16(%r12), %xmm0\\n\\tmovq %xmm0, 32(%r12)\\n\\tfldl 16(%r12)\\n\\tfstpt 80(%r12)\\n\\t\""
        "\"movq 0(%r12), %r11\\n\\tcallq *%r11\\n\\tmovq %rax, 24(%r12)\\n\\tcmpq $0, 8(%r12)\\n\\tje 1f\\n\\tincq 24(%r12)\\n1:\\n\\t\""
        "\"leaq -8(%rbp), %rsp\\n\\tpopq %r12\\n\\tpopq %rbp\\n\\tretq\\n\");}"
        "int tested(int n){struct frame f={target,n,n*0.5,0,0,{0},{0},0};for(int i=0;i<16;i++)f.bytes[i]=(char)(n+i);bridge(&f);"
        "int s=0;for(int i=0;i<16;i++)s+=f.copy[i];return (int)f.result*3+(int)(f.back*4)+s+(int)(f.ext*8);}",
        "asm-naked-bridge", clang);
    // x86_semantics_test_enter's shape, in Intel syntax: statics RIP-relative, a jump through one to another naked
    // function, whose template jumps back to the first one's label. Execute SSE2 moves on every x86-64 host;
    // assemble AVX-512 moves separately so the oracle can run without AVX-512.
    check_test_program_source(test,
        "static __attribute__((used)) long saved_rsp;"
        "static __attribute__((used)) long saved_arg;"
        "static __attribute__((used)) long continuation;"
        "static __attribute__((naked,noinline)) long enter(long x){__asm__(\".intel_syntax noprefix\\n\""
        "\"push rbx\\nmov qword ptr [rip + saved_rsp], rsp\\nmov qword ptr [rip + saved_arg], rdi\\nlea rbx, [rdi + 5]\\n\""
        "\"movdqu xmm0, xmm1\\nmovdqa xmm1, xmm0\\njmp qword ptr [rip + continuation]\\n\""
        "\"enter_return:\\nmov rax, rbx\\nadd rax, qword ptr [rip + saved_arg]\\npop rbx\\nret\\n.att_syntax prefix\\n\");}"
        "static __attribute__((used,naked,noinline)) void vector_moves(void){__asm__(\".intel_syntax noprefix\\n"
        "vmovdqu64 zmm16, zmm17\\nvmovdqa64 zmm17, zmm16\\nvzeroupper\\nret\\n.att_syntax prefix\\n\");}"
        "static __attribute__((used,naked,noinline)) void bounce(void){__asm__(\"addq $100, %rbx\\n\\tjmp enter_return\\n\");}"
        "static long bounce_address(void){long a;__asm__(\"leaq bounce(%%rip), %0\" : \"=r\"(a));return a;}"
        "int tested(int n){continuation=bounce_address();long r=enter(n);return (int)r+(saved_rsp!=0)+(int)(saved_arg-n);}",
        "asm-naked-intel", clang);
    // The original wide-register path also executes where CPU and OS save-state support permit AVX-512F. Its
    // compilation, constraints, lowering, encoding, oracle compilation and link remain required on every host.
    const X86Target native = x86_target_native();
    const bool wide_native = (native.features & BUSTER_X86_FEATURE(AVX512F)) != 0;
    if (!wide_native)
    {
        printf("test-checker: asm-naked-intel-avx512: AVX-512F unavailable; checking compile/link without native execution\n");
    }
    check_test_program_source_native(test,
        "static __attribute__((used)) long saved_rsp;"
        "static __attribute__((used)) long saved_arg;"
        "static __attribute__((used)) long continuation;"
        "static __attribute__((naked,noinline)) long enter(long x){__asm__(\".intel_syntax noprefix\\n\""
        "\"push rbx\\nmov qword ptr [rip + saved_rsp], rsp\\nmov qword ptr [rip + saved_arg], rdi\\nlea rbx, [rdi + 5]\\n\""
        "\"vmovdqu64 zmm16, zmm17\\nvmovdqa64 zmm17, zmm16\\njmp qword ptr [rip + continuation]\\n\""
        "\"enter_return:\\nmov rax, rbx\\nadd rax, qword ptr [rip + saved_arg]\\nvzeroupper\\npop rbx\\nret\\n.att_syntax prefix\\n\");}"
        "static __attribute__((used,naked,noinline)) void bounce(void){__asm__(\"addq $100, %rbx\\n\\tjmp enter_return\\n\");}"
        "static long bounce_address(void){long a;__asm__(\"leaq bounce(%%rip), %0\" : \"=r\"(a));return a;}"
        "int tested(int n){continuation=bounce_address();long r=enter(n);return (int)r+(saved_rsp!=0)+(int)(saved_arg-n);}",
        "asm-naked-intel-avx512", clang, wide_native);
    // The semantics harness saves all eight masks at offsets 2240..2296. Compile/link this regression on every
    // host; execute only with the AVX-512BW state support KMOVQ requires, including k0 as an ordinary operand.
    const bool masks_native = wide_native && (native.features & BUSTER_X86_FEATURE(AVX512BW)) != 0;
    if (!masks_native)
    {
        printf("test-checker: asm-naked-kmov: AVX-512BW unavailable; checking compile/link without native execution\n");
    }
    check_test_program_source_native(test,
        "static __attribute__((naked,noinline,target(\"avx512f,avx512bw\"))) void copy_masks(const unsigned long long* p,unsigned long long* q){"
        "__asm__(\".intel_syntax noprefix\\n"
        "kmovq k0,qword ptr [rdi+2240]\\nkmovq k1,qword ptr [rdi+2248]\\nkmovq k2,qword ptr [rdi+2256]\\nkmovq k3,qword ptr [rdi+2264]\\n"
        "kmovq k4,qword ptr [rdi+2272]\\nkmovq k5,qword ptr [rdi+2280]\\nkmovq k6,qword ptr [rdi+2288]\\nkmovq k7,qword ptr [rdi+2296]\\n"
        "kmovq qword ptr [rsi],k0\\nkmovq qword ptr [rsi+8],k1\\nkmovq qword ptr [rsi+16],k2\\nkmovq qword ptr [rsi+24],k3\\n"
        "kmovq qword ptr [rsi+32],k4\\nkmovq qword ptr [rsi+40],k5\\nkmovq qword ptr [rsi+48],k6\\nkmovq qword ptr [rsi+56],k7\\n"
        "ret\\n.att_syntax prefix\\n\");}"
        "int tested(int n){unsigned long long p[288],q[8];for(int i=0;i<8;i++){p[280+i]=((unsigned long long)(unsigned)n<<32)^"
        "(0xfedcba9876543210ULL+(unsigned)i*0x0101010101010101ULL);q[i]=0;}copy_masks(p,q);int r=0;"
        "for(int i=0;i<8;i++)r+=q[i]==p[280+i];return r+n;}",
        "asm-naked-kmov", clang, masks_native);
    // Opmask operands (#1846): k's and Yk's registers of a short (kmovw), a byte, an unsigned (kmovd) and an unsigned long
    // long (kmovq), a k clobber the operands avoid, a matching input, and an opmask input into a general output, in
    // functions whose target attributes have the moves' features: compiled and linked everywhere, run where the host has
    // AVX-512BW. Without the features, each width is refused by name, as clang can't place one.
    if (!masks_native)
    {
        printf("test-checker: asm-opmask: AVX-512BW unavailable; checking compile/link without native execution\n");
    }
    check_test_program_source_native(test,
        "static __attribute__((target(\"avx512f\"))) unsigned short mask_not(unsigned short m){unsigned short r;__asm__(\"knotw %1, %0\" : \"=k\"(r) : \"k\"(m));return r;}"
        "static __attribute__((target(\"avx512f\"))) unsigned char mask_byte(unsigned char m){__asm__(\"knotw %0, %0\" : \"+k\"(m));return m;}"
        "static __attribute__((target(\"avx512f,avx512bw\"))) unsigned mask_add(unsigned a, unsigned b){__asm__(\"kaddd %1, %0, %0\" : \"+k\"(a) : \"Yk\"(b) : \"k1\");return a;}"
        "static __attribute__((target(\"avx512f,avx512bw\"))) unsigned long long mask_shift(unsigned long long m){unsigned long long r;__asm__(\"kshiftlq $3, %1, %0\" : \"=Yk\"(r) : \"k\"(m));return r;}"
        "static __attribute__((target(\"avx512f\"))) unsigned short mask_tied(unsigned short m){unsigned short r;__asm__(\"kshiftrw $1, %0, %0\" : \"=k\"(r) : \"0\"(m));return r;}"
        "static __attribute__((target(\"avx512f\"))) unsigned short mask_general(unsigned short m){unsigned r;__asm__(\"kmovw %1, %0\" : \"=r\"(r) : \"k\"(m));return (unsigned short)r;}"
        "int tested(int n){unsigned long long wide=((unsigned long long)(unsigned)n*2654435761u<<17)^0x123456789abcdefull;"
        "return mask_not((unsigned short)(n*977))+mask_byte((unsigned char)(n*31))*3+(int)mask_add((unsigned)n*40503u,0x10001u*(unsigned)n)+(int)(mask_shift(wide)>>7)+mask_tied((unsigned short)(n*12345))*5+mask_general((unsigned short)(n+77))*7;}",
        "asm-opmask", clang, masks_native);
    // Explicit register variables in the other classes' registers (#1846, the owner's decision on #1447): mm2, k3 and
    // st, each the register its template names explicitly too (the MMX one copied out through mm2, the opmask one through
    // k3), whatever the constraint's letter, as clang's code places them.
    check_test_program_source(test,
        "static long mmx_variable(long x){register long m __asm__(\"mm2\")=x;long o;__asm__(\"paddd %1, %1\\n\\tmovq %%mm2, %0\" : \"=r\"(o), \"+y\"(m));long r=o+m;__asm__ volatile(\"emms\");return r;}"
        "static long double x87_variable(long double x){register long double s __asm__(\"st\")=x;__asm__(\"fsqrt\" : \"+t\"(s));return s;}"
        "int tested(int n){return (int)mmx_variable(n*3+1)+(int)(x87_variable((long double)(n*n))*4);}",
        "asm-register-variable-mmx-x87", clang);
    check_test_program_source_native(test,
        "static __attribute__((target(\"avx512f\"))) unsigned short mask_variable(unsigned short x){register unsigned short k __asm__(\"k3\")=x;unsigned o;"
        "__asm__(\"knotw %1, %1\\n\\tkmovw %%k3, %0\" : \"=r\"(o), \"+k\"(k));return (unsigned short)(o+k);}"
        "int tested(int n){return mask_variable((unsigned short)(n*977));}",
        "asm-register-variable-opmask", clang, masks_native);
    check_test_program_boundary_reason(test, "int tested(int n){unsigned short m=(unsigned short)n;__asm__(\"knotw %0, %0\" : \"+k\"(m));return m;}",
                                       "asm-opmask-baseline", true, false, 0, 1, "inline assembly opmask operands without AVX-512F");
    check_test_program_boundary_reason(test, "__attribute__((target(\"avx512f\"))) int tested(int n){unsigned m=(unsigned)n;__asm__(\"knotd %0, %0\" : \"+k\"(m));return (int)m;}",
                                       "asm-opmask-wide", true, false, 0, 1, "inline assembly opmask operands of 4 or 8 bytes without AVX-512BW");
    check_test_program_boundary_reason(test, "__attribute__((target(\"avx512f\"))) int tested(int n){unsigned short m=(unsigned short)n;__asm__(\"kmovw %w0, %0\" : \"+k\"(m));return m;}",
                                       "asm-opmask-modifier", true, false, 0, 1, "this inline assembly operand modifier");
    // Vectors in vector registers' constraints in templates of no instructions, main.c's compiler barriers (c_lexer.c's
    // and x86_64_encoder.c's `__asm__("" : "+v"(x))`): a `+` operand keeps its value, an output through a call's pointer
    // is evaluated once, and an input's value is evaluated once and dropped, a call's that returns a vector and one loaded
    // through a call's pointer in a statement of no outputs, both calls counting their evaluations (#773's review); for
    // x86-64-v4, run where the host has its features.
    X86Target vectors = { 0 };
    const bool vectors_parsed = x86_target_parse("x86-64-v4", strlen("x86-64-v4"), &vectors);
    check_test_expect(test, vectors_parsed, "the vector barriers' target, x86-64-v4");
    static const char asm_vector_types[] =
        "typedef long long v8di __attribute__((vector_size(64)));typedef int v8si __attribute__((vector_size(32)));"
        "typedef float v4sf __attribute__((vector_size(16)));";
    static const char asm_vector_program[] =
        "int count;int inputs;v8di saved[3];static v8di* next(void){count+=1;return &saved[count%3];}"
        "static v8di twice(v8di x){inputs+=1;return x+x;}static v8di* held(void){inputs+=10;return &saved[2];}"
        "int tested(int n){v8di a={n,n+1,n+2,n+3,n+4,n+5,n+6,n+7};__asm__(\"\" : \"+v\"(a));v8si b={n*2,1,2,3,4,5,6,7};"
        "__asm__ volatile(\"\" : \"+x\"(b) : \"v\"(twice(a)));__asm__ volatile(\"\" : : \"x\"(*held()));"
        "v4sf c={1.5f,(float)n,2.5f,3.5f};__asm__(\"# no instructions\" : \"+v\"(c) : : \"memory\");"
        "saved[1]=a;__asm__(\"\" : \"+v\"(*next()));long long s=0;for(int i=0;i<8;i++)s+=a[i]*(i+1)+saved[1][i]+b[i];"
        "return (int)(s+(long long)c[1]*5+count*7+inputs*11);}";
    if (vectors_parsed)
    {
        const bool vectors_native = (native.features & vectors.features) == vectors.features;
        if (!vectors_native)
        {
            printf("  asm-vector-barriers: native execution skipped (the host lacks x86-64-v4's features or their saved state)\n");
        }
        char source[2048];
        const int length = snprintf(source, sizeof(source), "%s%s", asm_vector_types, asm_vector_program);
        check_test_expect(test, length > 0 && (size_t)length < sizeof(source), "the vector barriers' source fits its buffer");
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64-v4";
        check_test_program_source_native_limits(test, source, "asm-vector-barriers", clang, false, -1, 0, &vectors, vectors_native);
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64";
    }
    // A naked function whose attribute isn't on its first declaration (#565's review): an unannotated prototype before a
    // naked definition that returns by its own ret, and a naked redeclaration between a prototype and an unannotated
    // definition, both naked as clang takes them; and a naked redeclaration after a definition, which clang ignores with
    // a warning: an ordinary function.
    check_test_program_source(test,
        "static long first_plain(long);"
        "static __attribute__((naked,noinline)) long first_plain(long x){__asm__(\"leaq 1(%rdi), %rax\\n\\tret\\n\");}"
        "static long middle_naked(long);"
        "static __attribute__((naked)) long middle_naked(long);"
        "static __attribute__((noinline)) long middle_naked(long x){__asm__(\"leaq 2(%rdi), %rax\\n\\tret\\n\");}"
        "static long after_definition(long x){return x+3;}"
        "static __attribute__((naked)) long after_definition(long);"
        "int tested(int n){return (int)(first_plain(n)*7+middle_naked(n)*5+after_definition(n));}",
        "asm-naked-redeclared", clang);
    // Templates that name symbols: the harness's function and data, the unit's static function and data, and a .L label
    // one statement's template defines and another's takes the address of.
    check_test_program_source(test,
        "extern int external_data;int external_helper(int);"
        "static __attribute__((used)) int counter;"
        "static __attribute__((used)) int bump(int x){return x+1;}"
        "int tested(int n){int r,s;long here,there;"
        "__asm__ volatile(\"movl %1, %%edi\\n\\tcall external_helper\\n\\taddl external_data(%%rip), %%eax\\n\\tmovl %%eax, %0\" : \"=r\"(r) : \"r\"(n)"
        ": \"rax\",\"rcx\",\"rdx\",\"rsi\",\"rdi\",\"r8\",\"r9\",\"r10\",\"r11\",\"xmm0\",\"xmm1\",\"xmm2\",\"xmm3\",\"xmm4\",\"xmm5\",\"xmm6\",\"xmm7\",\"xmm8\",\"xmm9\",\"xmm10\",\"xmm11\",\"xmm12\",\"xmm13\",\"xmm14\",\"xmm15\",\"memory\",\"cc\");"
        "__asm__ volatile(\"movl %1, %%edi\\n\\tcall bump\\n\\taddl %%eax, counter(%%rip)\\n\\tmovl counter(%%rip), %0\" : \"=r\"(s) : \"r\"(n)"
        ": \"rax\",\"rcx\",\"rdx\",\"rsi\",\"rdi\",\"r8\",\"r9\",\"r10\",\"r11\",\"xmm0\",\"xmm1\",\"xmm2\",\"xmm3\",\"xmm4\",\"xmm5\",\"xmm6\",\"xmm7\",\"xmm8\",\"xmm9\",\"xmm10\",\"xmm11\",\"xmm12\",\"xmm13\",\"xmm14\",\"xmm15\",\"memory\",\"cc\");"
        "__asm__(\"leaq .Lshared_point(%%rip), %0\\n.Lshared_point:\\n\\tnop\" : \"=r\"(here));"
        "__asm__(\"leaq .Lshared_point(%%rip), %0\" : \"=r\"(there));"
        "return r*7+s+(here==there)+external_helper(0);}",
        "asm-symbol-references", clang);
    // Numeric labels and `.` as data (#1845), and a loop's branch to a numeric label: the template assembled whole, as
    // a template of directives is, its labels' distances as clang's.
    check_test_program_source(test,
        "int tested(int n){long a,b,c,d;int e=n;"
        "__asm__(\"leaq 1f(%%rip), %0\\n\\tleaq 2f(%%rip), %1\\n1:\\tnop\\n2:\\tsubq %0, %1\" : \"=&r\"(a), \"=&r\"(b));"
        "__asm__(\"leaq .(%%rip), %0\\n\\tleaq .(%%rip), %1\\n\\tsubq %0, %1\" : \"=&r\"(c), \"=&r\"(d));"
        "__asm__(\"movl $3, %%ecx\\n1:\\tincl %0\\n\\tloop 1b\" : \"+r\"(e) : : \"rcx\");"
        "return (int)(b*1000+d*100)+e+(a!=0)+(c!=0);}",
        "asm-numeric-data", clang);
    // A label inside a helper's template at its exact record: another statement's template calls the snippet it marks,
    // which the first template jumps over.
    check_test_program_source(test,
        "extern int external_data;int external_helper(int);int tested(int n){int v;"
        "__asm__ volatile(\"jmp 1f\\n.Lforty_two:\\n\\tmovl $42, %%eax\\n\\taddl %%edi, %%eax\\n\\tret\\n1:\" ::: \"rax\");"
        "__asm__ volatile(\"movl %1, %%edi\\n\\tcall .Lforty_two\\n\\tmovl %%eax, %0\" : \"=r\"(v) : \"r\"(n)"
        ": \"rax\",\"rcx\",\"rdx\",\"rsi\",\"rdi\",\"r8\",\"r9\",\"r10\",\"r11\",\"memory\",\"cc\");"
        "return v+external_helper(0);}",
        "asm-helper-labels", clang);
    // A naked function's references beside a body's call of a runtime function (__divti3), in a unit of so few symbols
    // that its names' indices fall among the runtime's provisional symbols, which aren't theirs.
    check_test_program_source(test,
        "static __attribute__((naked,noinline)) long pick(long x){__asm__("
        "\"testq %rdi, %rdi\\n\\tjz l0\\n\\tcmpq $1, %rdi\\n\\tje l1\\n\\tcmpq $2, %rdi\\n\\tje l2\\n\\tcmpq $3, %rdi\\n\\tje l3\\n\\tjmp l4\\n\""
        "\"l0:\\n\\tmovq $10, %rax\\n\\tret\\nl1:\\n\\tmovq $11, %rax\\n\\tret\\nl2:\\n\\tmovq $12, %rax\\n\\tret\\nl3:\\n\\tmovq $13, %rax\\n\\tret\\n\""
        "\"l4:\\n\\tleaq l5(%rip), %rax\\n\\tleaq l0(%rip), %rdx\\n\\tsubq %rdx, %rax\\n\\tret\\nl5:\\n\");}"
        "int tested(int n){__int128 w=(__int128)n*1000000007;long q=(long)(w/(__int128)(n|1));return (int)pick(n&7)+(int)(q&0xff);}",
        "asm-naked-runtime", clang);
    // A jump table beside the templates' names, in one unit: the function whose switch takes a table has templates that
    // call the unit's static function, update its static and take the address of a label a naked function's template
    // defines. The names' own symbols come after the tables' and their labels' (CheckProgramLowered.case_symbol_base),
    // and neither's references are the other's to rewrite.
    static const char* const asm_tables =
        "static __attribute__((used)) int bump(int x){return x+1;}"
        "static int total;"
        "static __attribute__((naked,noinline)) int relay(int x){__asm__(\"addl $2, %edi\\nrelay_tail:\\n\\tjmp bump\\n\");}"
        "static int dense(int x){int r;long tail,start;"
        "switch(x){case 0:r=3;break;case 1:r=5;break;case 2:r=7;break;case 3:r=11;break;case 5:r=13;break;default:r=-1;}"
        "__asm__ volatile(\"movl %1, %%edi\\n\\tcall bump\\n\\taddl %%eax, total(%%rip)\\n\\tmovl %%eax, %0\" : \"=r\"(r) : \"r\"(r)"
        ": \"rax\",\"rcx\",\"rdx\",\"rsi\",\"rdi\",\"r8\",\"r9\",\"r10\",\"r11\",\"xmm0\",\"xmm1\",\"xmm2\",\"xmm3\",\"xmm4\",\"xmm5\",\"xmm6\",\"xmm7\",\"xmm8\",\"xmm9\",\"xmm10\",\"xmm11\",\"xmm12\",\"xmm13\",\"xmm14\",\"xmm15\",\"memory\",\"cc\");"
        "__asm__(\"leaq relay_tail(%%rip), %0\" : \"=r\"(tail));"
        "__asm__(\"leaq relay(%%rip), %0\" : \"=r\"(start));"
        "return r+(int)(tail-start)+bump(0);}"
        "int tested(int n){unsigned s=0;for(int i=-3;i<9;i++)s=s*7+(unsigned)dense(i+n)+(unsigned)relay(i);return (int)s+total;}";
    check_test_program_source(test, asm_tables, "asm-names-switch-tables", clang);
    check_test_program_tables(test, asm_tables, "asm-names-switch-tables", 1, 6);
    // Both compilers take the flag-output path, which __GCC_ASM_FLAG_OUTPUTS__ advertises.
    check_test_program_source(test,
        "int tested(int n){int z;\n"
        "#ifdef __GCC_ASM_FLAG_OUTPUTS__\n"
        "__asm__(\"testl %1,%1\" : \"=@ccz\"(z) : \"r\"(n) : \"cc\");\n"
        "#else\n"
        "__asm__(\"testl %1,%1; sete %b0; movzbl %b0,%0\" : \"=r\"(z) : \"r\"(n) : \"cc\");\n"
        "#endif\n"
        "return z;}", "asm-flag-output-fallback", clang);
    check_test_program_asm_flags(test, clang);
    check_test_program_asm_goto(test, clang);
    // The forms of musl's x86_64 atomics (arch/x86_64/atomic_arch.h), in functions of this test's own: lock prefixes as
    // statements of their own before cmpxchg, xadd, and, or, xor, inc and dec on memory outputs (an "=m" one beside
    // an "m" input of its object, and "+m"), xchg and `lock xchg` (#1846), a store fenced by a locked or of the stack, pause, the bit scans
    // (of operands never zero, whose result the scans leave undefined), and a load of the thread pointer through %fs.
    // hlt is compiled, assembled and linked, never run.
    check_test_program_source(test,
        "static int swap_if(volatile int* slot, int expected, int wanted){int seen;__asm__ __volatile__(\"lock ; cmpxchg %3, %1\" : \"=a\"(seen), \"=m\"(*slot) : \"a\"(expected), \"r\"(wanted) : \"memory\");return seen;}"
        "static void* swap_pointer_if(void* volatile* slot, void* expected, void* wanted){__asm__(\"lock ; cmpxchg %3, %1\" : \"=a\"(expected), \"=m\"(*slot) : \"a\"(expected), \"r\"(wanted) : \"memory\");return expected;}"
        "static long swap_long_if(volatile long* slot, long expected, long wanted){__asm__ __volatile__(\"lock ; cmpxchgq %2, %1\" : \"+a\"(expected), \"+m\"(*slot) : \"r\"(wanted) : \"memory\", \"cc\");return expected;}"
        "static int trade(volatile int* slot, int value){int old;__asm__ __volatile__(\"xchg %0, %1\" : \"=r\"(old), \"=m\"(*slot) : \"0\"(value) : \"memory\");return old;}"
        "static long locked_trade(volatile long* slot, long value){__asm__ __volatile__(\"lock xchg %0, %1\" : \"+r\"(value), \"+m\"(*slot) : : \"memory\");return value;}"
        "static int add_returning_old(volatile int* slot, int amount){int old;__asm__ __volatile__(\"lock ; xadd %0, %1\" : \"=r\"(old), \"=m\"(*slot) : \"0\"(amount) : \"memory\");return old;}"
        "static long add_long_returning_old(volatile long* slot, long amount){__asm__ __volatile__(\"lock ; xadd %0, %1\" : \"+r\"(amount), \"+m\"(*slot) : : \"memory\", \"cc\");return amount;}"
        "static void keep_bits(volatile unsigned* slot, unsigned bits){__asm__ __volatile__(\"lock ; and %1, %0\" : \"=m\"(*slot) : \"r\"(bits) : \"memory\");}"
        "static void set_bits(volatile unsigned* slot, unsigned bits){__asm__ __volatile__(\"lock ; or %1, %0\" : \"=m\"(*slot) : \"r\"(bits) : \"memory\");}"
        "static void keep_wide_bits(volatile unsigned long* slot, unsigned long bits){__asm__ __volatile(\"lock ; and %1, %0\" : \"=m\"(*slot) : \"r\"(bits) : \"memory\");}"
        "static void set_wide_bits(volatile unsigned long* slot, unsigned long bits){__asm__ __volatile__(\"lock ; or %1, %0\" : \"+m\"(*slot) : \"r\"(bits) : \"memory\");}"
        "static void flip_wide_bits(volatile unsigned long* slot, unsigned long bits){__asm__ __volatile__(\"lock ; xor %1, %0\" : \"+m\"(*slot) : \"r\"(bits) : \"memory\");}"
        "static void count_up(volatile int* slot){__asm__ __volatile__(\"lock ; incl %0\" : \"=m\"(*slot) : \"m\"(*slot) : \"memory\");}"
        "static void count_down(volatile int* slot){__asm__ __volatile__(\"lock ; decl %0\" : \"=m\"(*slot) : \"m\"(*slot) : \"memory\");}"
        "static void count_down_long(volatile long* slot){__asm__ __volatile__(\"lock ; decq %0\" : \"+m\"(*slot) : : \"memory\");}"
        "static void fenced_store(volatile long* slot, long value){__asm__ __volatile__(\"movq %1, %0 ; lock ; orl $0,(%%rsp)\" : \"=m\"(*slot) : \"r\"(value) : \"memory\");}"
        "static void wait_a_little(void){__asm__ __volatile__(\"pause\" : : : \"memory\");}"
        "static int lowest_set(unsigned long bits){__asm__(\"bsf %1,%0\" : \"=r\"(bits) : \"r\"(bits));return (int)bits;}"
        "static int highest_set(unsigned long bits){__asm__(\"bsr %1,%0\" : \"=r\"(bits) : \"r\"(bits));return (int)bits;}"
        "static unsigned long thread_pointer(void){unsigned long here;__asm__(\"mov %%fs:0,%0\" : \"=r\"(here));return here;}"
        "void stop_here(void){__asm__ __volatile__(\"hlt\" : : : \"memory\");}"
        "int tested(int n){volatile int word=n*7;int r=swap_if(&word,n*7,n+3);r=r*5+swap_if(&word,-1,9)+word*3;"
        "void* volatile cell=&r;void* before=swap_pointer_if(&cell,&r,(void*)&word);r+=(before==(void*)&r)*2+(cell==(void*)&word)*4;"
        "volatile long big=(long)n<<36;long previous=swap_long_if(&big,(long)n<<36,-n);r+=(int)(previous>>36)+(int)big;"
        "r=r*3+trade(&word,n-1)+word;volatile long cell64=(long)n<<33;r+=(int)(locked_trade(&cell64,n+2)>>33)+(int)cell64;r=r*7+add_returning_old(&word,n*5)+word;volatile long total=1000;r+=(int)add_long_returning_old(&total,n)+(int)total;"
        "volatile unsigned mask=0xf0f0u+n;keep_bits(&mask,0x3c3cu);set_bits(&mask,(unsigned)n<<8);"
        "volatile unsigned long flags=(unsigned long)n*0x100000001ul;keep_wide_bits(&flags,0xffff0000fffful);set_wide_bits(&flags,(unsigned long)n<<44);flip_wide_bits(&flags,0x11);"
        "volatile int tally=n;count_up(&tally);count_up(&tally);count_down(&tally);volatile long countdown=n*4;count_down_long(&countdown);"
        "volatile long stored=0;fenced_store(&stored,(long)r*2);wait_a_little();unsigned long probe=((unsigned long)n*2654435761u+1)<<(n&15);"
        "return r+(int)mask*11+(int)(flags>>20)+(int)flags+tally*13+(int)countdown*17+(int)stored+lowest_set(probe)*19+highest_set(probe)*23+"
        "(thread_pointer()!=0);}",
        "asm-lock-prefix-statements", clang);
    // The forms of musl's x86_64 math (src/math/x86_64), in functions of this test's own: sqrtsd and sqrtss, cvtsd2si and
    // cvtss2si into 64- and 32-bit registers under MXCSR's rounding, and a magnitude's mask, all ones (pcmpeqd) shifted
    // right (psrlq, psrld), and-ed in (andps).
    check_test_program_source(test,
        "static double root(double v){double r;__asm__(\"sqrtsd %1, %0\" : \"=x\"(r) : \"x\"(v));return r;}"
        "static float root_float(float v){float r;__asm__(\"sqrtss %1, %0\" : \"=x\"(r) : \"x\"(v));return r;}"
        "static long to_long(double v){long r;__asm__(\"cvtsd2si %1, %0\" : \"=r\"(r) : \"x\"(v));return r;}"
        "static long float_to_long(float v){long r;__asm__(\"cvtss2si %1, %0\" : \"=r\"(r) : \"x\"(v));return r;}"
        "static int to_int(double v){int r;__asm__(\"cvtsd2si %1, %0\" : \"=r\"(r) : \"x\"(v));return r;}"
        "static int float_to_int(float v){int r;__asm__(\"cvtss2si %1, %0\" : \"=r\"(r) : \"x\"(v));return r;}"
        "static double magnitude_mask(void){double m;__asm__(\"pcmpeqd %0, %0\" : \"=x\"(m));__asm__(\"psrlq   $1, %0\" : \"+x\"(m));return m;}"
        "static double magnitude(double v){double m=magnitude_mask();__asm__(\"andps   %1, %0\" : \"+x\"(v) : \"x\"(m));return v;}"
        "static float float_magnitude_mask(void){float m;__asm__(\"pcmpeqd %0, %0\" : \"=x\"(m));__asm__(\"psrld   $1, %0\" : \"+x\"(m));return m;}"
        "static float float_magnitude(float v){float m=float_magnitude_mask();__asm__(\"andps   %1, %0\" : \"+x\"(v) : \"x\"(m));return v;}"
        "int tested(int n){double d=n*0.625-3.5;float f=(float)n*-0.875f+1.5f;double a=magnitude(d);float b=float_magnitude(f);"
        "return (int)(root(a)*1000)+(int)(root_float(b)*100)*3+(int)(to_long(d*1e11/9)%100000)+(int)float_to_long(f*5)*5+to_int(d*27)*7+float_to_int(f*19)*11+"
        "(int)(a*8)+(int)(b*4)*13;}",
        "asm-sse-scalar-math", clang);
    // dr_flac's lzcnt, `rep; bsr` in GCC's dialect alternatives (the AT&T one, `bsrq %1, %0`), and tzcnt so: their bytes
    // (F3 0F BD and BC), which run as the scans where the processor has no LZCNT or BMI (of operands never zero, alike).
    check_test_program_source(test,
        "static unsigned long clz(unsigned long x){unsigned long r;__asm__ __volatile__(\"rep; bsr{q %1, %0| %0, %1}\" : \"=r\"(r) : \"r\"(x) : \"cc\");return r;}"
        "static unsigned clz32(unsigned x){unsigned r;__asm__ __volatile__(\"rep; bsr{l %1, %0| %0, %1}\" : \"=r\"(r) : \"r\"(x) : \"cc\");return r;}"
        "static unsigned long ctz(unsigned long x){unsigned long r;__asm__ __volatile__(\"rep\\n\\tbsfq %1, %0\" : \"=r\"(r) : \"r\"(x) : \"cc\");return r;}"
        "int tested(int n){unsigned long x=(unsigned long)(n*n*1000003+1)<<(n&15);return (int)clz(x)*100+(int)clz32((unsigned)x|1)*10+(int)ctz(x);}",
        "asm-rep-bsr", clang);
    // FMA3's scalar forms, as musl's fma and fmaf take them where the target has FMA, in functions of this test's own:
    // compiled and linked everywhere, run where the host has FMA.
    const bool fused_native = (native.features & BUSTER_X86_FEATURE(FMA)) != 0;
    if (!fused_native)
    {
        printf("test-checker: asm-fma-scalar: FMA unavailable; checking compile/link without native execution\n");
    }
    check_test_program_source_native(test,
        "static double fused(double a,double b,double c){__asm__(\"vfmadd132sd %1, %2, %0\" : \"+x\"(a) : \"x\"(b), \"x\"(c));return a;}"
        "static float fused_float(float a,float b,float c){__asm__(\"vfmadd132ss %1, %2, %0\" : \"+x\"(a) : \"x\"(b), \"x\"(c));return a;}"
        "int tested(int n){double x=n*0.3+1/7.0,y=n-0.4,z=3e-17*n;float u=n*0.7f+1/7.0f,v=n-0.4f,w=3e-8f*n;"
        "double r=fused(x,y,z)-x*y;float s=fused_float(u,v,w)-u*v;return (int)(r*1e18)+(int)(s*1e9f)*7+(int)fused(y,y,x)*3;}",
        "asm-fma-scalar", clang, fused_native);
    // Explicit register variables as operands, in the shape of musl's syscall wrappers (arch/x86_64/syscall_arch.h), in
    // functions of this test's own: the fourth to sixth arguments in r10, r8 and r9 (`register long fourth __asm__("r10")
    // = d;` and "r"(fourth)), beside the fixed registers of the others. getppid, getuid, prctl's PR_GET_NAME,
    // rt_sigprocmask's mask and two pages mapped, written and unmapped, each the same in clang's build in the same
    // process.
    check_test_program_source(test,
        "static long call0(long number){long result;__asm__ __volatile__(\"syscall\" : \"=a\"(result) : \"0\"(number) : \"rcx\", \"r11\", \"memory\");return result;}"
        "static long call2(long number, long first, long second){long result;__asm__ __volatile__(\"syscall\" : \"=a\"(result) : \"a\"(number), \"D\"(first),"
        " \"S\"(second) : \"rcx\", \"r11\", \"memory\");return result;}"
        "static long call4(long number, long a, long b, long c, long d){long result;register long fourth __asm__(\"r10\") = d;"
        "__asm__ __volatile__(\"syscall\" : \"=a\"(result) : \"a\"(number), \"D\"(a), \"S\"(b), \"d\"(c), \"r\"(fourth) : \"rcx\", \"r11\", \"memory\");return result;}"
        "static long call6(long number, long a, long b, long c, long d, long e, long f){long result;register long fourth __asm__(\"r10\") = d;"
        "register long fifth __asm__(\"r8\") = e;register long sixth __asm__(\"r9\") = f;__asm__ __volatile__(\"syscall\" : \"=a\"(result) : \"a\"(number), \"D\"(a),"
        " \"S\"(b), \"d\"(c), \"r\"(fourth), \"r\"(fifth), \"r\"(sixth) : \"rcx\", \"r11\", \"memory\");return result;}"
        "int tested(int n){long parent=call0(110);long user=call0(102);char name[16]={0};long named=call2(157,16,(long)name);"
        "unsigned long old_mask[2]={0,0};long masked=call4(14,0,0,(long)old_mask,8);long page=call6(9,0,8192,3,0x22,-1,0);"
        "if(page>0)((volatile char*)page)[4096]=(char)n;long gone=call2(11,page,8192);"
        "return (parent>0)+(user>=0)*2+(named==0)*3+(name[0]!=0)*5+(int)masked*7+(page>0&&(page&4095)==0)*11+(int)gone*13+n;}",
        "asm-register-variables-syscalls", clang);
    // Each shape of a register variable's operand, as clang places it: an input in the variable's register whatever its
    // letters ("a"), through a cast that keeps its width, by a 32-bit name of a 64-bit register and as an int's 32-bit
    // name; outputs in their variables' registers, a memory output's among them; a `+` operand in a callee-saved one; a
    // matching input in its variable's register, not its output's; an XMM register's; and a template of no instructions.
    check_test_program_source(test,
        "int tested(int n){register long a __asm__(\"r10\")=n*3;long o1;__asm__(\"movq %%r10, %0\" : \"=r\"(o1) : \"a\"(a));"
        "register unsigned long b __asm__(\"r9\")=(unsigned long)n<<33;long o2;__asm__(\"movq %%r9, %0\" : \"=r\"(o2) : \"r\"((long)b));"
        "register long c __asm__(\"ebx\")=n-7;long o3;__asm__(\"movq %%rbx, %0\" : \"=r\"(o3) : \"r\"(c));"
        "register int d __asm__(\"r12\")=n*5;int o4;__asm__(\"movl %1, %0\" : \"=r\"(o4) : \"r\"(d));"
        "register long e __asm__(\"r11\");__asm__(\"movq $42, %%r11\" : \"=r\"(e));register long f __asm__(\"r8\");__asm__(\"movq $7, %%r8\" : \"=m\"(f));"
        "register long g __asm__(\"rbx\")=n;__asm__(\"addq $5, %%rbx\" : \"+r\"(g));"
        "register long h __asm__(\"r13\")=n*9;long o5;__asm__(\"movq %%r13, %0\" : \"=r\"(o5) : \"0\"(h));"
        "register double x __asm__(\"xmm5\")=n*0.5;double o6;__asm__(\"movaps %%xmm5, %0\" : \"=x\"(o6) : \"x\"(x));"
        "register long i __asm__(\"r10\")=n+100;__asm__(\"\" : \"+r\"(i));"
        "return (int)o1+(int)(o2>>33)*3+(int)o3*5+o4*7+(int)e+(int)f*11+(int)g*13+(int)o5*17+(int)(o6*4)*19+(int)i*23;}",
        "asm-register-variables", clang);
    // Register variables in high byte registers (#1846), loaded and stored by their names and printed by them (by the
    // register's other names under a modifier): a `+` one, an input by its modifiers, an output, a matching input in
    // its output's, and a short in "ah", which clang's code takes as ax.
    check_test_program_source(test,
        "static int bumped(int n){register unsigned char c __asm__(\"ah\")=(unsigned char)n;__asm__(\"incb %0\" : \"+r\"(c));return c;}"
        "static int named(int n){register unsigned char c __asm__(\"bh\")=(unsigned char)n;int r;__asm__(\"movzbl %1, %0\\n\\taddl $3, %0 # %h1 %b1 %k1\" : \"=r\"(r) : \"q\"(c));return r;}"
        "static int out_high(int n){register signed char c __asm__(\"dh\");__asm__(\"movb %1, %0\\n\\tnegb %0\" : \"=r\"(c) : \"r\"((signed char)n));return c;}"
        "static int matched_high(int n){register unsigned char c __asm__(\"ch\");__asm__(\"shlb $1, %0\" : \"=r\"(c) : \"0\"((unsigned char)(n+9)));return c;}"
        "static int wider(int n){register short s __asm__(\"ah\")=(short)n;__asm__(\"addw $300, %0\" : \"+r\"(s));return s;}"
        "int tested(int n){return bumped(n*37)+named(n*11)*3+out_high(n*5)*5+matched_high(n)*7+wider(n*1000)*11;}",
        "asm-register-variables-high", clang);
    // And in XMM16 to XMM31 (#1846), by EVEX's moves, in functions whose target attributes have AVX-512F: compiled and
    // linked everywhere, run where the host has it.
    check_test_program_source_native(test,
        "static __attribute__((target(\"avx512f\"))) double doubled(double x){register double d __asm__(\"xmm20\")=x;__asm__(\"vaddsd %0, %0, %0\" : \"+v\"(d));return d;}"
        "static __attribute__((target(\"avx512f\"))) float moved(float x){register float f __asm__(\"xmm31\");__asm__(\"vmovss %1, %1, %0\" : \"=v\"(f) : \"v\"(x));return f;}"
        "static __attribute__((target(\"avx512f\"))) double summed(double x, double y){register double a __asm__(\"xmm16\")=x;register double b __asm__(\"xmm17\")=y;__asm__(\"vaddsd %1, %0, %0\" : \"+v\"(a) : \"v\"(b));return a;}"
        "int tested(int n){return (int)(doubled(n*0.75)*8)+(int)(moved(n*1.5f)*4)*3+(int)(summed(n*0.25,n-2.5)*16)*5;}",
        "asm-register-variables-xmm16", clang, (x86_target_native().features & BUSTER_X86_FEATURE(AVX512F)) != 0);
    // The forms of musl's long double functions (src/math/x86_64), in functions of this test's own: fabs, fsqrt and
    // frndint on a `+t` operand; fprem's and fprem1's loops with y in st(1) and the status word through fnstsw %ax ("=a"),
    // whose st(1) the helper pops after it stores x; a remainder's quotient bits from that word (C0, C3 and C1, its bits
    // 2, 1 and 0); fistpll's store of a "t" input it pops (its "st" clobber); and "X" operands of a template of no
    // instructions.
    check_test_program_source(test,
        "static long double absolute(long double v){__asm__(\"fabs\" : \"+t\"(v));return v;}"
        "static long double square_root(long double v){__asm__(\"fsqrt\" : \"+t\"(v));return v;}"
        "static long double rounded(long double v){__asm__(\"frndint\" : \"+t\"(v));return v;}"
        "static long double truncated_remainder(long double x, long double y){unsigned short status;"
        "do __asm__(\"fprem; fnstsw %%ax\" : \"+t\"(x), \"=a\"(status) : \"u\"(y));while((status>>10)&1);return x;}"
        "static long double nearest_remainder(long double x, long double y, int* quotient){unsigned short status;long double r=x;"
        "const void* inputs[2]={&x,&y};__asm__(\"\" :: \"X\"(inputs[0]), \"X\"(inputs[1]));"
        "do __asm__(\"fprem1\\n\\tfnstsw %%ax\" : \"+t\"(r), \"=a\"(status) : \"u\"(y));while((status>>10)&1);"
        "int low=((status>>8)&1)<<2|((status>>14)&1)<<1|((status>>9)&1);*quotient=(x<0)!=(y<0) ? -low : low;return r;}"
        "static long whole(long double v){long r;__asm__(\"fistpll %0\" : \"=m\"(r) : \"t\"(v) : \"st\");return r;}"
        "static long long whole_long_long(long double v){long long r;__asm__(\"fistpll %0\" : \"=m\"(r) : \"t\"(v) : \"st\");return r;}"
        "int tested(int n){long double a=n*1.25L-4,b=n%6+0.75L;int q=0;long double r=nearest_remainder(a*9,b,&q);"
        "return (int)(square_root(absolute(a))*1000)+(int)(rounded(a*3.5L)*3)+(int)(truncated_remainder(a*13,b)*100)*5+(int)(r*100)*7+q*11+"
        "(int)whole(a*2.2L)*13+(int)whole_long_long(a*-2.75L)*17;}",
        "asm-x87-library-shapes", clang);
    // Each shape of the x87's operands, by the rules clang's code follows (LLVM's stackifier): two outputs from st(0)
    // (fsincos), a matching input popped with st(1)'s clobbered input (fyl2x) and with st(1)'s kept one, which the helper
    // pops (fscale), outputs of a template that pushes them (fld1, fldpi into a double), a float's and a double's
    // operands, and templates of no instructions.
    check_test_program_source(test,
        "static long double x_sincos(long double x, long double* s){long double c,t;__asm__(\"fsincos\" : \"=t\"(c), \"=u\"(t) : \"0\"(x));*s=t;return c;}"
        "static long double x_yl2x(long double x, long double y){long double r;__asm__(\"fyl2x\" : \"=t\"(r) : \"0\"(x), \"u\"(y) : \"st(1)\");return r;}"
        "static long double x_scale(long double x, long double y){long double r;__asm__(\"fscale\" : \"=t\"(r) : \"0\"(x), \"u\"(y));return r;}"
        "static double x_one(void){double r;__asm__(\"fld1\" : \"=t\"(r));return r;}"
        "static double x_pi(void){double r;__asm__(\"fldpi\" : \"=t\"(r));return r;}"
        "static float x_sqrtf(float x){__asm__(\"fsqrt\" : \"+t\"(x));return x;}"
        "static double x_sqrt(double x){double r;__asm__(\"fsqrt\" : \"=t\"(r) : \"0\"(x));return r;}"
        "static long double x_keep(long double x){__asm__(\"\" : \"+t\"(x));return x;}"
        "static long double x_copy(long double x){long double r;__asm__(\"\" : \"=t\"(r) : \"0\"(x));return r;}"
        "int tested(int n){long double s=0;long double c=x_sincos(n*0.25L,&s);"
        "return (int)(c*10000)+(int)(s*10000)*3+(int)(x_yl2x(n*n+1.5L,n+0.5L)*1000)*5+(int)(x_scale(1.5L,n%7)*64)*7+(int)(x_one()*11)+(int)(x_pi()*1e6)+"
        "(int)(x_sqrtf(n*n+2.f)*1000)*13+(int)(x_sqrt(n+5.0)*1e6)%100000+(int)(x_keep(n*0.5L)*4)*17+(int)(x_copy(n*0.75L)*4)*19;}",
        "asm-x87-shapes", clang);
    // x87 operands named in templates (#1115): t's is %st and u's %st(1), as GCC and clang print them, which the
    // assembler reads, with AT&T's operations on the stack's top (D8's forms, not reversed) and a comment.
    check_test_program_source(test,
        "static long double x_named_multiply(long double x, long double y){__asm__(\"fmul %1, %0\" : \"+t\"(x) : \"u\"(y));return x;}"
        "static long double x_named_reverse(long double x, long double y){__asm__(\"fsubr %1, %0\" : \"+t\"(x) : \"u\"(y));return x;}"
        "static long double x_named_divide(long double x, long double y){__asm__(\"fdiv %1, %0\" : \"+t\"(x) : \"u\"(y));return x;}"
        "static long double x_named_root(long double x){__asm__(\"fsqrt # %0\" : \"+t\"(x));return x;}"
        "int tested(int n){return (int)(x_named_multiply(n*1.5L,2.25L)*100)+(int)(x_named_reverse(n*0.5L,7.0L)*100)*3+(int)(x_named_divide(n+9.0L,4.0L)*1000)*7+"
        "(int)(x_named_root(n*n+1.0L)*1000)*5;}",
        "asm-x87-named-operations", clang);
    // A float's and a double's signaling NaNs through `+t` in templates of no instructions: their helper passes them
    // through the x87's stack as clang's code does, whose fld quiets them and raises the invalid operation (masked). The
    // results' bits and the status word's flag after fnclex, and a long double's value, must be clang's build's.
    check_test_program_source(test,
        "static float through_float(float v){__asm__ volatile(\"\" : \"+t\"(v));return v;}"
        "static double through_double(double v){__asm__ volatile(\"\" : \"+t\"(v));return v;}"
        "static long double through_long_double(long double v){__asm__ volatile(\"\" : \"+t\"(v));return v;}"
        "static unsigned short status_word(void){unsigned short word;__asm__ volatile(\"fnstsw %0\" : \"=m\"(word));return word;}"
        "static void clear_exceptions(void){__asm__ volatile(\"fnclex\");}"
        "int tested(int n){unsigned bits=0x7f800001u|((unsigned)n&0xffu)<<8;float f;__builtin_memcpy(&f,&bits,4);"
        "clear_exceptions();float g=through_float(f);unsigned short after_float=status_word();unsigned out;__builtin_memcpy(&out,&g,4);"
        "unsigned long long wide=0x7ff0000000000001ull|((unsigned long long)n&0xffu)<<20;double d;__builtin_memcpy(&d,&wide,8);"
        "clear_exceptions();double e=through_double(d);unsigned short after_double=status_word();unsigned long long wide_out;__builtin_memcpy(&wide_out,&e,8);"
        "long double x=n*0.5L;clear_exceptions();long double y=through_long_double(x);unsigned short after_long_double=status_word();"
        "return (int)(out>>8)+(int)(wide_out>>40)*3+(after_float&1)*5+(after_double&1)*7+(after_long_double&1)*11+(int)(y*4)*13;}",
        "asm-x87-empty-signaling", clang);
    // Templates that take directives besides the syntax switches: alignment and fill, data bytes, section switches, and
    // symbol bindings and absolute assignments (asm_assemble's, #812). Data and hand-encoded bytes are a helper's code;
    // alignment and section-only templates publish atoms and call nothing. The bytes are jumped over, the sections'
    // records are read, and the absolute value is an immediate. clang's build of the same source must print the same.
    check_test_program_source(test,
        "static __attribute__((naked,noinline)) int directivenaked(int n){__asm__(\"leal 3(%rdi), %eax\\n.p2align 4\\n.byte 0x90\\nret\");}"
        "int tested(int n){long a,b,c,d,f;"
        "__asm__ volatile(\".p2align 4\");__asm__ volatile(\".balign 8\");__asm__ volatile(\".align 4\");"
        "__asm__ volatile(\".nops 5\");__asm__ volatile(\".nops 4, 1\");"
        "__asm__ volatile(\"jmp 1f\\n.skip 3\\n1:\");"
        "__asm__ volatile(\"jmp 1f\\n.byte 0x90, 0x91\\n1:\");"
        "__asm__ volatile(\"jmp 1f\\n.byte 0x0f, 0x1f, 0x00\\n1:\");"
        "__asm__ volatile(\"jmp 1f\\n.long 0\\n1:\");"
        "__asm__ volatile(\".pushsection .data\\n.Ldir_data%=:\\n.byte 41\\n.popsection\\nleaq .Ldir_data%=(%%rip), %0\" : \"=r\"(a));"
        "__asm__ volatile(\".section .rodata\\n.Ldir_ro%=:\\n.byte 43\\n.previous\\nleaq .Ldir_ro%=(%%rip), %0\" : \"=r\"(b));"
        "__asm__ volatile(\".pushsection .data\\n.weak dir_weak%=\\ndir_weak%=:\\n.byte 45\\n.popsection\" : : : \"memory\");"
        "__asm__ volatile(\".pushsection .data\\n.globl dir_global%=\\ndir_global%=:\\n.byte 47\\n.popsection\" : : : \"memory\");"
        "__asm__ volatile(\".globl declared_only\");"
        "__asm__ volatile(\".pushsection .data\\n.type dir_typed%=,@object\\n.size dir_typed%=,1\\n.hidden dir_typed%=\\ndir_typed%=:\\n.byte 49\\n.popsection\\n"
        "leaq dir_typed%=(%%rip), %0\" : \"=r\"(c));"
        "__asm__ volatile(\".pushsection .data\\n.Ldir_more%=:\\n.byte 1\\n.word 2\\n.long 3\\n.quad 4\\n.ascii \\\"A\\\"\\n.asciz \\\"B\\\"\\n.string \\\"C\\\"\\n.zero 2\\n.space 3, 5\\n"
        ".popsection\\nleaq .Ldir_more%=(%%rip), %0\" : \"=r\"(d));"
        "__asm__ volatile(\".pushsection .bss\\n.Ldir_bss%=:\\n.zero 8\\n.popsection\\nleaq .Ldir_bss%=(%%rip), %0\" : \"=r\"(f));"
        "int e,g;g=0;__asm__(\".set dir_set, 7\\n.equ dir_equ, 9\\nmovl $dir_set, %0\\naddl $dir_equ, %0\" : \"=r\"(e));"
        "__asm__ volatile(\".set dir_repeat, 7\\naddl $dir_repeat, %0\" : \"+r\"(g));"
        "__asm__ volatile(\".set dir_repeat, 8\\naddl $dir_repeat, %0\" : \"+r\"(g));"
        "return (*(unsigned char*)a*10000+*(unsigned char*)b*1000+*(unsigned char*)c*100+*(unsigned char*)d*10+*(unsigned char*)f+e+g+n)%1000+directivenaked(n);}",
        "asm-directives", clang);
    // Templates that take directives with the record helper's own loads and stores (#1839's item 10,
    // check_asm_unit_helper): x87 operands (one of directives alone among them, whose helper passes its value through the
    // x87's stack), flag outputs (one of directives alone, whose object is set), and asm goto's labels, by name and number,
    // with an output reaching a label. clang's build of the same source must print the same.
    check_test_program_source(test,
        "static double root(double x){double r;__asm__(\".p2align 4\\n\\tfsqrt\" : \"=t\"(r) : \"0\"(x));return r;}"
        "static long double sum(long double a, long double b){long double r;__asm__(\".p2align 3\\n\\tfaddp\" : \"=t\"(r) : \"0\"(a), \"u\"(b) : \"st(1)\");return r;}"
        "static float kept(float x){__asm__(\".p2align 4\" : \"+t\"(x));return x;}"
        "static int zero(long x){int z;__asm__(\".p2align 4\\n\\ttestq %1, %1\" : \"=@ccz\"(z) : \"r\"(x));return z;}"
        "static int below(unsigned a, unsigned b){_Bool c;__asm__(\".nops 3\\n\\tcmpl %2, %1\" : \"=@ccb\"(c) : \"r\"(a), \"r\"(b));return c;}"
        "static int flag_only(void){int f;__asm__(\".p2align 4\" : \"=@ccz\"(f));return f == 0 || f == 1;}"
        "static int sign(int x){asm goto(\".p2align 4\\n\\ttestl %0, %0\\n\\tjz %l1\\n\\tjs %l[negative]\" : : \"r\"(x) : : zero, negative);return 1;zero:return 0;negative:return -1;}"
        "static int five(int x, int* o){int r=x*2;asm goto(\".balign 8\\n\\tcmpl $5, %1\\n\\tje %l[hit]\\n\\tmovl $7, %0\" : \"+r\"(r) : \"r\"(x) : : hit);*o=r;return 0;hit:*o=r;return 5;}"
        "int tested(int n){int a=0,b=0;int f=five(n%7,&a)+five(5,&b);"
        "return (int)(root(n*n+0.25)*4)+(int)(sum(n*0.5L,2.25L)*8)+(int)kept(n*1.5f)+zero(n&3)*3+below((unsigned)n,7u)*5+flag_only()*11+sign(n-9)*13+sign(9-n)*17+sign(0)+f*19+a+b;}",
        "asm-directives-helper", clang);
    // loop and jrcxz to the template's own labels (#1839's item 4): asm bodies whose rel8 displacements the assembler
    // fixed, a loop summing rcx's counts down, a jrcxz skipping a store, and a loop and a jrcxz in one template; and one
    // whose jrcxz jumps over a je (which branch sizing sizes), which the whole assembler takes. clang's build must print
    // the same.
    check_test_program_source(test,
        "static long sum(long n){long s=0;if(n<=0)return 0;__asm__(\"1:\\n\\taddq %%rcx, %0\\n\\tloop 1b\" : \"+r\"(s), \"+c\"(n));return s;}"
        "static int zero(long n){int r=1;__asm__(\"jrcxz 1f\\n\\tmovl $0, %0\\n1:\" : \"+r\"(r) : \"c\"(n));return r;}"
        "static long both(long n){long s=0;__asm__(\"jrcxz 2f\\n1:\\n\\tincq %0\\n\\tloop 1b\\n2:\" : \"+r\"(s), \"+c\"(n));return s;}"
        "static int over(long n){int r=0;__asm__(\"jrcxz 2f\\n1:\\n\\tincl %0\\n\\tcmpl $5, %0\\n\\tje 2f\\n\\tloop 1b\\n2:\" : \"+r\"(r), \"+c\"(n));return r;}"
        "int tested(int n){long m=(long)((unsigned)n%9u);return (int)sum(m+1)+zero(m)*3+zero(0)*5+(int)both(m)*7+(int)both(0)*11+over(m)*13+over(0)*17;}",
        "asm-loop-bodies", clang);
    check_test_program_boundary_reason(test, "int tested(int n){__asm__(\".cfi_startproc\\n\\t.cfi_endproc\");return n;}",
                                       "asm-cfi", true, false, 0, 0, "inline assembly's CFI directives");
    check_test_program_boundary_reason(test, "int tested(int n){int e;__asm__ volatile(\".set crossset, 7\");__asm__(\"movl $crossset, %0\" : \"=r\"(e));return e+n;}",
                                       "asm-cross-set", true, false, 0, 0, "inline assembly absolute symbols are per-template");
    check_test_program_boundary_reason(test, "int tested(int n){__asm__ volatile(\".p2align 4\\nret\");return n;}", "asm-directive-return", true, false, 0, 0,
                                       "inline assembly templates that return from their helper");
    // What the lowering refuses, by name.
    check_test_program_boundary_reason(test, "int tested(int n){long r;__asm__(\"movq %1, %0\" : \"=r\"(r) : \"Z\"(0x100000000ull));return (int)r+n;}",
                                       "asm-z-range", true, false, 0, 0, "inline assembly constants outside their constraint's range");
    check_test_program_boundary_reason(test, "int tested(int n){int r;__asm__(\"movl %1, %0\" : \"=r\"(r) : \"N\"((unsigned char)255));return r+n;}",
                                       "asm-n-range", true, false, 0, 0, "inline assembly constants outside their constraint's range");
    // Flag outputs named in their templates (#1846), as the register the condition is set into after them, by each
    // modifier, and written there before it is: run against clang's build.
    check_test_program_source(test,
        "static int zero_named(int n){int z;__asm__(\"testl %1, %1 # %0 %b0 %w0 %k0 %q0\" : \"=@ccz\"(z) : \"r\"(n));return z;}"
        "static _Bool carry_named(unsigned long v){_Bool c;__asm__(\"btq $3, %1\\n\\tsetc %b0\" : \"=@ccc\"(c) : \"r\"(v));return c;}"
        "static long sign_named(long v){long s;int t;__asm__(\"testq %2, %2\\n\\tsets %b0\\n\\tmovl $7, %1\" : \"=@ccs\"(s), \"=r\"(t) : \"r\"(v));return s*100+t;}"
        "int tested(int n){return zero_named(n)+carry_named((unsigned long)n*3)*2+(int)sign_named((long)n-5)*4;}",
        "asm-flag-output-named", clang);
    // Flag outputs (#147): of an object no integer store covers, and with another alternative.
    check_test_program_boundary_reason(test, "struct three{char c[3];};int tested(int n){struct three z;__asm__(\"testl %1, %1\" : \"=@ccz\"(z) : \"r\"(n));return z.c[0];}",
                                       "asm-flag-output-size", true, false, 0, 0, "inline assembly flag outputs of this size");
    check_test_program_boundary_reason(test, "int tested(int n){int z;__asm__(\"testl %1, %1\" : \"=r@ccz\"(z) : \"r\"(n));return z;}",
                                       "asm-flag-output-alternatives", true, false, 0, 0, "inline assembly flag outputs with other alternatives");
    // asm goto's labels as data and as addresses (#1845's item 1): the template assembled whole in its helper, whose exits
    // the uses take, as clang's take its indirect targets' blocks: the Linux kernel's __jump_table entries (`.quad 1b,
    // %l[yes]`, and the relative `.long %l[yes] - .`), a rip-relative address the template jumps through, by a directive
    // template and a record one, with an output stored on each way out; run against clang's build.
    check_test_program_source(test,
        "static int key(int n){__asm__ goto(\"cmpl $3, %0\\n\\tje %l[yes]\\n\\t.pushsection __jump_table, \\\"aw\\\"\\n\\t.balign 8\\n\\t.quad 1f, %l[yes]\\n\\t"
        ".long 1f - ., %l[yes] - .\\n\\t.popsection\\n1:\" : : \"r\"(n) : : yes);return 0;yes:return 1;}"
        "static int via(int n){int y;__asm__ goto(\"leaq %l[two](%%rip), %%rax\\n\\tcmpl $1, %1\\n\\tje 1f\\n\\tleaq %l[one](%%rip), %%rax\\n1:\\tleal 7(%1), %0\\n\\tjmp *%%rax\""
        " : \"=&r\"(y) : \"r\"(n) : \"rax\" : one, two);return 100;one:return y+10;two:return y+20;}"
        "static int aligned(int n){__asm__ goto(\".p2align 4\\n\\tleaq %l1(%%rip), %%rax\\n\\ttestl %0, %0\\n\\tjz 1f\\n\\tjmp *%%rax\\n1:\" : : \"r\"(n) : \"rax\" : out);return n;out:return -n;}"
        "int tested(int n){int m=(int)((unsigned)n%5u);return key(m)+via(m)*3+aligned(m)*7;}",
        "asm-goto-label-data", clang);
    // The x87's operands the lowering doesn't place: f's register of the compiler's choice; an integer; shapes off the
    // stack's top, which clang's code refuses too (an input at st(1) without one at st(0), a clobber below the stack's
    // top), and two inputs at st(0), which clang compiles as one register's; and an x87 register named in the template,
    // which the assembler doesn't parse.
    // f's inputs (#147): below the fixed operands, each in a register of its own that the template names, none popped;
    // with outputs the template pushes early-clobbered, as GCC's rules for f inputs require (clang's code without the &
    // pops the wrong register after the template).
    check_test_program_source(test,
        "static long double add_any(long double a, long double b){__asm__(\"fadd %1, %0\" : \"+t\"(a) : \"f\"(b));return a;}"
        "static long double pick(long double a, long double b, long double c){long double r,s;__asm__(\"fld %2\" : \"=&t\"(r) : \"f\"(a), \"f\"(b));"
        "__asm__(\"fld %1\\n\\tfmul %%st(0), %%st\" : \"=&t\"(s) : \"f\"(c));return r*16+s;}"
        "static long double barrier(long double a){__asm__ volatile(\"\" : : \"f\"(a));return a+1;}"
        "int tested(int n){long double a=n*0.25L+2,b=n-1.5L;return (int)(add_any(a,b)*8)+(int)(pick(a,b,3.0L)*4)+(int)barrier(b);}",
        "asm-x87-any", clang);
    check_test_program_boundary_reason(test, "int tested(int n){int i=n;__asm__(\"\" : \"+t\"(i));return i;}",
                                       "asm-x87-integer", true, false, 0, 1, "inline assembly operands of this type in their constraint's registers");
    check_test_program_boundary_reason(test, "int tested(int n){long double x=n;__asm__ volatile(\"\" : : \"u\"(x));return n;}",
                                       "asm-x87-gap", true, false, 0, 1, "inline assembly's x87 operands and clobbers off the stack's top");
    // Two operands at one depth (#1846), one register's as clang's code makes them: two inputs' depth takes the first
    // matching or `+` input's value in clang's order of the inputs (the explicit ones, then the `+` outputs'), else the
    // last other input's; two outputs at one depth each take its value, a template's push's too.
    check_test_program_source(test,
        "static double plus_last(double x, double y){__asm__(\"\" : \"+t\"(x) : \"t\"(y));return x;}"
        "static double tied_first(double x, double y){double r;__asm__(\"\" : \"=t\"(r) : \"0\"(x), \"t\"(y));return r;}"
        "static double tied_after(double x, double y){double r;__asm__(\"\" : \"=t\"(r) : \"t\"(y), \"0\"(x));return r;}"
        "static double last_below(double x, double y, double z){double r;__asm__(\"fxch\\n\\tfstp %%st(1)\" : \"=t\"(r) : \"t\"(x), \"u\"(y), \"u\"(z) : \"st(1)\");return r;}"
        "static long double both_out(long double x, long double y){long double r, s;__asm__(\"\" : \"=t\"(r), \"=t\"(s) : \"t\"(x), \"t\"(y));return r*10+s;}"
        "static long double first_matching(long double x, long double y){long double r, s;__asm__(\"\" : \"=t\"(r), \"=t\"(s) : \"1\"(y), \"0\"(x));return r*10+s;}"
        "static long double two_plus(long double x, long double y){__asm__(\"\" : \"+t\"(x), \"+t\"(y));return x*10+y;}"
        "static long double matching_wins(long double x, long double y, long double z){long double r;__asm__(\"\" : \"=t\"(r) : \"t\"(z), \"0\"(x), \"t\"(y));return r;}"
        "static float added(float x, float y){__asm__(\"fadd %1, %0\" : \"+t\"(x) : \"t\"(y));return x;}"
        "static void pushed_twice(double* a, float* b){__asm__(\"fldpi\" : \"=t\"(*a), \"=t\"(*b));}"
        "int tested(int n){double a=0;float b=0;pushed_twice(&a,&b);double x=n*0.5+1,y=n-3.25,z=n*n+0.125;"
        "return (int)(plus_last(x,y)*8)+(int)(tied_first(x,y)*16)+(int)(tied_after(x,y)*32)+(int)(last_below(x,y,z)*4)+(int)both_out(x,y)+(int)(first_matching(x,y)*2)+"
        "(int)(two_plus(x,y)*3)+(int)(matching_wins(x,y,z)*5)+(int)(added((float)x,(float)y)*7)+(int)(a*1000)+(int)(b*100000);}",
        "asm-x87-doubled", clang);
    check_test_program_boundary_reason(test, "int tested(int n){long double x=n;__asm__ volatile(\"\" : : \"t\"(x) : \"st(1)\");return n;}",
                                       "asm-x87-clobber", true, false, 0, 1, "inline assembly's x87 operands and clobbers off the stack's top");
    // x87 registers named in templates (#147): %0 of t as %st, of u as %st(1), the stack's registers by name, an
    // exchange, AT&T's reversed subtraction with a pop, a compare into the flags, and ffreep's pop (#1223).
    check_test_program_source(test,
        "static long double swap_sub(long double a, long double b){__asm__(\"fxch %1\\n\\tfsub %1, %0\" : \"+t\"(a), \"+u\"(b));return a*3+b;}"
        "static long double add_top(long double a, long double b){__asm__(\"fadd %%st(1), %%st\" : \"+t\"(a) : \"u\"(b));return a;}"
        "static long double square(long double a){__asm__(\"fld %%st(0)\\n\\tfmul %%st(1), %%st\\n\\tfstp %%st(1)\" : \"+t\"(a));return a;}"
        "static long double reversed(long double a, long double b){long double r;__asm__(\"fsubp %%st, %%st(1)\" : \"=t\"(r) : \"0\"(a), \"u\"(b) : \"st(1)\");return r;}"
        "static int below(long double a, long double b){int r;__asm__(\"fcomi %%st(1), %%st\" : \"=@ccb\"(r) : \"t\"(a), \"u\"(b));return r;}"
        "static long double square_freed(long double a){__asm__(\"fld %%st(0)\\n\\tfmul %%st(1), %%st\\n\\tfxch\\n\\tffreep %%st(0)\" : \"+t\"(a));return a;}"
        "int tested(int n){long double a=n*0.5L+1,b=n-2.25L;return (int)(swap_sub(a,b)*8)+(int)(add_top(a,b)*4)+(int)square(a)+(int)(square_freed(b)*2)+(int)(reversed(a,b)*16)+below(a,b)*1000+below(b,a)*3000;}",
        "asm-x87-named", clang);
    // A's operands (#1846): an __int128 in rdx:rax, mulq's product beside an "a" input that shares rax, a `+` pair, a
    // pair's input matching its output, and 64-bit and narrower values in rax, printed by their 64-bit names (an
    // unsigned zero-extended there, and a short's), a double's bits among them.
    check_test_program_source(test,
        "static unsigned __int128 product(unsigned long a, unsigned long b){unsigned __int128 r;__asm__(\"mulq %2\" : \"=A\"(r) : \"a\"(a), \"rm\"(b));return r;}"
        "static unsigned __int128 swap_halves(unsigned __int128 v){__asm__(\"xchgq %%rax, %%rdx # %0\" : \"+A\"(v));return v;}"
        "static unsigned __int128 copied(unsigned __int128 v){unsigned __int128 r;__asm__(\"# %0 %1\" : \"=A\"(r) : \"A\"(v));return r;}"
        "static unsigned long low_bits(unsigned long v){unsigned long r;__asm__(\"lea 1(%1), %0\" : \"=A\"(r) : \"r\"(v));return r;}"
        "static unsigned widened(unsigned v){unsigned r;__asm__(\"not %0\\n\\tshr $32, %0\" : \"=A\"(r) : \"0\"(v));return r;}"
        "static int wide_name(short v){int r;__asm__(\"movq %1, %q0\\n\\tadd %k1, %k0\" : \"=r\"(r) : \"A\"(v));return r;}"
        "static double through(double d){__asm__(\"# %0\" : \"+A\"(d));return d;}"
        "int tested(int n){unsigned long a=(unsigned long)n*0x9e3779b97f4a7c15ul+1,b=(unsigned long)(n+3)*0xc2b2ae3d27d4eb4ful;"
        "unsigned __int128 p=product(a,b);unsigned __int128 q=swap_halves(p);unsigned __int128 c=copied(q+n);"
        "return (int)(p>>64)+(int)p*3+(int)(q>>64)*5+(int)(c>>70)+(int)low_bits(a)+(int)widened((unsigned)n*77u)+wide_name((short)(n*1000-3))+(int)through(n*2.5);}",
        "asm-pair", clang);
    check_test_program_source(test, "int tested(int n){unsigned __int128 v=(unsigned)n;__asm__(\".p2align 4\\n\\tnotq %%rax\" : \"+A\"(v));return (int)v+(int)(v>>64)*3;}",
                              "asm-pair-directives", clang);
    // MMX's operands (#1846): y's registers of a long, an unsigned, a short and a byte (zero-extended into the register,
    // its low bytes stored), a double's bits, an early-clobbered output, a matching input, MMX clobbers the
    // registers avoid, an MMX input into a general output, and a template of no instructions; emms before returning.
    check_test_program_source(test,
        "static long add_lanes(long a, long b){long r;__asm__(\"movq %1, %0\\n\\tpaddd %2, %0\" : \"=&y\"(r) : \"y\"(a), \"y\"(b));return r;}"
        "static unsigned flip(unsigned a, unsigned b){__asm__(\"pxor %1, %0\" : \"+y\"(a) : \"y\"(b));return a;}"
        "static short doubled(short a){short r;__asm__(\"paddd %0, %0\" : \"=y\"(r) : \"0\"(a));return r;}"
        "static unsigned char byte_copy(unsigned char a){unsigned char r;__asm__(\"movq %1, %0\" : \"=y\"(r) : \"y\"(a));return r;}"
        "static double through(double d){__asm__(\"movq %0, %%mm7\\n\\tmovq %%mm7, %0\" : \"+y\"(d) : : \"mm7\");return d;}"
        "static long avoiding(long a){long r;__asm__(\"movq %1, %%mm0\\n\\tmovq %%mm0, %0\" : \"=y\"(r) : \"y\"(a) : \"mm0\", \"mm1\");return r;}"
        "static long to_general(long a){long r;__asm__(\"movq %1, %0\" : \"=r\"(r) : \"y\"(a));return r;}"
        "static int low_half(long a){int r;__asm__(\"movd %1, %0\" : \"=r\"(r) : \"y\"(a));return r;}"
        "static void empty(long* p){__asm__(\"\" : \"+y\"(*p));}"
        "int tested(int n){long a=((long)n<<32)|(unsigned)(n*3+1);long b=((long)(n+5)<<32)|7u;long e=a*3;empty(&e);"
        "long r=add_lanes(a,b)+(long)flip((unsigned)n*2654435761u,0x5a5a5a5au)+doubled((short)(n*1000))+byte_copy((unsigned char)(n*37))+(long)through(n*0.75)+avoiding(a^b)+to_general(b)+low_half(a)+e;"
        "__asm__ volatile(\"emms\");return (int)(r^(r>>32));}",
        "asm-mmx", clang);
    check_test_program_boundary_reason(test, "int tested(int n){long m=n;__asm__(\"movq %q0, %0\" : \"+y\"(m));return (int)m;}",
                                       "asm-mmx-modifier", true, false, 0, 1, "this inline assembly operand modifier");
    // MMX and x87 operands in one statement (#1846, the owner's decision on #1447): the x87's loaded first and the MMX
    // ones after, the MMX outputs stored first, as clang's code orders them; the x87 output's bytes, whatever the MMX
    // load left on its stack, are the same instructions' as clang's, compared bit for bit.
    check_test_program_source(test,
        "static long both(long m, long double* x){__asm__(\"paddd %0, %0\\n\\tfsqrt\" : \"+y\"(m), \"+t\"(*x));__asm__ volatile(\"emms\");return m;}"
        "int tested(int n){long double x=(long double)(n*n);long m=both(n*3+1,&x);unsigned char b[10];__builtin_memcpy(b,&x,10);"
        "int s=0;for(int i=0;i<10;i++)s=s*31+b[i];return (int)m+s;}",
        "asm-mmx-x87", clang);
    check_test_program_source(test, "int tested(int n){long m=n;__asm__(\".p2align 4\\n\\tpaddd %0, %0\" : \"+y\"(m));__asm__ volatile(\"emms\");return (int)m;}",
                              "asm-mmx-directives", clang);
    // An __m64 in an MMX register (#1846, the owner's decision on #1447): an object's 8 bytes, read through its address,
    // as an input, a `+` operand and an early-clobbered output; an input of no object (an rvalue, a call's) through a copy's.
    check_test_program_source(test,
        "typedef long long m64 __attribute__((vector_size(8)));"
        "static long long added(long long a, long long b){m64 x={a},y={b},r;__asm__(\"movq %1, %0\\n\\tpaddd %2, %0\" : \"=&y\"(r) : \"y\"(x), \"y\"(y));"
        "__asm__ volatile(\"emms\");return r[0];}"
        "int tested(int n){m64 v={n*65537};__asm__(\"paddd %0, %0\" : \"+y\"(v));__asm__ volatile(\"emms\");"
        "return (int)v[0]+(int)added((long long)n<<33|7,(long long)n*3);}",
        "asm-mmx-vector", clang);
    check_test_program_source(test,
        "typedef long long m64 __attribute__((vector_size(8)));static m64 made(int n){m64 v={n*7};return v;}"
        "int tested(int n){m64 v={n};long r;__asm__(\"movq %1, %0\" : \"=r\"(r) : \"y\"(v+v));long q;__asm__(\"movq %1, %0\" : \"=r\"(q) : \"y\"(made(n)));"
        "__asm__ volatile(\"emms\");return (int)r+(int)q*3;}",
        "asm-mmx-vector-value", clang);
    // A template that returns is an asm body, whose return leaves its caller (#1839, test-checker-driver's returning templates);
    // one that keeps its helper, as a named label's does, is refused by name.
    check_test_program_boundary_reason(test, "int tested(int n){__asm__ volatile(\"rep ret\\n.Lafter%=:\" ::: \"memory\");return n;}",
                                      "asm-returning-template", true, false, 0, 0, "inline assembly templates that return from their helper");
    // rsp's explicit register variable (#1846, the owner's decision on #1447), as clang's code takes it: an output read
    // from rsp after the template (one of no instructions too), an input moved into rsp before it at the variable's width
    // (sp's movw keeps rsp's other bits), the template naming it %rsp, and an input beside an rsp output sharing it. Each
    // input is the value rsp has, as a program's must be; a template that keeps a helper refuses it by name.
    check_test_program_source(test,
        "static unsigned long read_sp(void){register unsigned long sp __asm__(\"rsp\");__asm__(\"\" : \"=r\"(sp));return sp;}"
        "static int near(void){char local;register unsigned long sp __asm__(\"rsp\");__asm__(\"nop\" : \"=r\"(sp));"
        "long d=(long)((unsigned long)&local-sp);return d>-4096&&d<4096;}"
        "static int kept(void){register unsigned long sp __asm__(\"rsp\");__asm__(\"\" : \"=r\"(sp));unsigned long before=sp;"
        "__asm__ volatile(\"\" : \"+r\"(sp));unsigned long r;__asm__(\"mov %1, %0\" : \"=r\"(r) : \"r\"(sp));"
        "__asm__(\"\" : \"=r\"(sp) : \"r\"(sp));return sp==before&&r==before;}"
        "static int narrow(void){register unsigned short sp __asm__(\"sp\");register unsigned int esp __asm__(\"esp\");"
        "register unsigned long full __asm__(\"rsp\");__asm__(\"\" : \"=r\"(full));__asm__(\"\" : \"=r\"(esp));__asm__(\"\" : \"=r\"(sp));"
        "unsigned short s=sp;__asm__ volatile(\"\" : \"+r\"(sp));return (unsigned short)full==s&&sp==s&&(unsigned int)full==esp;}"
        "int tested(int n){return n+(read_sp()!=0)+2*near()+4*kept()+8*narrow();}",
        "asm-register-variable-rsp", clang);
    check_test_program_boundary_reason(test, "int tested(int n){register long r __asm__(\"rsp\");__asm__ volatile(\".p2align 4\\n\\tmovq %0, %%rax\" : : \"r\"(r) : \"rax\");return n;}",
                                       "asm-register-variable-rsp-directives", true, false, 0, 0, "explicit register variables in rsp in templates that take directives");
    // Explicit register variables the lowering doesn't place: in an XMM register past XMM15 without AVX-512F; an input of a
    // memory constraint, which clang's code refuses too; and an integer in an XMM register.
    check_test_program_source(test, "int tested(int n){register char r __asm__(\"ah\")=(char)n;__asm__ volatile(\".p2align 4\\n\\tincb %0\" : \"+q\"(r));return r;}",
                              "asm-register-variable-high", clang);
    check_test_program_boundary_reason(test, "int tested(int n){register double r __asm__(\"xmm16\")=n;__asm__ volatile(\"\" : : \"v\"(r));return n;}",
                                       "asm-register-variable-xmm16", true, false, 0, 1, "explicit register variables in XMM16 to XMM31 without AVX-512F");
    check_test_program_boundary_reason(test, "int tested(int n){register long r __asm__(\"r10\")=n;__asm__ volatile(\"\" : : \"m\"(r));return n;}",
                                       "asm-register-variable-memory", true, false, 0, 1, "explicit register variables as inline assembly memory inputs");
    check_test_program_boundary_reason(test, "int tested(int n){register long r __asm__(\"xmm3\")=n;__asm__ volatile(\"\" : : \"x\"(r));return n;}",
                                       "asm-register-variable-integer-xmm", true, false, 0, 1, "inline assembly operands of this type in their constraint's registers");
    // #554 rejects ordinary statements and parameter operands before lowering. An uninitialized named-register VLA
    // passes that constraint, but evaluating its bound still gives the naked planner a non-asm control to refuse.
    check_test_program_boundary_reason(test, "__attribute__((naked)) void raw(int n){register int x[n] asm(\"rax\");__asm__(\"ret\");}int tested(int n){return n;}",
                                       "asm-naked-vla-register", true, false, 0, 0, "statements other than asm statements in naked functions");
    // A constant with a register constraint reaches the planner without a forbidden parameter reference.
    check_test_program_boundary_reason(test, "__attribute__((naked)) void raw(void){__asm__(\"ret\" : : \"r\"(1));}int tested(int n){return n;}",
                                       "asm-naked-operand", true, false, 0, 0,
                                       "naked functions' inline assembly operands other than integer constants or static objects' symbols");
    check_test_program_boundary_reason(test, "int tested(int n){__asm__(\"twice: nop\");__asm__(\"twice: nop\");return n;}",
                                       "asm-label-twice", true, false, 0, 0, "inline assembly labels another template defines too");
    check_test_program_boundary_reason(test, "int tested(int n){__asm__(\"jmp .Lnowhere\");return n;}",
                                       "asm-local-undefined", true, false, 0, 0, "inline assembly's local labels that no template defines");
    // Vectors of 16, 32 and 64 bytes in vector registers (#1846, the owner's decision on #1447), for x86-64-v4: in
    // templates of instructions, through their addresses, as inputs (a call's value, an rvalue's), `+` operands, outputs,
    // matching inputs (in a template of no instructions too, whose output takes the input's value), a volatile `+`
    // output of no instructions (loaded and stored, as clang's code does), modifiers (%x0 of a ymm operand), a reference
    // in a comment, and explicit register variables in an XMM register past XMM15, a YMM register and a ZMM one past
    // ZMM15; run where the host has its features.
    static const char asm_vector_operands[] =
        "static v8di twice8(v8di x){return x+x;}"
        "static v8di added(v8di a,v8di b){v8di r;__asm__(\"vpaddq %2, %1, %0\" : \"=v\"(r) : \"v\"(a), \"v\"(twice8(b)));return r;}"
        "static v8si doubled(v8si a){__asm__(\"vpaddd %0, %0, %0\" : \"+x\"(a));return a;}"
        "static v4sf squared(v4sf a){__asm__(\"vmulps %0, %0, %0 # %1\" : \"+x\"(a) : \"v\"(a+a));return a;}"
        "static v8si matched(v8si a){v8si b;__asm__(\"vpaddd %0, %0, %0\" : \"=x\"(b) : \"0\"(a));return b;}"
        "static v8di copied(v8di a){v8di b={0};__asm__(\"\" : \"=v\"(b) : \"0\"(a));return b;}"
        "static int kept(int n){volatile v8di a={n,1};__asm__(\"# %0\" : \"+v\"(a));v8di b=a;return (int)b[0];}"
        "static v8si low(v8si a){__asm__(\"vpaddd %x0, %x0, %x0\" : \"+x\"(a));return a;}"
        "static v8si in_ymm(v8si a){register v8si y __asm__(\"ymm7\")=a;__asm__(\"vpaddd %0, %0, %0\" : \"+x\"(y));return y;}"
        "static v8di in_zmm(v8di a){register v8di z __asm__(\"zmm21\")=a;__asm__(\"vpaddq %0, %0, %0\" : \"+v\"(z));return z;}"
        "static v4sf in_xmm(v4sf a){register v4sf x __asm__(\"xmm30\")=a;v4sf r;__asm__(\"vaddps %1, %1, %0\" : \"=v\"(r) : \"v\"(x));return r;}"
        "int tested(int n){v8di a={n,n+1,n+2,n+3,n+4,n+5,n+6,n+7};v8si b={n,2,3,4,5,6,7,n*3};v4sf c={1.5f,(float)n,2.5f,-3.0f};"
        "v8di r=added(a,a);v8si d=doubled(b);v4sf e=squared(c);v8si f=matched(b);v8di g=copied(a);v8si h=low(b);v8si y=in_ymm(b);v8di z=in_zmm(a);"
        "v4sf x=in_xmm(c);long long s=kept(n);"
        "for(int i=0;i<8;i++)s+=r[i]*(i+1)+d[i]*3+f[i]*5+g[i]*7+h[i]*11+y[i]*13+z[i]*17;"
        "for(int i=0;i<4;i++)s+=(long long)(e[i]*4)+(long long)(x[i]*8);return (int)s;}";
    if (vectors_parsed)
    {
        const bool vectors_native = (native.features & vectors.features) == vectors.features;
        char source[4096];
        const int length = snprintf(source, sizeof(source), "%s%s", asm_vector_types, asm_vector_operands);
        check_test_expect(test, length > 0 && (size_t)length < sizeof(source), "the vector operands' source fits its buffer");
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64-v4";
        check_test_program_source_native_limits(test, source, "asm-vector-operands", clang, false, -1, 0, &vectors, vectors_native);
        check_test_program_build_release();
        check_test_program_march = "-march=x86-64";
    }
    // Under SSE alone, a 16-byte vector by movups, in xmm0 up and in an explicit register variable's XMM register.
    check_test_program_source(test,
        "typedef float v4sf __attribute__((vector_size(16)));"
        "static v4sf added(v4sf a,v4sf b){__asm__(\"addps %1, %0\" : \"+x\"(a) : \"x\"(b));return a;}"
        "static v4sf fixed(v4sf a){register v4sf x __asm__(\"xmm9\")=a;__asm__(\"mulps %0, %0\" : \"+x\"(x));return x;}"
        "int tested(int n){v4sf a={(float)n,1.5f,2.0f,-4.0f};v4sf r=fixed(added(a,a));return (int)(r[0]+r[1]*3+r[2]*5+r[3]*7);}",
        "asm-vector-operands-sse", clang);
    // Where clang can't place them: a register variable of another size than its register's (clang's "does not match
    // required type"), and a template that takes directives. An x or v operand of 32 bytes without AVX, or 64 without
    // AVX-512F, is clang's Sema error now (#2231, test-checker-driver's asm operand sizes), before the lowering.
    check_test_program_boundary_reason(test, "typedef float v4sf __attribute__((vector_size(16)));int tested(int n){register v4sf a __asm__(\"ymm3\")={(float)n};__asm__(\"nop\" : \"+x\"(a));return (int)a[0];}",
                                       "asm-vector-register-size", true, false, 0, 1, "inline assembly operands of this type in their constraint's registers");
    check_test_program_boundary_reason(test, "typedef float v4sf __attribute__((vector_size(16)));int tested(int n){v4sf a={(float)n};__asm__(\".p2align 4\\n\\taddps %0, %0\" : \"+x\"(a));return (int)a[0];}",
                                       "asm-vector-directives", true, false, 0, 1, "inline assembly vector operands in templates that take directives");
}

#endif
