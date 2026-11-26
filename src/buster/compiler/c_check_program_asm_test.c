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
// the names step adds (#2090: its run without room must leave the helpers as they were), then drops as private (#2351).
// The final directive template exercises the assembled-byte body's allocations and its rollback after exhaustion.
static void check_test_program_asm_room(CheckTest* test)
{
    static const char source[] =
        "float through(float x){__asm__ volatile(\"\" : \"+t\"(x));return x;}"
        "long double root(long double x){__asm__(\"fsqrt\" : \"+t\"(x));return x;}"
        "int flagged(int a, float x){__int128 z;__asm__(\"cmpl $0, %2\" : \"=r\"(z), \"+t\"(x) : \"r\"(a));return (int)z+(int)x;}"
        "int rotated(int n){int r;__asm__(\"rorxl $3, %1, %0\" : \"=r\"(r) : \"r\"(n));return r;}"
        "int counted(int n){__asm__(\".Lcount%=:\\n\\tdecl %0\\n\\tjnz .Lcount%=\" : \"+r\"(n));return n;}"
        "int jumped(int n){asm goto(\"testl %0, %0\\n\\tjz %l1\" : : \"r\"(n) : : zero);return 1;zero:return 0;}"
        "int aligned(int n){__asm__(\".p2align 5\\n\\tincl %0\" : \"+r\"(n));return n;}";
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
        const CheckOptions checking = { 0 };
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
        ready = ready && check_program(&program, &parser, &input, &stream, &types.table, options, &output, &scratch) && input.asm_count == 7;
        char description[256];
        snprintf(description, sizeof(description), "asm room, %s: the program is ready", path == 0 ? "program by the checker's scalar path" : "program by its vector path");
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
            done = check_program_asm_plan(&planned, &helpers, &program, &output, &scratch);
            output.capacity = capacity;
            failures += !done;
            kept &= done || (planned.status == CHECK_PROGRAM_RESOURCE && output.length == output_mark && scratch.length == scratch_mark && helpers.count == 0);
        }
        snprintf(description, sizeof(description), "asm room, %s: %u runs without room fail with the arenas as they were, then one plans", path == 0 ? "program by the checker's scalar path" : "program by its vector path",
                 failures);
        check_test_expect(test, ready && done && kept && failures != 0, description);
        const bool plans_right = done && planned.plans != NULL && planned.plans[0].reason == NULL && planned.plans[0].inlined &&
                                 planned.plans[0].helper == BUSTER_CHECK_NONE && planned.plans[1].reason == NULL && planned.plans[1].inlined &&
                                 planned.plans[1].helper == BUSTER_CHECK_NONE && planned.plans[2].reason != NULL &&
                                 strcmp(planned.plans[2].reason, "inline assembly operands of this type in their constraint's registers") == 0 && planned.plans[3].reason == NULL &&
                                 !planned.plans[3].inlined && planned.plans[3].helper != BUSTER_CHECK_NONE && planned.plans[4].reason == NULL &&
                                 planned.plans[4].inlined && planned.plans[4].helper == BUSTER_CHECK_NONE && planned.plans[4].label_count == 0 && planned.plans[5].reason == NULL &&
                                 planned.plans[5].inlined && planned.plans[5].helper == BUSTER_CHECK_NONE && planned.plans[5].inline_branch_count == 2 &&
                                 planned.plans[6].reason == NULL && planned.plans[6].inlined && planned.plans[6].inline_unit &&
                                 planned.plans[6].helper == BUSTER_CHECK_NONE && helpers.count == 1;
        snprintf(description, sizeof(description), "asm room, %s: the x87 statements are asm bodies, the __int128 operand is refused by name, rorx has a helper, the private label's and the asm goto's statements are asm bodies",
                 path == 0 ? "program by the checker's scalar path" : "program by its vector path");
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
        const CheckOptions checking = { 0 };
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
        ready = ready && check_program_asm_plan(&planned, &helpers, &program, &output, &scratch) && planned.plans != NULL;
        char description[256];
        snprintf(description, sizeof(description), "asm kinds, %s: the statements are planned, with no helper (%u)", path == 0 ? "program by the checker's scalar path" : "program by its vector path", helpers.count);
        check_test_expect(test, ready && helpers.count == 0, description);
        for (uint32_t s = 0; ready && s < STATEMENTS; s += 1)
        {
            const CheckProgramAsm* plan = planned.plans + s;
            // mmx_general's operands are general registers alone: its body's operands are its call's arguments.
            const bool passes = s == 1;
            bool right = plan->planned && plan->reason == NULL && plan->inlined && plan->helper == BUSTER_CHECK_NONE && plan->inline_passed == passes;
            right = right && (s != MASK || ((plan->inline_masks & 0xfe) != 0 && (plan->inline_touched & (1u << X86_REGISTER_RAX)) != 0));
            snprintf(description, sizeof(description), "asm kinds, %s: statement %u is an asm body%s", path == 0 ? "program by the checker's scalar path" : "program by its vector path", s,
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
// arguments, whose body returns its four outputs in registers, and which touches rbx; rdtsc's; a shift's count
// in cl), a literal register (`%%rsp`, read), a syntax switch, more records than a row takes, an x87 operand, a form
// x86_semantics.c doesn't cover (ldmxcsr), an explicit register variable, and a register class narrower than the sixteen
// (Q's), a branch to a numeric label (a branch of the body), a named label (#1839: the body's label, at the record
// after its movl, the template's end), and hlt (musl's a_crash, #2351), which writes nothing and kept its helper until
// check_asm_implicit listed it.
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
        "int named(int n){int r;__asm__(\"testl %1, %1\\n\\tjle .Lnamed%=\\n\\tmovl $5, %0\\n.Lnamed%=:\" : \"=r\"(r) : \"r\"(n), \"0\"(0));return r;}"
        "void halted(void){__asm__ volatile(\"hlt\" ::: \"memory\");}";
    static const uint16_t first_forms[] = {
        X86_FORM_BSWAP_R32, X86_FORM_MOV_RM32_R32, X86_FORM_SQRTSD_X_XM64, X86_FORM_MFENCE, X86_FORM_LOCK_XADD_M32_R32, X86_FORM_ADD_RM64_IMM8, X86_FORM_MFENCE,
    };
    static const uint8_t record_counts[] = { 2, 4, 2, 4, 2, 2, 1 };
    static const uint8_t operand_counts[] = { 2, 3, 2, 0, 3, 2, 0 };
    enum
    {
        PLACED = 7,
        KEPT = 13,
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
        const CheckOptions checking = { 0 };
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
        ready = ready && check_program_asm_plan(&planned, &helpers, &program, &output, &scratch) && planned.plans != NULL;
        char description[256];
        snprintf(description, sizeof(description), "asm placement, %s: the statements are planned", path == 0 ? "program by the checker's scalar path" : "program by its vector path");
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
                // The named label's body: testl, jle to the label and movl, the jle a branch of the body to the template's
                // end (record 3). Its label, `.Lnamed` and the statement's number, which no template refers to, is private
                // and dropped (#2351's gap 4).
                right = plan->label_count == 0 && plan->inline_branch_count == 1 && plan->inline_targets[0] == 3;
            }
            if (right && s == PLACED)
            {
                // cpuid's body (#1839): its inputs are its call's arguments, moved into eax and ecx. All four outputs
                // come back in their registers (#2144), so its body is just cpuid, without output-address loads or stores.
                const X86Instruction* records = plan->inline_records;
                right = plan->inline_passed && plan->inline_count == 1 && plan->inline_result == 1 && records[0].form == X86_FORM_CPUID &&
                        plan->places[0].returned && plan->places[1].returned && plan->places[2].returned && plan->places[3].returned &&
                        (plan->inline_touched & (1u << X86_REGISTER_RBX | 1u << X86_REGISTER_RDX)) == (1u << X86_REGISTER_RBX | 1u << X86_REGISTER_RDX) &&
                        (plan->inline_touched & (1u << X86_REGISTER_RSP | 1u << X86_REGISTER_RBP | 1u << X86_REGISTER_R15)) == 0;
            }
            if (right && placed)
            {
                rows[path][s] = *row;
                right = row->record_count == record_counts[s] && row->operand_count == operand_counts[s] && row->records[0].form == first_forms[s] &&
                        row->result_class == BUSTER_TYPED_CLASS_VOID && tier0_operation_valid(row);
            }
            snprintf(description, sizeof(description), "asm placement, %s: statement %u %s", path == 0 ? "program by the checker's scalar path" : "program by its vector path", s,
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
            snprintf(description, sizeof(description), "asm placement, %s: the rows' roles are the operands', scratch's and stores' as planned", path == 0 ? "program by the checker's scalar path" : "program by its vector path");
            check_test_expect(test, roles, description);
            snprintf(description, sizeof(description), "asm placement, %s: no helper, of %u (the named label's is an asm body too)", path == 0 ? "program by the checker's scalar path" : "program by its vector path", helpers.count);
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

// The native directive fixture must be planned as inline units, not merely execute correctly through whole helpers.
// The same plans serve Tier 0 and Tier 1. Each unit owns one record anchor after the real name indices; the emitter
// uses that anchor to splice its aligned bytes and relocate its entry atom into the containing function (#2351).
static void check_test_program_asm_unit_placement(CheckTest* test)
{
    enum
    {
        STATEMENTS = 19,
        UNITS = 15,
        CONSTANT_DEAD = STATEMENTS - 2,
    };
    CheckTestFixtures fixtures;
    const bool loaded = check_test_fixtures_load(&fixtures, "checker/programs/asm-directive-placement") && fixtures.count == 1;
    check_test_expect(test, loaded, "asm unit placement: its runtime fixture is loaded");
    const char* source = loaded ? fixtures.cases[0].source : NULL;
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const PredefinesOptions predefines = check_test_program_predefines();
    const bool preprocessed = loaded && intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) &&
                              pp_run_source(&pp, "asm-unit-placement", (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "asm unit placement: source preprocessing");
    for (uint32_t tier = 0; preprocessed && tier < 2; tier += 1)
    {
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 26);
        Arena output = arena_reserve(UINT64_C(1) << 26);
        Arena scratch = arena_reserve(UINT64_C(1) << 26);
        const ParseOptions parsing = { 0 };
        const CheckOptions checking = { 0 };
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers plans = { 0 };
        CheckAsms asms = { 0 };
        CheckProgram program = { 0 };
        bool ready = input_arena.base != NULL && output.base != NULL && scratch.base != NULL && check_test_types_open(&types) &&
                     parse_initialize(&parser, &parsing, pp_options.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0 &&
                     check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) &&
                     check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &checking, &stream) && stream.report_count == 0 &&
                     check_initializers_run(&plans, &input, &stream, &parser, &types.table, &input_arena, &scratch) &&
                     check_asms_run(&asms, &input, &stream, &types.table, &input_arena, &scratch);
        const CheckProgramOptions options = {
            .initializers = &plans,
            .asms = &asms,
            .identifiers = &identifiers,
            .predefines = &predefines,
            .asm_bodies = tier != 0,
        };
        ready = ready && check_program(&program, &parser, &input, &stream, &types.table, options, &output, &scratch) && input.asm_count == STATEMENTS;
        CheckProgramAsms planned = { 0 };
        CheckProgramHelpers helpers = { 0 };
        ready = ready && check_program_asm_plan(&planned, &helpers, &program, &output, &scratch) && planned.plans != NULL;
        char description[256];
        snprintf(description, sizeof(description), "asm unit placement, Tier %u: the fixture is planned without a helper (%u)", tier, helpers.count);
        check_test_expect(test, ready && helpers.count == 0, description);
        uint32_t units = 0;
        for (uint32_t s = 0; ready && s < STATEMENTS; s += 1)
        {
            const CheckProgramAsm* plan = planned.plans + s;
            bool placed = s == CONSTANT_DEAD ? !plan->planned :
                          plan->planned && plan->reason == NULL && plan->inlined && plan->helper == BUSTER_CHECK_NONE && plan->row == NULL;
            if (placed && plan->inline_unit)
            {
                const uint32_t atom = plan->unit.entry_atom;
                placed = plan->unit_assembled && atom != 0 && atom < plan->unit.link.atoms.count &&
                         plan->unit.link.atoms.size[atom] <= plan->inline_unit_size && plan->inline_label_records != NULL && plan->inline_label_names != NULL &&
                         plan->inline_label_records[0] <= plan->inline_count && plan->inline_label_names[0] == planned.name_count + s;
            }
            units += plan->inline_unit;
            if (s == CONSTANT_DEAD)
            {
                snprintf(description, sizeof(description), "asm unit placement, Tier %u: constant-dead statement %u has no plan", tier, s);
            }
            else
            {
                snprintf(description, sizeof(description), "asm unit placement, Tier %u: statement %u is an inline %s with its emission anchor", tier, s,
                         plan->inline_unit ? "assembled unit" : "record body");
            }
            check_test_expect(test, placed, description);
        }
        snprintf(description, sizeof(description), "asm unit placement, Tier %u: %u assembled units, including trailing and directive-only alignment", tier, units);
        check_test_expect(test, ready && units == UNITS, description);
        parse_release(&parser);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&output);
        arena_release(&input_arena);
    }
    pp_release(&pp);
    intern_release(&identifiers);
    arena_release(&fixtures.storage);
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
    check_test_program_suite(test, "checker/programs/asm-flag-objects", clang);
    // Each flag output stored once over its object (#1126's review), counted where it lands: the objects lie on a page
    // the program makes read-only, whose write fault each store takes, and which the fault's handler opens for that store
    // alone (a single step, whose trap closes it again). A volatile object of each width takes one store, two outputs of a
    // statement one each, an _Atomic one its exchange, an asm goto's output one on either way out; then their values.
    check_test_program_includes = true;
    check_test_program_suite(test, "checker/programs/asm-flag-single-stores", clang);
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
    check_test_program_suite(test, "checker/programs/program-asm-goto", clang);
}

static void check_test_program_asm(CheckTest* test, const char* clang)
{
    // F3-prefixed scans work on baseline too. Compare with clang on the same machine: REP BSR may execute as LZCNT.
    check_test_program_suite(test, "checker/programs/asm-rep-scans", clang);
    check_test_program_asm_room(test);
    check_test_program_asm_kinds(test);
    check_test_program_asm_placement(test);
    check_test_program_asm_unit_placement(test);
    // Statements placed in their caller (#1059), each run against clang's build: values live across them in Tier 0's
    // depth registers and register homes, in a loop and under an expression 18 values deep (past the depth registers,
    // in the deep slots), a + register and a memory operand on the stack below them.
    check_test_program_suite(test, "checker/programs/asm-placed-live-values", clang);
    // #2351's assembled templates: their alignment, bytes and symbol/branch fixups must remain valid at each inline site.
    check_test_program_suite(test, "checker/programs/asm-directive-placement", clang);
    // XGETBV is executable only with XSAVE and OSXSAVE, even on an x86-64 host.
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx) && (ecx & (3u << 26)) == (3u << 26))
    {
        check_test_program_suite(test, "checker/programs/asm-xgetbv", clang);
    }
    else
    {
        printf("  asm-xgetbv: skipped execution (XSAVE or OSXSAVE unavailable)\n");
    }
    // The x87's, MMX's and SSE's state of 512 bytes, saved and restored by fxsave and fxrstor and their 64-bit forms, in
    // aligned arrays, as main.c's test of DAZ saves it: MXCSR's control bits and its mask, which the state holds at bytes
    // 24 and 28, against stmxcsr's.
    check_test_program_suite(test, "checker/programs/asm-fx-state", clang);
    // A function's template's CFI is refused by name: clang adds it to the function's own frame description.
    check_test_program_boundary_reason(test, "int tested(int n){__asm__ volatile(\"pushq %%rbx\\n\\t.cfi_adjust_cfa_offset 8\\n\\tpopq %%rbx\\n\\t.cfi_adjust_cfa_offset -8\" ::: \"memory\");return n;}",
                                       "asm-template-cfi", true, false, 0, 0, "inline assembly CFI directives in a function's template (clang adds them to the function's frame)");
    // interpreter_native_bridge's shape: a frame of its own, the flags, a string copy, SSE2's movq, the x87's load and
    // store, a call through a register and a numeric label's branch.
    check_test_program_suite(test, "checker/programs/asm-naked-bridge", clang);
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
    check_test_program_suite(test, "checker/programs/asm-register-variable-mmx-x87", clang);
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
    check_test_program_suite(test, "checker/programs/asm-naked-redeclared", clang);
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
    check_test_program_suite(test, "checker/programs/asm-flag-output-fallback", clang);
    check_test_program_asm_flags(test, clang);
    check_test_program_asm_goto(test, clang);
    // The forms of musl's x86_64 atomics (arch/x86_64/atomic_arch.h), in functions of this test's own: lock prefixes as
    // statements of their own before cmpxchg, xadd, and, or, xor, inc and dec on memory outputs (an "=m" one beside
    // an "m" input of its object, and "+m"), xchg and `lock xchg` (#1846), a store fenced by a locked or of the stack, pause, the bit scans
    // (of operands never zero, whose result the scans leave undefined), and a load of the thread pointer through %fs.
    // hlt is compiled, assembled and linked, never run.
    check_test_program_suite(test, "checker/programs/asm-lock-prefix-statements", clang);
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
    check_test_program_suite(test, "checker/programs/asm-register-variables-syscalls", clang);
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
    check_test_program_suite(test, "checker/programs/asm-x87-library-shapes", clang);
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
    check_test_program_suite(test, "checker/programs/asm-flag-output-named", clang);
    // Flag outputs (#147): of an object no integer store covers, and with another alternative.
    check_test_program_boundary_reason(test, "struct three{char c[3];};int tested(int n){struct three z;__asm__(\"testl %1, %1\" : \"=@ccz\"(z) : \"r\"(n));return z.c[0];}",
                                       "asm-flag-output-size", true, false, 0, 0, "inline assembly flag outputs of this size");
    check_test_program_boundary_reason(test, "int tested(int n){int z;__asm__(\"testl %1, %1\" : \"=r@ccz\"(z) : \"r\"(n));return z;}",
                                       "asm-flag-output-alternatives", true, false, 0, 0, "inline assembly flag outputs with other alternatives");
    // asm goto's labels as data and as addresses (#1845's item 1): the template assembled whole in its helper, whose exits
    // the uses take, as clang's take its indirect targets' blocks: the Linux kernel's __jump_table entries (`.quad 1b,
    // %l[yes]`, and the relative `.long %l[yes] - .`), a rip-relative address the template jumps through, by a directive
    // template and a record one, with an output stored on each way out; run against clang's build.
    check_test_program_suite(test, "checker/programs/asm-goto-label-data", clang);
    check_test_program_boundary_reason(test, "int tested(int n){int i=n;__asm__(\"\" : \"+t\"(i));return i;}",
                                       "asm-x87-integer", true, false, 0, 1, "inline assembly operands of this type in their constraint's registers");
    check_test_program_boundary_reason(test, "int tested(int n){long double x=n;__asm__ volatile(\"\" : : \"u\"(x));return n;}",
                                       "asm-x87-gap", true, false, 0, 1, "inline assembly's x87 operands and clobbers off the stack's top");
    // Two operands at one depth (#1846), one register's as clang's code makes them: two inputs' depth takes the first
    // matching or `+` input's value in clang's order of the inputs (the explicit ones, then the `+` outputs'), else the
    // last other input's; two outputs at one depth each take its value, a template's push's too.
    check_test_program_suite(test, "checker/programs/asm-x87-doubled", clang);
    check_test_program_boundary_reason(test, "int tested(int n){long double x=n;__asm__ volatile(\"\" : : \"t\"(x) : \"st(1)\");return n;}",
                                       "asm-x87-clobber", true, false, 0, 1, "inline assembly's x87 operands and clobbers off the stack's top");
    // x87 registers named in templates (#147): %0 of t as %st, of u as %st(1), the stack's registers by name, an
    // exchange, AT&T's reversed subtraction with a pop, a compare into the flags, and ffreep's pop (#1223).
    check_test_program_suite(test, "checker/programs/asm-x87-named", clang);
    check_test_program_boundary_reason(test, "int tested(int n){long m=n;__asm__(\"movq %q0, %0\" : \"+y\"(m));return (int)m;}",
                                       "asm-mmx-modifier", true, false, 0, 1, "this inline assembly operand modifier");
    // MMX and x87 operands in one statement (#1846, the owner's decision on #1447): the x87's loaded first and the MMX
    // ones after, the MMX outputs stored first, as clang's code orders them; the x87 output's bytes, whatever the MMX
    // load left on its stack, are the same instructions' as clang's, compared bit for bit.
    check_test_program_suite(test, "checker/programs/asm-mmx-x87", clang);
    // A template that returns is an asm body, whose return leaves its caller (#1839, test-checker-driver's returning templates);
    // one that keeps its helper, as a named label's does, is refused by name.
    check_test_program_boundary_reason(test, "int tested(int n){__asm__ volatile(\"rep ret\\n.Lafter%=:\" ::: \"memory\");return n;}",
                                      "asm-returning-template", true, false, 0, 0, "inline assembly templates that return from their helper");
    // rsp's explicit register variable (#1846, the owner's decision on #1447), as clang's code takes it: an output read
    // from rsp after the template (one of no instructions too), an input moved into rsp before it at the variable's width
    // (sp's movw keeps rsp's other bits), the template naming it %rsp, and an input beside an rsp output sharing it. Each
    // input is the value rsp has, as a program's must be; a template that keeps a helper refuses it by name.
    check_test_program_suite(test, "checker/programs/asm-register-variable-rsp", clang);
    check_test_program_boundary_reason(test, "int tested(int n){register long r __asm__(\"rsp\");__asm__ volatile(\".p2align 4\\n\\tpushq %%rax\\n\\tmovq %0, %%rax\\n\\tpopq %%rax\" : : \"r\"(r) : \"rax\");return n;}",
                                       "asm-register-variable-rsp-directives", true, false, 0, 0, "explicit register variables in rsp in templates that take directives");
    // Explicit register variables the lowering doesn't place: in an XMM register past XMM15 without AVX-512F; an input of a
    // memory constraint, which clang's code refuses too; and an integer in an XMM register.
    check_test_program_suite(test, "checker/programs/asm-register-variable-high", clang);
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
    check_test_program_suite(test, "checker/programs/asm-vector-operands-sse", clang);
    // A register variable of another size than its register's (clang's "does not match required type"), and a directive
    // template whose stack mutations keep it out of the caller. An x or v operand of 32 bytes without AVX, or 64 without
    // AVX-512F, is clang's Sema error now (#2231, test-checker-driver's asm operand sizes), before the lowering.
    check_test_program_boundary_reason(test, "typedef float v4sf __attribute__((vector_size(16)));int tested(int n){register v4sf a __asm__(\"ymm3\")={(float)n};__asm__(\"nop\" : \"+x\"(a));return (int)a[0];}",
                                       "asm-vector-register-size", true, false, 0, 1, "inline assembly operands of this type in their constraint's registers");
    check_test_program_boundary_reason(test, "typedef float v4sf __attribute__((vector_size(16)));int tested(int n){v4sf a={(float)n};__asm__(\".p2align 4\\n\\tpushq %%rax\\n\\taddps %0, %0\\n\\tpopq %%rax\" : \"+x\"(a));return (int)a[0];}",
                                       "asm-vector-directives", true, false, 0, 1, "inline assembly vector operands in templates that take directives");
}

#endif
