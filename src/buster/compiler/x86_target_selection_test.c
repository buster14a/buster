// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent #180 consumer of the existing selector interfaces, included after their tests in main.c. The Tier 0
// audit covers every compiled template plus generated deferred/ABI/frame records. Tier 1's existing independent
// semantic/native fixtures are replayed with the requested policy, including both covers and both allocation paths.
// This does not change the selectors' implementation or assert full C-source/corpus acceptance.

static __attribute__((no_sanitize("function"))) uint64_t x86_target_test_call_kernel(const void* code)
{
    uint64_t (*function)(void);
    memcpy(&function, &code, sizeof(function));
    return function();
}

// An original record-level fixture, not a new selector rule or permission to contract C expressions. Baseline uses
// MULSS/SUBSS; v3 uses VEX VFMSUB; v4 uses EVEX VFMSUB. The exact result (1.5 * 2 - .25 = 2.75) is 0x40300000.
static bool x86_target_test_native_kernel(const X86Target* target, const uint8_t* allowed, const char* profile)
{
    X86Instruction records[11] = {
        { .form = X86_FORM_MOV_R32_IMM32, .rm = X86_REGISTER_RAX, .immediate = 0x3fc00000 },
        { .form = X86_FORM_MOVD_X_RM32, .reg = 0, .rm = X86_REGISTER_RAX },
        { .form = X86_FORM_MOV_R32_IMM32, .rm = X86_REGISTER_RAX, .immediate = 0x40000000 },
        { .form = X86_FORM_MOVD_X_RM32, .reg = 1, .rm = X86_REGISTER_RAX },
        { .form = X86_FORM_MOV_R32_IMM32, .rm = X86_REGISTER_RAX, .immediate = 0x3e800000 },
        { .form = X86_FORM_MOVD_X_RM32, .reg = 2, .rm = X86_REGISTER_RAX },
    };
    const bool evex = (target->features & BUSTER_X86_FEATURE(AVX512F)) != 0;
    const bool vex = (target->features & BUSTER_X86_FEATURE(FMA)) != 0;
    uint32_t count = 6;
    if (vex)
    {
        records[count++] = (X86Instruction){ .form = evex ? X86_FORM_VFMSUB132SS_X_X_XM32_EVEX : X86_FORM_VFMSUB132SS_X_X_XM32, .reg = 0, .vvvv = 2, .rm = 1 };
    }
    else
    {
        records[count++] = (X86Instruction){ .form = X86_FORM_MULSS_X_XM32, .reg = 0, .rm = 1 };
        records[count++] = (X86Instruction){ .form = X86_FORM_SUBSS_X_XM32, .reg = 0, .rm = 2 };
    }
    records[count++] = (X86Instruction){ .form = X86_FORM_MOVD_RM32_X, .reg = 0, .rm = X86_REGISTER_RAX };
    if (vex)
    {
        records[count++] = (X86Instruction){ .form = X86_FORM_VZEROUPPER };
    }
    records[count++] = (X86Instruction){ .form = X86_FORM_RET };
    uint8_t scalar[BUSTER_X86_ENCODER_CAPACITY(11)];
    uint8_t vector[BUSTER_X86_ENCODER_CAPACITY(11)];
    const X86Target native = x86_target_native();
    bool passed = (native.features & target->features) == target->features && x86_target_validate(allowed, records, count) == count;
    const int64_t bytes = passed ? x86_encode_scalar(records, count, scalar, sizeof(scalar), 0, 0) : -1;
    passed &= bytes > 0 && x86_encode(records, count, vector, sizeof(vector), 0, 0) == bytes;
    passed &= bytes > 0 && memcmp(scalar, vector, bytes > 0 ? (size_t)bytes : 0) == 0;
    uint8_t lower[X86_FORM_COUNT];
    if (vex)
    {
        X86Target denied;
        passed &= x86_target_parse(evex ? "x86-64-v3" : "x86-64", evex ? 9 : 6, &denied);
        x86_target_forms(&denied, lower);
        passed &= x86_target_validate(lower, records, count) == 6;
    }
    void* code = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    passed &= code != MAP_FAILED;
    if (passed)
    {
        memcpy(code, vector, (size_t)bytes);
        passed &= mprotect(code, 4096, PROT_READ | PROT_EXEC) == 0;
        for (uint32_t i = 0; passed && i < 64; i += 1)
        {
            passed &= x86_target_test_call_kernel(code) == UINT64_C(0x40300000);
        }
    }
    if (code != MAP_FAILED)
    {
        munmap(code, 4096);
    }
    printf("target %s native %s kernel: %s (%lld bytes, exact scalar/vector bytes; lower-profile rejection)\n", profile, evex ? "EVEX" : vex ? "VEX" : "SSE", passed ? "pass" : "FAIL", (long long)bytes);
    fflush(stdout);
    return passed;
}

static bool x86_target_test_tier0(const uint8_t* allowed, const char* profile)
{
    bool passed = true;
    uint64_t records = 0;
    for (uint32_t t = 0; t < tier0_template_count; t += 1)
    {
        const Tier0Template* template = &tier0_templates[t];
        for (uint32_t r = 0; r < template->length; r += 1)
        {
            const uint32_t form = template->specs[r].form;
            if (!x86_target_form_allowed(allowed, form))
            {
                fprintf(stderr, "target %s: Tier 0 template %u record %u has unavailable form %u\n", profile, t, r, form);
                passed = false;
            }
        }
    }
    Tier0Tables tables = {0};
    Arena input = arena_reserve(64u << 20);
    Arena output = arena_reserve(16u << 20);
    bool ready = input.base != 0 && output.base != 0 && tier0_tables_open(&tables);
    uint32_t functions = 0;
    uint32_t refused = 0;
    for (uint64_t seed = 1; ready && seed <= 24; seed += 1)
    {
        arena_pop_to(&input, 0);
        Tier0Program* program = BUSTER_ARENA_PUSH_ARRAY(&input, Tier0Program, 1);
        Tier0Functions* views = BUSTER_ARENA_PUSH_ARRAY(&input, Tier0Functions, 1);
        const Tier0ProgramOptions options = {
            .functions = 4,
            .statements = 12,
            .expression_nodes = 12,
            .calls = true,
            .loops = true,
            .floating = true,
            .pointers = true,
            .defined = true,
            .objects = true,
            .indirect = seed % 2 != 0,
            .records = seed % 3 != 0 ? &tables.records : 0,
            .access = seed % 3 == 0,
            .vlas = seed % 4 == 0,
        };
        ready = program != 0 && views != 0 && tier0_generate_program(program, &input, seed, options, "isa_") && tier0_program_functions(program, &tables, &input, views);
        for (uint32_t f = 0; ready && f < views->count; f += 1)
        {
            arena_pop_to(&output, 0);
            const Tier0Function* function = &views->functions[f];
            ready = tier0_validate(function).error == TIER0_OK;
            const Tier0Code scalar = ready ? tier0_compile_scalar(function, &output) : (Tier0Code){ .error = TIER0_ERROR_DEPTH };
            const Tier0Code vector = ready ? tier0_compile(function, &output) : (Tier0Code){ .error = TIER0_ERROR_DEPTH };
            passed &= ready && scalar.error == vector.error;
            if (scalar.error == TIER0_OK && vector.error == TIER0_OK)
            {
                passed &= tier0_test_same_code(&scalar, &vector);
                const uint32_t valid = x86_target_validate(allowed, scalar.records, scalar.count);
                if (valid != scalar.count)
                {
                    fprintf(stderr, "target %s: Tier 0 seed %llu function %u record %u of %u has unavailable/invalid form %u\n", profile, (unsigned long long)seed, f, valid, scalar.count, scalar.records[valid].form);
                    passed = false;
                }
                records += scalar.count;
                functions += 1;
            }
            else
            {
                // These are bounded generator cases, not permission to accept an ISA failure.
                passed &= scalar.error == vector.error && scalar.error == TIER0_ERROR_DEPTH;
                refused += 1;
            }
        }
    }
    passed &= ready && functions != 0 && refused == 0;
    printf("target %s Tier 0: %s (%u templates, %u functions / %llu records, %u explicit depth refusals; exact twins)\n", profile, passed ? "pass" : "FAIL", tier0_template_count, functions, (unsigned long long)records, refused);
    fflush(stdout);
    arena_release(&input);
    arena_release(&output);
    tier0_tables_close(&tables);
    return passed;
}

static bool x86_target_test_tier1(uint8_t* allowed, const char* profile)
{
    Tier1SelectTest selection = { .allowed = allowed };
    selection.executable = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool ready = selection.executable != MAP_FAILED;
    if (ready)
    {
        tier1_select_test_patterns(&selection);
        tier1_select_test_memory(&selection);
        tier1_select_test_branches(&selection);
        tier1_select_test_plans(&selection);
        tier1_select_test_memory_plans(&selection);
        tier1_select_test_errors(&selection);
        tier1_select_test_guarded(&selection);
        tier1_select_test_calls(&selection);
        tier1_select_test_call_barriers(&selection);
        tier1_select_test_conversions(&selection);
        tier1_select_test_narrow_memory(&selection);
        tier1_select_test_narrow_values(&selection);
        tier1_select_test_narrow_refusals(&selection);
        tier1_select_test_unreachable(&selection);
        munmap(selection.executable, 4096);
    }
    Tier1EmitTest emission = {
        .input = arena_reserve(32u << 20),
        .output = arena_reserve(64u << 20),
        .scratch = arena_reserve(32u << 20),
        .allowed = allowed,
    };
    ready = ready && emission.input.base != 0 && emission.output.base != 0 && emission.scratch.base != 0;
    if (ready)
    {
        tier1_emit_test_pressure(&emission);
        tier1_emit_test_failures(&emission);
        tier1_call_tests(&emission);
        tier1_narrow_tests(&emission);
        tier1_terminal_tests(&emission);
    }
    const bool passed = ready && selection.failures == 0 && emission.failures == 0;
    printf("target %s Tier 1: %s (%llu selection / %llu native-emission checks; both covers and allocation paths)\n", profile, passed ? "pass" : "FAIL", (unsigned long long)selection.checks, (unsigned long long)emission.checks);
    fflush(stdout);
    arena_release(&emission.input);
    arena_release(&emission.output);
    arena_release(&emission.scratch);
    return passed;
}

// Compare cached admission with the direct, uncached predicate for every row. This also catches a future
// syntax predicate gaining an input that was omitted from the cache key, including false-positive admission.
static bool x86_target_test_matcher_admission(void)
{
    Arena arena = arena_reserve(1ull << 20);
    X86MatchIndex* index = BUSTER_ARENA_PUSH_ARRAY(&arena, X86MatchIndex, 1);
    bool passed = index != 0;
    static const char* const profiles[] = { "x86-64", "x86-64-v2", "x86-64-v3", "x86-64-v4", "znver5", "native" };
    uint32_t configurations = 0;
    uint8_t allowed[X86_FORM_COUNT];
    uint8_t seen[X86_FORM_COUNT];
    for (uint32_t p = 0; index != 0 && p <= BUSTER_ARRAY_LENGTH(profiles); p += 1)
    {
        X86Target target = { .features = UINT64_MAX };
        if (p < BUSTER_ARRAY_LENGTH(profiles))
        {
            passed &= x86_target_parse(profiles[p], strlen(profiles[p]), &target);
        }
        const uint64_t original = target.features;
        for (uint32_t bit = 0; bit <= X86_FEATURE_COUNT; bit += 1)
        {
            target.features = bit == X86_FEATURE_COUNT ? original : original & ~(UINT64_C(1) << bit);
            x86_target_forms_scalar(&target, allowed);
            x86_match_initialize(index, &target);
            memset(seen, 0, sizeof(seen));
            const uint32_t count = index->begin[BUSTER_X86_MATCH_BUCKETS];
            passed &= count <= X86_FORM_COUNT;
            for (uint32_t at = 0; at < count && at < X86_FORM_COUNT; at += 1)
            {
                const uint32_t form = index->forms[at];
                passed &= form < X86_FORM_COUNT;
                if (form < X86_FORM_COUNT)
                {
                    passed &= seen[form] == 0;
                    seen[form] = 1;
                }
            }
            for (uint32_t form = 0; form < X86_FORM_COUNT; form += 1)
            {
                uint8_t roles[4];
                const X86FormDescription* row = &x86_form_descriptions[form];
                const bool expected = form < BUSTER_X86_FORM_LEGACY_MODES && allowed[form] && x86_match_supported(row) && x86_match_roles(row->syntax, roles) <= 4;
                passed &= seen[form] == expected;
            }
            configurations += 1;
        }
    }
    printf("target matcher admission: %s (%u configurations, every form against uncached syntax and scalar ISA)\n", passed ? "pass" : "FAIL", configurations);
    arena_release(&arena);
    return passed;
}

bool run_x86_target_selection_tests(void)
{
    bool passed = x86_encoder_initialize() && x86_semantics_initialize() && x86_zen5_initialize() && tier0_initialize();
    passed &= x86_target_test_matcher_admission();
    fflush(0);
    const pid_t child = passed ? fork() : -1;
    if (child == 0)
    {
        static const char* const profiles[] = { "x86-64", "x86-64-v3", "x86-64-v4" };
        uint8_t allowed[X86_FORM_COUNT];
        for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(profiles); p += 1)
        {
            X86Target target;
            passed &= x86_target_parse(profiles[p], strlen(profiles[p]), &target);
            x86_target_forms(&target, allowed);
            passed &= x86_target_test_tier0(allowed, profiles[p]);
            passed &= x86_target_test_tier1(allowed, profiles[p]);
            passed &= x86_target_test_native_kernel(&target, allowed, profiles[p]);
        }
        fflush(0);
        _exit(passed ? 0 : 1);
    }
    int status = 0;
    pid_t waited;
    do
    {
        waited = child > 0 ? waitpid(child, &status, 0) : -1;
    }
    while (child > 0 && waited < 0 && errno == EINTR);
    passed &= child > 0 && waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!passed)
    {
        fprintf(stderr, "target selector/native verification failed (child status %d)\n", status);
    }
    return passed;
}
