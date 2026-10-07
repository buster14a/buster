// Test registration and the in-process runner. test_descriptors is the
// single table every module test registers in — a new test pair adds its
// row here in registration order, its sources to CMakeLists.txt, and its
// includes below (AGENTS.md). library_tests runs the table, prints the
// TEST_MODULE_TIMING lines the test_timing_summary diagnostic consumes,
// and honors BUSTER_TEST_JOBS. Private child payloads dispatch before prewarm;
// test_os_with_fatal_child_isolation checks that fatal probes run no modules.
// Arena scopes publish TEST_ARENA_V1 and
// TEST_ARENA_TOP_V1 counters with diagnostics copied before rewind. Opt-in
// TEST_FIXTURE_TIMING_V1 rows observe the same scopes; see docs/driver-test-timing.md.
// The same scopes print TEST_FIXTURE_START_V1 on entry and feed the fixture
// watchdog (TestWatchSlot, test_watchdog_start, test_watchdog_thread), which
// ends a run stuck in one scope past BUSTER_TEST_FIXTURE_TIMEOUT_SECONDS;
// test_watchdog_child_run is its private hanging payload. Parallel gang
// boundaries are written live around deterministic lane replay, and
// test_parallel_crash_child_self_test covers abrupt lane exit without changing
// registered assertion or TEST_MODULE_TIMING totals. iOS launch observations
// bracket library_tests preparation before the first fixture can report.
// Deliberate harness failures preserve their diagnostics and accounting while
// suppressing only their debugger stop; test_debugger_failure_self_test checks
// that ordinary argument-bearing and argument-free failures still stop.
// A descriptor marked table_audit runs only
// on the canonical tree per platform (BUSTER_TEST_TABLE_AUDITS, default
// on) — reserve that flag for results that are a pure function of the
// generated tables and repository source. `ide test --module=a,b` narrows a
// run to named descriptors (test_module_selection_resolve); library_tests
// marks the others deselected in its working copy of the table. The opt-in
// BUSTER_TEST_MODULE_GROUP=primary|rest selects complementary process groups
// (test_module_group_resolve), preserving audit ownership and table indices.
// Its inventory value queries the table without running any registered module.
// test_native_host_profile records the same binary's usable native features
// and compiled SIMD tiers only for that independent inventory query.
// CI_UNIT_MODULE_V1 inventories and the post-cleanup CI_UNIT_BATCH_V1 bind
// selected timing rows to the complete registered suite for the parent.

#include <buster/tests/test.h>
#include <buster/lib/entry_point.h>

#include <buster/lib/os.h>
#include <buster/lib/arena.h>
#include <buster/lib/string.h>
#include <buster/lib/file.h>
#include <buster/lib/hash.h>
#include <buster/lib/time.h>
#include <buster/lib/simd.h>
#include <buster/lib/system_headers.h>
#if BUSTER_CPU_ARCH_X86_64
#include <buster/lib/x86_64.h>
#endif

#if BUSTER_INCLUDE_TESTS
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/assembly/aarch64_exact_bridge.h>
#include <buster/lib/compiler/assembly/aarch64_control_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_system_registers.h>
#include <buster/lib/compiler/assembly/aarch64_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_system_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_syntax.h>
#include <buster/lib/compiler/assembly/aarch64_semantic_vm.h>
#include <buster/lib/compiler/assembly/aarch64_direct_simd_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_complex_simd_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_memory_semantics.h>
#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/compiler/assembly/x86_64_completion_census.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/compiler/llvm/bitcode.h>
#include <buster/lib/compiler/dwarf/dwarf.h>
#include <buster/lib/compiler/codeview/codeview.h>
#include <buster/lib/compiler/pdb/pdb.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/compiler/jit/jit.h>
#include <buster/lib/compiler/link/link.h>
#include <buster/lib/compiler/driver/driver.h>

#include <buster/tests/byte_writer_test.h>
#include <buster/tests/arena_test.h>
#include <buster/tests/integer_test.h>
#include <buster/tests/sanitizer_test.h>
#include <buster/tests/hash_test.h>
#include <buster/tests/simd_test.h>
#include <buster/tests/string_test.h>
#include <buster/tests/os_test.h>
#include <buster/tests/file_test.h>
#include <buster/tests/target_test.h>
#include <buster/tests/truetype_test.h>
#include <buster/tests/image_test.h>
#include <buster/tests/compiler/metamorphic/metamorphic_test.h>
#include <buster/tests/compiler/frontend/c/c_test.h>
#include <buster/tests/compiler/frontend/c/once_test.h>
#include <buster/tests/compiler/frontend/c/type_layout_test.h>
#include <buster/tests/compiler/frontend/c/macro_conditional_test.h>
#include <buster/tests/compiler/frontend/c/record_layout_test.h>
#include <buster/tests/compiler/assembly/aarch64_encoding_test.h>
#include <buster/tests/compiler/assembly/aarch64_exact_bridge_test.h>
#include <buster/tests/compiler/assembly/aarch64_base_assembly_test.h>
#include <buster/tests/compiler/assembly/aarch64_control_semantics_test.h>
#include <buster/tests/compiler/assembly/aarch64_system_registers_test.h>
#include <buster/tests/compiler/assembly/aarch64_semantics_test.h>
#include <buster/tests/compiler/assembly/aarch64_system_semantics_test.h>
#include <buster/tests/compiler/assembly/aarch64_syntax_test.h>
#include <buster/tests/compiler/assembly/aarch64_semantic_vm_test.h>
#include <buster/tests/compiler/assembly/aarch64_direct_simd_test.h>
#include <buster/tests/compiler/assembly/aarch64_complex_simd_test.h>
#include <buster/tests/compiler/assembly/aarch64_memory_semantics_test.h>
#include <buster/tests/compiler/assembly/aarch64_alias_projection_test.h>
#include <buster/tests/compiler/assembly/assembly_test.h>
#include <buster/tests/compiler/assembly/x86_64_forwarding_test.h>
#include <buster/tests/compiler/assembly/x86_64_metadata_test.h>
#include <buster/tests/compiler/assembly/x86_64_tls_test.h>
#include <buster/tests/compiler/assembly/padding_test.h>
#include <buster/tests/compiler/assembly/x86_64_got_test.h>
#include <buster/tests/compiler/assembly/x86_64_completion_census_test.h>
#include <buster/tests/compiler/diagnostic_test.h>
#include <buster/tests/compiler/ir/ir_test.h>
#include <buster/tests/compiler/ir/ir_oracle_test.h>
#include <buster/tests/compiler/ir/vector_contract_test.h>
#include <buster/tests/compiler/llvm/bitcode_test.h>
#include <buster/tests/compiler/codegen/machine_select_test.h>
#include <buster/tests/compiler/codegen/machine_test.h>
#include <buster/tests/compiler/codegen/codegen_test.h>
#include <buster/tests/compiler/codegen/investigation_test.h>
#include <buster/tests/compiler/codegen/debug_location_block_start_test_internal.h>
#include <buster/tests/compiler/codegen/aarch64_stride_test.h>
#include <buster/tests/compiler/debug/debug_test.h>
#include <buster/tests/compiler/dwarf/dwarf_test.h>
#include <buster/tests/compiler/codeview/codeview_test.h>
#include <buster/tests/compiler/pdb/pdb_test.h>
#include <buster/tests/compiler/object/object_test.h>
#include <buster/tests/compiler/jit/jit_test.h>
#include <buster/tests/compiler/link/link_test.h>
#include <buster/tests/compiler/gpu/gpu_test.h>
#include <buster/tests/compiler/spirv/spirv_test.h>
#include <buster/tests/compiler/driver/driver_test.h>
#include <buster/tests/compiler/driver/object_path_test.h>

#if BUSTER_CPU_ARCH_X86_64
#include <buster/tests/x86_64_test.h>
#endif

#if BUSTER_UNITY_BUILD
#include <buster/tests/byte_writer_test.c>
#include <buster/tests/arena_test.c>
#include <buster/tests/integer_test.c>
#include <buster/tests/sanitizer_test.c>
#include <buster/tests/hash_test.c>
#include <buster/tests/simd_test.c>
#include <buster/tests/string_test.c>
#include <buster/tests/os_test.c>
#include <buster/tests/file_test.c>
#include <buster/tests/target_test.c>
#include <buster/tests/truetype_test.c>
#include <buster/tests/image_test.c>
#include <buster/tests/compiler/metamorphic/metamorphic_test.c>
#include <buster/tests/compiler/frontend/c/c_test.c>
#include <buster/tests/compiler/frontend/c/once_test.c>
#include <buster/tests/compiler/frontend/c/type_layout_test.c>
#include <buster/tests/compiler/frontend/c/macro_conditional_test.c>
#include <buster/tests/compiler/frontend/c/record_layout_test.c>
#include <buster/tests/compiler/assembly/aarch64_encoding_test.c>
#include <buster/tests/compiler/assembly/aarch64_exact_bridge_test.c>
#include <buster/tests/compiler/assembly/aarch64_base_assembly_test.c>
#include <buster/tests/compiler/assembly/aarch64_control_semantics_test.c>
#include <buster/tests/compiler/assembly/aarch64_system_registers_test.c>
#include <buster/tests/compiler/assembly/aarch64_semantics_test.c>
#include <buster/tests/compiler/assembly/aarch64_system_semantics_test.c>
#include <buster/tests/compiler/assembly/aarch64_syntax_test.c>
#include <buster/tests/compiler/assembly/aarch64_semantic_vm_test.c>
#include <buster/tests/compiler/assembly/aarch64_direct_simd_test.c>
#include <buster/tests/compiler/assembly/aarch64_complex_simd_test.c>
#include <buster/tests/compiler/assembly/aarch64_memory_semantics_test.c>
#include <buster/tests/compiler/assembly/aarch64_alias_projection_test.c>
#include <buster/tests/compiler/assembly/assembly_test.c>
#include <buster/tests/compiler/assembly/x86_64_forwarding_test.c>
#include <buster/tests/compiler/assembly/x86_64_metadata_test.c>
#include <buster/tests/compiler/assembly/x86_64_tls_test.c>
#include <buster/tests/compiler/assembly/padding_test.c>
#include <buster/tests/compiler/assembly/x86_64_got_test.c>
#include <buster/tests/compiler/assembly/x86_64_completion_census_test.c>
#include <buster/tests/compiler/diagnostic_test.c>
#include <buster/tests/compiler/ir/ir_test.c>
#include <buster/tests/compiler/ir/ir_oracle_test.c>
#include <buster/tests/compiler/ir/vector_contract_test.c>
#include <buster/tests/compiler/llvm/bitcode_test.c>
#include <buster/tests/compiler/codegen/machine_select_test.c>
#include <buster/tests/compiler/codegen/machine_test.c>
#include <buster/tests/compiler/codegen/codegen_test.c>
#include <buster/tests/compiler/codegen/investigation_test.c>
#include <buster/tests/compiler/codegen/aarch64_stride_test.c>
#include <buster/tests/compiler/debug/debug_test.c>
#include <buster/tests/compiler/dwarf/dwarf_test.c>
#include <buster/tests/compiler/codeview/codeview_test.c>
#include <buster/tests/compiler/pdb/pdb_test.c>
#include <buster/tests/compiler/object/object_test.c>
#include <buster/tests/compiler/jit/jit_test.c>
#include <buster/tests/compiler/link/link_test.c>
#include <buster/tests/compiler/gpu/gpu_test.c>
#include <buster/tests/compiler/spirv/spirv_test.c>
#include <buster/tests/compiler/driver/driver_test.c>
#include <buster/tests/compiler/driver/object_path_test.c>
#if BUSTER_CPU_ARCH_X86_64
#include <buster/tests/x86_64_test.c>
#endif
#endif

#endif

BUSTER_GLOBAL_LOCAL bool buster_test_debugger_stop_requested(UnitTestArguments* arguments, bool debugger_present)
{
    bool result = debugger_present;
#if BUSTER_INCLUDE_TESTS
    result = result && (!arguments || !arguments->suppress_debugger_break);
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}

#if BUSTER_INCLUDE_TESTS
typedef struct TestDescriptor TestDescriptor;
typedef enum TestDescriptorParallelKind
{
    TEST_DESCRIPTOR_PARALLEL_NONE,
    TEST_DESCRIPTOR_PARALLEL_AARCH64_DIRECT_SIMD,
    TEST_DESCRIPTOR_PARALLEL_AARCH64_COMPLEX_SIMD,
    TEST_DESCRIPTOR_PARALLEL_AARCH64_MEMORY_SEMANTICS,
} TestDescriptorParallelKind;
struct TestDescriptor
{
    String8 name;
    TestFunction* function;
    bool requires_temporary_root;
    TestDescriptorParallelKind parallel_kind;
    // A whole-table audit: its answer is a function of the generated
    // metadata tables and the source text alone, so it cannot differ between
    // compilers, configurations or optimization levels. The matrix runs it on
    // one canonical tree per platform rather than in all eight to ten, the
    // same carve-out clang_analyze already has. Off means "some other tree in
    // this matrix runs it", never "nobody does".
    bool table_audit;
    // Set only in library_tests' working copy of the table, for a module a
    // `--module=` or process-group selection leaves out. Zero runs the descriptor.
    bool deselected;
};

// Table audits run unless the superbuild explicitly says another tree owns
// them. Defaulting to *on* keeps a bare `ide test`, a single-tree build and
// any future runner at full coverage; only the matrix opts a tree out.
BUSTER_GLOBAL_LOCAL bool buster_test_table_audits_enabled(void)
{
    String8 value = os_get_environment_variable(S8("BUSTER_TEST_TABLE_AUDITS"));
    return !value.length || !string_equal(value, S8("0"));
}

BUSTER_GLOBAL_LOCAL bool buster_test_descriptor_runs(TestDescriptor descriptor)
{
    return !descriptor.deselected && (!descriptor.table_audit || buster_test_table_audits_enabled());
}

BUSTER_GLOBAL_LOCAL bool test_fixture_timing_selected(String8 selection, String8 module)
{
    return selection.length && module.length &&
           (string_equal(selection, S8("all")) || string_equal(selection, module));
}

// The fixture watchdog. Each scope transition publishes the module's innermost
// open scope to that module's slot; a thread polls the slots and, when one
// position has stayed current past the deadline, prints TEST_FIXTURE_TIMEOUT_V1
// and terminates the process. Positions reset at every begin and end, so the
// deadline bounds the longest stretch between transitions, not a module.
//
// The default is generous: the slowest recorded module, compiler_driver_tests
// under sanitized Clang Debug on hosted Windows x86-64, took 414 s in total
// across 39 scopes (docs/driver-test-timing.md). It stays far below the
// 300-minute desktop job budget that a hang used to exhaust silently.
enum
{
    TEST_FIXTURE_TIMEOUT_DEFAULT_SECONDS = 1800,
    // timeout(1)'s status, so logs and wrappers read it as a deadline.
    TEST_FIXTURE_TIMEOUT_EXIT_CODE = 124,
    TEST_WATCHDOG_POLL_MILLISECONDS = 50,
};

#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS && !BUSTER_SINGLE_THREADED
// Mobile launchers own their own shorter deadlines; single-threaded builds
// have no second thread to watch from.
#define BUSTER_TEST_WATCHDOG_SUPPORTED 1
#else
#define BUSTER_TEST_WATCHDOG_SUPPORTED 0
#endif

// A seqlock with one writer, the thread running the module: sequence is odd
// while it rewrites the fields, and a reader trusts only fields read between
// two equal even values. Names are static tokens, like the arena records'.
struct TestWatchSlot
{
    AtomicU64 sequence;
    String8 module;
    // Empty while no scope of the module is open.
    String8 fixture;
    String8 last_completed;
    u64 index;
    u64 since_microseconds;
    bool module_scope;
    u8 reserved[7];
};

typedef struct TestWatchSnapshot TestWatchSnapshot;
struct TestWatchSnapshot
{
    String8 module;
    String8 fixture;
    String8 last_completed;
    u64 index;
    u64 since_microseconds;
    bool module_scope;
    bool consistent;
    u8 reserved[6];
};

typedef struct TestFixtureDeadline TestFixtureDeadline;
struct TestFixtureDeadline
{
    u64 seconds;
    bool valid;
    u8 reserved[7];
};

// Empty selects the default and zero disables the watchdog. Anything else must
// be a whole decimal count of seconds that still fits in microseconds; an
// invalid value keeps the default.
BUSTER_GLOBAL_LOCAL TestFixtureDeadline test_fixture_deadline_parse(String8 text)
{
    TestFixtureDeadline result = {.seconds = TEST_FIXTURE_TIMEOUT_DEFAULT_SECONDS, .valid = true};
    if (text.length)
    {
        IntegerParsingU64 parsed = string8_parse_u64_decimal(text);
        result.valid = parsed.status == INTEGER_PARSING_SUCCESS && parsed.length == text.length && parsed.value <= UINT64_MAX / 1000000;
        result.seconds = result.valid ? parsed.value : result.seconds;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void test_watch_publish(TestWatchSlot* slot, String8 module, String8 fixture, u64 index, bool module_scope, String8 last_completed)
{
    // Sequentially consistent read-modify-writes keep the plain stores between
    // them on every compiler, including MSVC's Interlocked implementation.
    atomic_u64_increment(&slot->sequence);
    slot->module = module;
    slot->fixture = fixture;
    slot->last_completed = last_completed;
    slot->index = index;
    slot->module_scope = module_scope;
    slot->since_microseconds = os_now_microseconds();
    atomic_u64_increment(&slot->sequence);
}

BUSTER_GLOBAL_LOCAL TestWatchSnapshot test_watch_snapshot(TestWatchSlot* slot)
{
    // Adding zero is the sequentially consistent load every AtomicU64 build has.
    u64 before = atomic_u64_add(&slot->sequence, 0);
    TestWatchSnapshot result = {
        .module = slot->module,
        .fixture = slot->fixture,
        .last_completed = slot->last_completed,
        .index = slot->index,
        .since_microseconds = slot->since_microseconds,
        .module_scope = slot->module_scope,
    };
    u64 after = atomic_u64_add(&slot->sequence, 0);
    result.consistent = before == after && !(before & 1);
    return result;
}

// Inclusive at the deadline. A torn read, a closed module, or a clock sample
// older than the position never expires.
BUSTER_GLOBAL_LOCAL bool test_watch_expired(TestWatchSnapshot snapshot, u64 now_microseconds, u64 deadline_microseconds)
{
    return snapshot.consistent && snapshot.fixture.length && now_microseconds >= snapshot.since_microseconds &&
           now_microseconds - snapshot.since_microseconds >= deadline_microseconds;
}

// elapsed_ms counts from when the named scope last became innermost: its begin
// or the end of its latest nested scope. last_completed is the module's most
// recently closed scope, which localizes a hang in code between fixtures.
BUSTER_GLOBAL_LOCAL String8 test_watch_report(Arena* arena, TestWatchSnapshot snapshot, u64 now_microseconds, u64 deadline_seconds)
{
    return string_format(arena,
        S8("TEST_FIXTURE_TIMEOUT_V1 kind={S8} module={S8} fixture={S8} index={u64} elapsed_ms={u64} deadline_seconds={u64} last_completed={S8}\n"),
        snapshot.module_scope ? S8("module") : S8("fixture"), snapshot.module, snapshot.fixture, snapshot.index,
        (now_microseconds - snapshot.since_microseconds) / 1000, deadline_seconds,
        snapshot.last_completed.length ? snapshot.last_completed : S8("none"));
}

// Slot zero is the supplied arena; the rest are the selected context's scratch
// arenas unless one of them is the supplied arena.
BUSTER_GLOBAL_LOCAL Arena* test_arena_observed(ThreadContext* context, Arena* arena, u32 slot)
{
    Arena* observed = slot ? (context ? context->arenas[slot - 1] : 0) : arena;
    return observed && (!slot || observed != arena) ? observed : 0;
}

// Scope-local peaks include temporary allocations discarded inside the body.
// Saving/restoring the parent's peak makes nested scopes independent of older
// fixtures while preserving the module maximum. Arena zeroing state is untouched.
TestArenaScope buster_test_arena_begin(UnitTestArguments* arguments, Arena* arena, String8 name, bool module)
{
    TestArenaScope result = {.name = name, .index = module ? 0 : arguments->memory_fixture_index++, .module = module,
                            .timing = arguments->fixture_timing_report};
    ThreadContext* context = thread_context_selected();
    if (arguments->memory_report)
    {
        // Published before the scope opens, so a hang or crash inside it still
        // names it; completion records come only from end. As with end's
        // reporting, the formatting scratch stays out of enclosing peaks.
        u64 high_waters[BUSTER_ARRAY_LENGTH(result.marks)] = {0};
        for (u32 slot = 0; slot < BUSTER_ARRAY_LENGTH(result.marks); slot += 1)
        {
            Arena* observed = test_arena_observed(context, arena, slot);
            high_waters[slot] = observed ? observed->high_water : 0;
        }
        arguments->show(arguments, S8("TEST_FIXTURE_START_V1 kind={S8} module={S8} fixture={S8} index={u64}\n"),
                        module ? S8("module") : S8("fixture"), arguments->memory_module, name, result.index);
        for (u32 slot = 0; slot < BUSTER_ARRAY_LENGTH(result.marks); slot += 1)
        {
            Arena* observed = test_arena_observed(context, arena, slot);
            if (observed)
            {
                observed->high_water = high_waters[slot];
            }
        }
    }
    for (u32 slot = 0; slot < BUSTER_ARRAY_LENGTH(result.marks); slot += 1)
    {
        Arena* observed = test_arena_observed(context, arena, slot);
        if (observed)
        {
            result.marks[slot] = (TestArenaMark){.arena = observed, .start = observed->position, .previous_high_water = observed->high_water};
            observed->high_water = observed->position;
        }
    }
    if (module)
    {
        arguments->memory_fixture_index = 0;
        arguments->memory_top_retained_fixture = S8("none");
        arguments->memory_top_peak_fixture = S8("none");
        arguments->memory_top_retained_bytes = 0;
        arguments->memory_top_peak_bytes = 0;
    }
    TestWatchSlot* watch = arguments->watch_slot;
    if (watch)
    {
        // This thread is the slot's only writer, so reading it back is plain.
        result.watch_parent = watch->fixture;
        result.watch_parent_index = watch->index;
        result.watch_parent_module = watch->module_scope;
        test_watch_publish(watch, arguments->memory_module, name, result.index, module, module ? (String8){0} : watch->last_completed);
    }
    if (result.timing)
    {
        result.start = timestamp_take();
    }
    return result;
}

void buster_test_arena_end(UnitTestArguments* arguments, TestArenaScope scope, bool rewind)
{
    // Stop before cleanup/reporting. Enclosing scopes still include nested
    // scope work and output; these are inclusive wall intervals, not CPU time.
    TimeDataType end = {0};
    if (scope.timing)
    {
        end = timestamp_take();
    }
    u64 ends[BUSTER_ARRAY_LENGTH(scope.marks)] = {0};
    u64 peaks[BUSTER_ARRAY_LENGTH(scope.marks)] = {0};
    // Capture every arena before formatting, which itself uses scratch memory.
    for (u32 slot = 0; slot < BUSTER_ARRAY_LENGTH(scope.marks); slot += 1)
    {
        TestArenaMark mark = scope.marks[slot];
        if (mark.arena)
        {
            ends[slot] = mark.arena->position;
            peaks[slot] = BUSTER_MAX(mark.arena->high_water, ends[slot]);
            BUSTER_VALIDATE(ends[slot] >= mark.start);
        }
    }
    if (rewind)
    {
        arena_set_position(scope.marks[0].arena, scope.marks[0].start);
    }
    for (u32 slot = 0; slot < BUSTER_ARRAY_LENGTH(scope.marks); slot += 1)
    {
        TestArenaMark mark = scope.marks[slot];
        if (!mark.arena) continue;
        u64 retained = ends[slot] - mark.start;
        u64 peak = peaks[slot] - mark.start;
        // The top fixture rows describe primary-arena ownership. Scratch rows
        // remain separately addressable by arena_slot in the full report.
        if (!slot && !scope.module && retained > arguments->memory_top_retained_bytes)
        {
            arguments->memory_top_retained_fixture = scope.name;
            arguments->memory_top_retained_bytes = retained;
        }
        if (!slot && !scope.module && peak > arguments->memory_top_peak_bytes)
        {
            arguments->memory_top_peak_fixture = scope.name;
            arguments->memory_top_peak_bytes = peak;
        }
        if (arguments->memory_report)
        {
            // Parallel show copies bytes to its separate output arena. No
            // published record points into reclaimed fixture storage.
            arguments->show(arguments,
                S8("TEST_ARENA_V1 kind={S8} module={S8} fixture={S8} index={u64} arena_slot={u32} start={u64} end={u64} retained_bytes={u64} high_water={u64} peak_bytes={u64} after={u64} live_bytes={u64} rewind={u32}\n"),
                scope.module ? S8("module") : S8("fixture"), arguments->memory_module, scope.name, scope.index, slot,
                mark.start, ends[slot], retained, peaks[slot], peak, mark.arena->position,
                mark.arena->position - mark.start, (u32)(!slot && rewind));
        }
    }
    if (arguments->memory_report && scope.module)
    {
        arguments->show(arguments,
            S8("TEST_ARENA_TOP_V1 module={S8} fixtures={u64} retained_fixture={S8} retained_bytes={u64} peak_fixture={S8} peak_bytes={u64}\n"),
            arguments->memory_module, arguments->memory_fixture_index, arguments->memory_top_retained_fixture,
            arguments->memory_top_retained_bytes, arguments->memory_top_peak_fixture, arguments->memory_top_peak_bytes);
    }
    if (scope.timing)
    {
        // One row per existing scope, through the same deterministic output
        // owner as arena records. Completion does not imply a passing fixture.
        arguments->show(arguments,
            S8("TEST_FIXTURE_TIMING_V1 kind={S8} module={S8} fixture={S8} index={u64} duration_ns={u64} measurement=inclusive-wall status=completed\n"),
            scope.module ? S8("module") : S8("fixture"), arguments->memory_module, scope.name, scope.index,
            timestamp_ns_between(scope.start, end));
    }
    // Exclude reporting's scratch allocations from enclosing observations.
    // dirty_position still records every discarded byte for correct zeroing.
    for (u32 slot = 0; slot < BUSTER_ARRAY_LENGTH(scope.marks); slot += 1)
    {
        TestArenaMark mark = scope.marks[slot];
        if (mark.arena)
        {
            mark.arena->high_water = BUSTER_MAX(mark.previous_high_water, peaks[slot]);
        }
    }
    // Last, so the watched position stays on this scope until it has closed.
    TestWatchSlot* watch = arguments->watch_slot;
    if (watch)
    {
        test_watch_publish(watch, arguments->memory_module, scope.watch_parent, scope.watch_parent_index, scope.watch_parent_module, scope.name);
    }
}

typedef struct TestTimingRecord TestTimingRecord;
struct TestTimingRecord
{
    u64 index;
    String8 module;
    u64 duration_ns;
    UnitTestResult result;
};

BUSTER_GLOBAL_LOCAL UnitTestResult test_timing_self_test_pass(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    return (UnitTestResult){2, 2};
}

BUSTER_GLOBAL_LOCAL UnitTestResult test_timing_self_test_fail(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    return (UnitTestResult){1, 2};
}

BUSTER_GLOBAL_LOCAL TestTimingRecord test_timing_run_descriptor(UnitTestArguments* arguments, TestDescriptor descriptor, u64 index)
{
    TimeDataType start = timestamp_take();
    UnitTestResult result = descriptor.function(arguments);
    TimeDataType end = timestamp_take();
    u64 duration_ns = timestamp_ns_between(start, end);

    return (TestTimingRecord){
        .index = index,
        .module = descriptor.name,
        .duration_ns = duration_ns,
        .result = result,
    };
}

BUSTER_GLOBAL_LOCAL bool test_timing_self_test(UnitTestArguments* arguments)
{
    TestDescriptor descriptors[] = {
        {S8("self_test_first"), &test_timing_self_test_pass},
        {S8("self_test_failed"), &test_timing_self_test_fail},
        {S8("self_test_last"), &test_timing_self_test_pass},
    };
    TestTimingRecord records[BUSTER_ARRAY_LENGTH(descriptors)] = {0};
    u64 arena_position = arguments->arena->position;
    u64 record_count = 0;
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(descriptors); i += 1)
    {
        records[record_count] = test_timing_run_descriptor(arguments, descriptors[i], i);
        record_count += 1;
    }
    arena_set_position(arguments->arena, arena_position);

    bool result = record_count == BUSTER_ARRAY_LENGTH(descriptors);
    result = result && string_equal(records[0].module, S8("self_test_first"));
    result = result && string_equal(records[1].module, S8("self_test_failed"));
    result = result && string_equal(records[2].module, S8("self_test_last"));
    result = result && records[0].index == 0 && records[1].index == 1 && records[2].index == 2;
    result = result && records[0].result.succeeded_test_count == 2 && records[0].result.test_count == 2;
    result = result && records[1].result.succeeded_test_count == 1 && records[1].result.test_count == 2;
    result = result && records[2].result.succeeded_test_count == 2 && records[2].result.test_count == 2;
    result = result && records[1].result.test_count - records[1].result.succeeded_test_count == 1;
    return result;
}

BUSTER_GLOBAL_LOCAL void test_timing_report(UnitTestArguments* arguments, TestTimingRecord record)
{
    u64 failed_test_count = record.result.test_count - record.result.succeeded_test_count;
    String8 status = unit_test_succeeded(record.result) ? S8("pass") : S8("fail");
    arguments->show(arguments,
                    S8("TEST_MODULE_TIMING index={u64} module={S8} duration_ns={u64} passed={u64} failed={u64} assertions={u64} status={S8}\n"),
                    record.index, record.module, record.duration_ns, record.result.succeeded_test_count, failed_test_count,
                    record.result.test_count, status);
}

// A real `ide test` child whose only fixture spins, run under a one-second
// BUSTER_TEST_FIXTURE_TIMEOUT_SECONDS, must end itself with the timeout status
// and a record naming that fixture after the fixture's start record. The
// parent's own wait deadline catches a watchdog that never fires.
BUSTER_GLOBAL_LOCAL UnitTestResult test_fixture_watchdog_child(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if BUSTER_TEST_WATCHDOG_SUPPORTED
    enum { TEST_WATCHDOG_CHILD_TIMEOUT_US = 30000000, TEST_WATCHDOG_CHILD_DEADLINE_US = 1000000 };
    Arena* arena = arguments->arena;
    u64 position = arena->position;
    String8 child_arguments[] = {program_state->input.arguments.pointer[0], S8("test"), S8("--ci=1")};
    SliceString8 inherited_keys = program_state->input.environment_keys;
    SliceString8 inherited_values = program_state->input.environment_values;
    String8* keys = arena_allocate(arena, String8, inherited_keys.length + 2);
    String8* values = arena_allocate(arena, String8, inherited_keys.length + 2);
    keys[0] = S8("BUSTER_TEST_WATCHDOG_CHILD_MODE");
    values[0] = S8("hang");
    keys[1] = S8("BUSTER_TEST_FIXTURE_TIMEOUT_SECONDS");
    values[1] = S8("1");
    u64 count = 2;
    for (u64 inherited = 0; inherited < inherited_keys.length; inherited += 1)
    {
        if (!string_equal(inherited_keys.pointer[inherited], keys[0]) && !string_equal(inherited_keys.pointer[inherited], keys[1]))
        {
            keys[count] = inherited_keys.pointer[inherited];
            values[count] = inherited_values.pointer[inherited];
            count += 1;
        }
    }
    TimeDataType start = timestamp_take();
    ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments),
        (SliceString8){keys, count}, (SliceString8){values, count},
        (ProcessSpawnOptions){.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR)});
    if (BUSTER_REQUIRE(arguments, spawn.handle != 0))
    {
        ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, TEST_WATCHDOG_CHILD_TIMEOUT_US);
        u64 duration_ns = timestamp_ns_between(start, timestamp_take());
        BUSTER_TEST(arguments, !wait.timed_out);
#if BUSTER_WINDOWS
        BUSTER_TEST(arguments, wait.platform_status == TEST_FIXTURE_TIMEOUT_EXIT_CODE);
#else
        // Darwin's wait macros require an addressable native int.
        int native_status = (int)wait.platform_status;
        BUSTER_TEST(arguments, WIFEXITED(native_status) && WEXITSTATUS(native_status) == TEST_FIXTURE_TIMEOUT_EXIT_CODE);
#endif
        String8 output = {(char8*)wait.streams[STANDARD_STREAM_OUTPUT].pointer, wait.streams[STANDARD_STREAM_OUTPUT].length};
        u64 started = string_first_sequence(output,
            S8("TEST_FIXTURE_START_V1 kind=fixture module=fixture_watchdog_self_test fixture=test_watchdog_hang_fixture index=0\n"));
        u64 expired = string_first_sequence(output,
            S8("TEST_FIXTURE_TIMEOUT_V1 kind=fixture module=fixture_watchdog_self_test fixture=test_watchdog_hang_fixture index=0 elapsed_ms="));
        BUSTER_TEST(arguments, started != BUSTER_STRING_NO_MATCH);
        BUSTER_TEST(arguments, expired != BUSTER_STRING_NO_MATCH && expired > started);
        BUSTER_TEST(arguments, string_first_sequence(output, S8(" deadline_seconds=1 last_completed=none\n")) != BUSTER_STRING_NO_MATCH);
        // The deadline bounds the child from below, the wait above.
        BUSTER_TEST(arguments, duration_ns >= (u64)TEST_WATCHDOG_CHILD_DEADLINE_US * 1000);
        BUSTER_TEST(arguments, wait.streams[STANDARD_STREAM_ERROR].length == 0);
        arguments->show(arguments,
            S8("TEST_FIXTURE_WATCHDOG_CHILD_V1 duration_ns={u64} timed_out={u32} platform_status={u64} stdout_bytes={u64} stderr_bytes={u64}\n"),
            duration_ns, (u32)wait.timed_out, (u64)wait.platform_status, wait.streams[STANDARD_STREAM_OUTPUT].length,
            wait.streams[STANDARD_STREAM_ERROR].length);
    }
    else
    {
        arguments->show(arguments, S8("TEST_FIXTURE_WATCHDOG_CHILD_V1 status=spawn-failed\n"));
    }
    arena_set_position(arena, position);
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}

// A private CI child enters one selected parallel module and fails immediately
// after opening its buffered module scope. The live gang-start record must name
// the module, while the post-replay completion record must be absent. This is a
// harness check rather than a registered assertion, so aggregate totals stay
// unchanged.
BUSTER_GLOBAL_LOCAL bool test_parallel_crash_child_self_test(UnitTestArguments* arguments)
{
    bool result = true;
#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
    String8 crash_mode = os_get_environment_variable(S8("BUSTER_TEST_PARALLEL_CRASH_CHILD_MODE"));
    if (!crash_mode.length)
    {
        enum { TEST_PARALLEL_CRASH_CHILD_TIMEOUT_US = 30000000 };
        Arena* arena = arguments->arena;
        u64 position = arena->position;
        String8 child_arguments[] = {program_state->input.arguments.pointer[0], S8("test"), S8("--ci=1"),
                                     S8("--module=aarch64_complex_simd_tests")};
        SliceString8 inherited_keys = program_state->input.environment_keys;
        SliceString8 inherited_values = program_state->input.environment_values;
        String8* keys = arena_allocate(arena, String8, inherited_keys.length + 4);
        String8* values = arena_allocate(arena, String8, inherited_keys.length + 4);
        keys[0] = S8("BUSTER_TEST_PARALLEL_CRASH_CHILD_MODE");
        values[0] = S8("aarch64_complex_simd_tests");
        keys[1] = S8("BUSTER_TEST_FIXTURE_TIMEOUT_SECONDS");
        values[1] = S8("0");
        keys[2] = S8("BUSTER_TEST_JOBS");
        values[2] = S8("1");
        // This private probe selects one module explicitly; it must retain
        // that payload when the containing OS module belongs to a CI group.
        keys[3] = S8("BUSTER_TEST_MODULE_GROUP");
        values[3] = S8("");
        u64 count = 4;
        for (u64 inherited = 0; inherited < inherited_keys.length; inherited += 1)
        {
            bool overridden = string_equal(inherited_keys.pointer[inherited], keys[0]) ||
                              string_equal(inherited_keys.pointer[inherited], keys[1]) ||
                              string_equal(inherited_keys.pointer[inherited], keys[2]) ||
                              string_equal(inherited_keys.pointer[inherited], keys[3]);
            if (!overridden)
            {
                keys[count] = inherited_keys.pointer[inherited];
                values[count] = inherited_values.pointer[inherited];
                count += 1;
            }
        }
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments),
            (SliceString8){keys, count}, (SliceString8){values, count},
            (ProcessSpawnOptions){.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR)});
        result = spawn.handle != 0;
        if (result)
        {
            ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, TEST_PARALLEL_CRASH_CHILD_TIMEOUT_US);
            result = !wait.timed_out && wait.result == PROCESS_RESULT_FAILED;
#if BUSTER_WINDOWS
            result = result && wait.platform_status == 1;
#else
            // Darwin's wait macros require an addressable native int.
            int native_status = (int)wait.platform_status;
            result = result && WIFEXITED(native_status) && WEXITSTATUS(native_status) == 1;
#endif
            String8 output = {(char8*)wait.streams[STANDARD_STREAM_OUTPUT].pointer, wait.streams[STANDARD_STREAM_OUTPUT].length};
            String8 error = {(char8*)wait.streams[STANDARD_STREAM_ERROR].pointer, wait.streams[STANDARD_STREAM_ERROR].length};
            u64 started = string_first_sequence(output,
                S8("TEST_PARALLEL_GANG_V1 status=started module_count=1 modules=aarch64_complex_simd_tests\n"));
            u64 completed = string_first_sequence(output,
                S8("TEST_PARALLEL_GANG_V1 status=completed module_count=1 modules=aarch64_complex_simd_tests\n"));
            result = result && started != BUSTER_STRING_NO_MATCH && completed == BUSTER_STRING_NO_MATCH &&
                     string_first_sequence(error, S8("parallel lane crash self-test at ")) != BUSTER_STRING_NO_MATCH;
        }
        arena_set_position(arena, position);
    }
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}

// Keep the original OS tests, including every closed/full-stream control. These
// additional live probes make late child dispatch observable even on fast hosts:
// verbose nested modules write stdout, whereas the fatal payload writes only stderr.
BUSTER_GLOBAL_LOCAL UnitTestResult test_os_with_fatal_child_isolation(UnitTestArguments* arguments)
{
    UnitTestResult result = os_tests(arguments);
    UnitTestResult watchdog = test_fixture_watchdog_child(arguments);
    result.succeeded_test_count += watchdog.succeeded_test_count;
    result.test_count += watchdog.test_count;
    BUSTER_VALIDATE(test_parallel_crash_child_self_test(arguments));
#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
    enum { TEST_FATAL_CHILD_REPETITIONS = 3, TEST_FATAL_CHILD_TIMEOUT_US = 30000000 };
    Arena* arena = arguments->arena;
    u64 position = arena->position;
    String8 modes[] = {S8("raw-live"), S8("formatted-live")};
    String8 child_arguments[] = {program_state->input.arguments.pointer[0], S8("test"), S8("--verbose=1"), S8("--ci=1")};
    SliceString8 inherited_keys = program_state->input.environment_keys;
    SliceString8 inherited_values = program_state->input.environment_values;
    String8* keys = arena_allocate(arena, String8, inherited_keys.length + 2);
    String8* values = arena_allocate(arena, String8, inherited_keys.length + 2);
    keys[0] = S8("BUSTER_OS_FATAL_OUTPUT_MODE");
    keys[1] = S8("BUSTER_TEST_JOBS");
    values[1] = S8("1");
    u64 count = 2;
    for (u64 inherited = 0; inherited < inherited_keys.length; inherited += 1)
    {
        if (!string_equal(inherited_keys.pointer[inherited], keys[0]) && !string_equal(inherited_keys.pointer[inherited], keys[1]))
        {
            keys[count] = inherited_keys.pointer[inherited];
            values[count] = inherited_values.pointer[inherited];
            count += 1;
        }
    }
    u64 capture_position = arena->position;
    for (u32 repetition = 0; repetition < TEST_FATAL_CHILD_REPETITIONS; repetition += 1)
    {
        for (u32 mode = 0; mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
        {
            values[0] = modes[mode];
            TimeDataType start = timestamp_take();
            ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments),
                (SliceString8){keys, count}, (SliceString8){values, count},
                (ProcessSpawnOptions){.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR)});
            if (BUSTER_REQUIRE(arguments, spawn.handle != 0))
            {
                ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, TEST_FATAL_CHILD_TIMEOUT_US);
                TimeDataType end = timestamp_take();
                BUSTER_TEST(arguments, !wait.timed_out);
                BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_FAILED);
#if BUSTER_WINDOWS
                BUSTER_TEST(arguments, wait.platform_status == 1);
#else
                // Darwin's wait macros require an addressable native int.
                int native_status = (int)wait.platform_status;
                BUSTER_TEST(arguments, WIFEXITED(native_status) && WEXITSTATUS(native_status) == 1);
#endif
                String8 error = {(char8*)wait.streams[STANDARD_STREAM_ERROR].pointer, wait.streams[STANDARD_STREAM_ERROR].length};
                BUSTER_STRING_TEST(arguments, error, S8("fatal-output-37 at os-fail-regression.c:19 in child\n"));
                BUSTER_TEST(arguments, wait.streams[STANDARD_STREAM_OUTPUT].length == 0);
                arguments->show(arguments,
                    S8("OS_FATAL_OUTPUT_CHILD_V1 mode={S8} repetition={u32} duration_ns={u64} timed_out={u32} platform_status={u64} stdout_bytes={u64} stderr_bytes={u64}\n"),
                    modes[mode], repetition, timestamp_ns_between(start, end), (u32)wait.timed_out, (u64)wait.platform_status,
                    wait.streams[STANDARD_STREAM_OUTPUT].length, wait.streams[STANDARD_STREAM_ERROR].length);
            }
            else
            {
                arguments->show(arguments, S8("OS_FATAL_OUTPUT_CHILD_V1 mode={S8} repetition={u32} status=spawn-failed\n"), modes[mode], repetition);
            }
            arena_set_position(arena, capture_position);
        }
    }
    arena_set_position(arena, position);
#endif
    return result;
}

typedef enum TestId
{
    TEST_ID_BYTE_WRITER,
    TEST_ID_ARENA,
    TEST_ID_INTEGER,
    TEST_ID_SANITIZER,
    TEST_ID_HASH,
    TEST_ID_SIMD,
    TEST_ID_STRING,
    TEST_ID_OS,
    TEST_ID_FILE,
    TEST_ID_TARGET,
    TEST_ID_TRUETYPE,
    TEST_ID_IMAGE,
    TEST_ID_C_FRONTEND,
    TEST_ID_C_ONCE,
    TEST_ID_C_TYPE_LAYOUT,
    TEST_ID_C_MACRO_CONDITIONAL,
    TEST_ID_C_RECORD_LAYOUT,
    TEST_ID_METAMORPHIC,
    TEST_ID_AARCH64_ENCODING,
    TEST_ID_AARCH64_EXACT_BRIDGE,
    TEST_ID_AARCH64_BASE_ASSEMBLY,
    TEST_ID_AARCH64_CONTROL_SEMANTICS,
    TEST_ID_AARCH64_SYSTEM_REGISTERS,
    TEST_ID_AARCH64_SEMANTICS,
    TEST_ID_AARCH64_SYSTEM_SEMANTICS,
    TEST_ID_AARCH64_SYNTAX,
    TEST_ID_AARCH64_SEMANTIC_VM,
    TEST_ID_AARCH64_DIRECT_SIMD,
    TEST_ID_AARCH64_COMPLEX_SIMD,
    TEST_ID_AARCH64_MEMORY_SEMANTICS,
    TEST_ID_AARCH64_ALIAS_PROJECTION,
    TEST_ID_ASSEMBLY,
    TEST_ID_X86_64_FORWARDING,
    TEST_ID_X86_64_METADATA,
    TEST_ID_X86_64_TLS,
    TEST_ID_EXECUTABLE_PADDING,
    TEST_ID_X86_64_GOT,
#if BUSTER_CPU_ARCH_X86_64
    TEST_ID_X86_64_COMPLETION_CENSUS,
#endif
    TEST_ID_IR,
    TEST_ID_IR_ORACLE,
    TEST_ID_IR_ORACLE_NATIVE,
    TEST_ID_VECTOR_CONTRACT,
    TEST_ID_LLVM_BITCODE,
    TEST_ID_MACHINE_SELECTION,
    TEST_ID_MACHINE,
    TEST_ID_CODEGEN,
    TEST_ID_INVESTIGATION,
    TEST_ID_AARCH64_STRIDE,
    TEST_ID_DEBUG_MODEL,
    TEST_ID_DWARF,
    TEST_ID_CODEVIEW,
    TEST_ID_PDB,
    TEST_ID_OBJECT,
    TEST_ID_JIT,
    TEST_ID_LINK,
    TEST_ID_GPU_PIPELINE,
    TEST_ID_SPIRV_COMPUTE,
    TEST_ID_COMPILER_DIAGNOSTIC,
    TEST_ID_COMPILER_DRIVER,
    TEST_ID_COMPILER_DRIVER_OBJECT_PATH,
#if BUSTER_CPU_ARCH_X86_64
    TEST_ID_X86_64,
#endif
    TEST_ID_COUNT,
} TestId;

BUSTER_GLOBAL_LOCAL TestDescriptor test_descriptors[TEST_ID_COUNT] = {
    [TEST_ID_BYTE_WRITER] = {S8_INITIALIZER("byte_writer_tests"), &byte_writer_tests},
    [TEST_ID_ARENA] = {S8_INITIALIZER("arena_tests"), &arena_tests},
    [TEST_ID_INTEGER] = {S8_INITIALIZER("integer_tests"), &integer_tests},
    [TEST_ID_SANITIZER] = {S8_INITIALIZER("sanitizer_tests"), &sanitizer_tests},
    [TEST_ID_HASH] = {S8_INITIALIZER("hash_tests"), &hash_tests},
    [TEST_ID_SIMD] = {S8_INITIALIZER("simd_tests"), &simd_tests},
    [TEST_ID_STRING] = {S8_INITIALIZER("string_tests"), &string_tests},
    [TEST_ID_OS] = {S8_INITIALIZER("os_tests"), &test_os_with_fatal_child_isolation, true},
    [TEST_ID_FILE] = {S8_INITIALIZER("file_tests"), &file_tests, !BUSTER_ANDROID && !BUSTER_IOS},
    [TEST_ID_TARGET] = {S8_INITIALIZER("target_tests"), &target_tests},
    [TEST_ID_TRUETYPE] = {S8_INITIALIZER("truetype_tests"), &truetype_tests},
    [TEST_ID_IMAGE] = {S8_INITIALIZER("image_tests"), &image_tests},
    [TEST_ID_METAMORPHIC] = {S8_INITIALIZER("metamorphic_tests"), &metamorphic_tests, !BUSTER_ANDROID && !BUSTER_IOS},
    [TEST_ID_C_FRONTEND] = {S8_INITIALIZER("c_frontend_tests"), &c_frontend_tests, true},
    [TEST_ID_C_ONCE] = {S8_INITIALIZER("c_once_tests"), &c_once_tests, true},
    [TEST_ID_C_TYPE_LAYOUT] = {S8_INITIALIZER("c_type_layout_tests"), &c_type_layout_tests},
    [TEST_ID_C_MACRO_CONDITIONAL] = {S8_INITIALIZER("c_macro_conditional_tests"), &c_macro_conditional_tests, true},
    [TEST_ID_C_RECORD_LAYOUT] = {S8_INITIALIZER("record_layout_tests"), &record_layout_tests, true},
    [TEST_ID_AARCH64_ENCODING] = {S8_INITIALIZER("aarch64_encoding_tests"), &aarch64_encoding_tests},
    [TEST_ID_AARCH64_EXACT_BRIDGE] = {S8_INITIALIZER("aarch64_exact_bridge_tests"), &aarch64_exact_bridge_tests},
    [TEST_ID_AARCH64_BASE_ASSEMBLY] = {S8_INITIALIZER("aarch64_base_assembly_tests"), &aarch64_base_assembly_tests},
    [TEST_ID_AARCH64_CONTROL_SEMANTICS] = {S8_INITIALIZER("aarch64_control_semantics_tests"), &aarch64_control_semantics_tests},
    [TEST_ID_AARCH64_SYSTEM_REGISTERS] = {S8_INITIALIZER("aarch64_system_registers_tests"), &aarch64_system_registers_tests},
    [TEST_ID_AARCH64_SEMANTICS] = {S8_INITIALIZER("aarch64_semantics_tests"), &aarch64_semantics_tests},
    [TEST_ID_AARCH64_SYSTEM_SEMANTICS] = {S8_INITIALIZER("aarch64_system_semantics_tests"), &aarch64_system_semantics_tests},
    [TEST_ID_AARCH64_SYNTAX] = {S8_INITIALIZER("aarch64_syntax_tests"), &aarch64_syntax_tests},
    [TEST_ID_AARCH64_SEMANTIC_VM] = {S8_INITIALIZER("aarch64_semantic_vm_tests"), &aarch64_semantic_vm_tests},
    [TEST_ID_AARCH64_DIRECT_SIMD] = {S8_INITIALIZER("aarch64_direct_simd_tests"), &aarch64_direct_simd_tests, false, TEST_DESCRIPTOR_PARALLEL_AARCH64_DIRECT_SIMD},
    [TEST_ID_AARCH64_COMPLEX_SIMD] = {S8_INITIALIZER("aarch64_complex_simd_tests"), &aarch64_complex_simd_tests, false, TEST_DESCRIPTOR_PARALLEL_AARCH64_COMPLEX_SIMD},
    [TEST_ID_AARCH64_MEMORY_SEMANTICS] = {S8_INITIALIZER("aarch64_memory_semantics_tests"), &aarch64_memory_semantics_tests, false, TEST_DESCRIPTOR_PARALLEL_AARCH64_MEMORY_SEMANTICS},
    [TEST_ID_AARCH64_ALIAS_PROJECTION] = {S8_INITIALIZER("aarch64_alias_projection_tests"), &aarch64_alias_projection_tests},
    [TEST_ID_ASSEMBLY] = {S8_INITIALIZER("assembly_tests"), &assembly_tests},
    [TEST_ID_X86_64_FORWARDING] = {S8_INITIALIZER("x86_64_forwarding_tests"), &x86_64_forwarding_tests},
    [TEST_ID_X86_64_METADATA] = {S8_INITIALIZER("x86_64_metadata_tests"), &x86_64_metadata_tests},
    [TEST_ID_X86_64_TLS] = {S8_INITIALIZER("x86_64_tls_tests"), &x86_64_tls_tests},
    [TEST_ID_EXECUTABLE_PADDING] = {S8_INITIALIZER("executable_padding_tests"), &executable_padding_tests},
    [TEST_ID_X86_64_GOT] = {S8_INITIALIZER("x86_64_got_tests"), &x86_64_got_tests},
#if BUSTER_CPU_ARCH_X86_64
    [TEST_ID_X86_64_COMPLETION_CENSUS] = {S8_INITIALIZER("x86_64_completion_census_tests"), &x86_64_completion_census_tests, false,
                                          TEST_DESCRIPTOR_PARALLEL_NONE, true},
#endif
    [TEST_ID_IR] = {S8_INITIALIZER("ir_tests"), &ir_tests},
    [TEST_ID_IR_ORACLE] = {S8_INITIALIZER("ir_oracle_tests"), &ir_oracle_tests, true},
    // This re-exec payload uses only in-memory mappings. Its timeout child is
    // killed before final cleanup, so leave unused temporary roots unallocated.
    [TEST_ID_IR_ORACLE_NATIVE] = {S8_INITIALIZER("ir_oracle_native_tests"), &ir_oracle_native_tests, false},
    [TEST_ID_VECTOR_CONTRACT] = {S8_INITIALIZER("vector_contract_tests"), &vector_contract_tests},
    [TEST_ID_LLVM_BITCODE] = {S8_INITIALIZER("llvm_bitcode_tests"), &llvm_bitcode_tests},
    [TEST_ID_MACHINE_SELECTION] = {S8_INITIALIZER("machine_selection_tests"), &machine_selection_tests},
    [TEST_ID_MACHINE] = {S8_INITIALIZER("machine_tests"), &machine_tests},
    [TEST_ID_CODEGEN] = {S8_INITIALIZER("codegen_tests"), &codegen_tests_with_block_start_oracle},
    [TEST_ID_INVESTIGATION] = {S8_INITIALIZER("investigation_tests"), &investigation_test, true, TEST_DESCRIPTOR_PARALLEL_NONE},
    [TEST_ID_AARCH64_STRIDE] = {S8_INITIALIZER("aarch64_stride_tests"), &aarch64_stride_tests},
    [TEST_ID_DEBUG_MODEL] = {S8_INITIALIZER("debug_model_tests"), &debug_model_tests},
    [TEST_ID_DWARF] = {S8_INITIALIZER("dwarf_tests"), &dwarf_tests},
    [TEST_ID_CODEVIEW] = {S8_INITIALIZER("codeview_tests"), &codeview_tests},
    [TEST_ID_PDB] = {S8_INITIALIZER("pdb_tests"), &pdb_tests, !BUSTER_IOS},
    [TEST_ID_OBJECT] = {S8_INITIALIZER("object_tests"), &object_tests},
    [TEST_ID_JIT] = {S8_INITIALIZER("jit_tests"), &jit_tests},
    [TEST_ID_LINK] = {S8_INITIALIZER("link_tests"), &link_tests, !BUSTER_ANDROID && !BUSTER_IOS},
    [TEST_ID_GPU_PIPELINE] = {S8_INITIALIZER("gpu_pipeline_tests"), &gpu_pipeline_tests},
    [TEST_ID_SPIRV_COMPUTE] = {S8_INITIALIZER("spirv_compute_tests"), &spirv_compute_tests},
    [TEST_ID_COMPILER_DIAGNOSTIC] = {S8_INITIALIZER("compiler_diagnostic_tests"), &compiler_diagnostic_tests, true},
    [TEST_ID_COMPILER_DRIVER] = {S8_INITIALIZER("compiler_driver_tests"), &compiler_driver_tests, true},
    [TEST_ID_COMPILER_DRIVER_OBJECT_PATH] = {S8_INITIALIZER("compiler_driver_object_path_tests"), &compiler_driver_object_path_tests, !BUSTER_ANDROID && !BUSTER_IOS},
#if BUSTER_CPU_ARCH_X86_64
    [TEST_ID_X86_64] = {S8_INITIALIZER("x86_64_tests"), &x86_64_tests},
#endif
};

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(test_descriptors) == TEST_ID_COUNT);

// Slots are indexed by descriptor, so serial modules and parallel lanes share
// one table. The runner publishes only while slots is set.
typedef struct TestWatchdog TestWatchdog;
struct TestWatchdog
{
    TestWatchSlot* slots;
    u64 slot_count;
    u64 deadline_seconds;
    OsThreadHandle* thread;
    AtomicU64 stop;
};

BUSTER_GLOBAL_LOCAL TestWatchdog test_watchdog;

#if BUSTER_TEST_WATCHDOG_SUPPORTED
BUSTER_GLOBAL_LOCAL TestWatchSlot test_watch_slots[TEST_ID_COUNT];

BUSTER_GLOBAL_LOCAL void test_watchdog_sleep(u32 milliseconds)
{
#if defined(_WIN32)
    Sleep(milliseconds);
#else
    (void)poll(0, 0, (int)milliseconds);
#endif
}

// The timed-out thread is still running and may hold any lock, so neither
// exit handlers nor exit-time sanitizer reports may run behind it.
BUSTER_GLOBAL_LOCAL void test_watchdog_terminate(u32 code)
{
#if defined(_WIN32)
    (void)TerminateProcess(GetCurrentProcess(), code);
    os_exit(code);
#else
    _exit((int)code);
#endif
}

// Polls instead of waiting on an event: stopping costs at most one interval.
// Every expired slot is reported before the process ends.
BUSTER_GLOBAL_LOCAL ThreadReturnType test_watchdog_thread(void* argument)
{
    TestWatchdog* watchdog = (TestWatchdog*)argument;
    u64 deadline_microseconds = watchdog->deadline_seconds * 1000000;
    bool expired = false;
    while (!expired && !atomic_u64_add(&watchdog->stop, 0))
    {
        test_watchdog_sleep(TEST_WATCHDOG_POLL_MILLISECONDS);
        for (u64 index = 0; index < watchdog->slot_count; index += 1)
        {
            TestWatchSnapshot snapshot = test_watch_snapshot(&watchdog->slots[index]);
            u64 now = os_now_microseconds();
            if (test_watch_expired(snapshot, now, deadline_microseconds))
            {
                TemporalArena scratch = scratch_begin(0, 0);
                String8 report = test_watch_report(scratch.arena, snapshot, now, watchdog->deadline_seconds);
                (void)os_file_write_attempt(os_get_stdout(), BUSTER_SLICE_TO_BYTE_SLICE(report));
                scratch_end(scratch);
                expired = true;
            }
        }
    }
    if (expired)
    {
        test_watchdog_terminate(TEST_FIXTURE_TIMEOUT_EXIT_CODE);
    }
}
#endif

// Arms the watchdog over slot_count descriptor slots. False asks the caller
// to record one harness failure: an invalid deadline (the default still
// applies) or a thread that could not start. Verbose and CI runs always get a
// status record; failures get one in every mode.
BUSTER_GLOBAL_LOCAL bool test_watchdog_start(UnitTestArguments* arguments, u64 slot_count)
{
    String8 text = os_get_environment_variable(S8("BUSTER_TEST_FIXTURE_TIMEOUT_SECONDS"));
    TestFixtureDeadline deadline = test_fixture_deadline_parse(text);
    bool result = deadline.valid;
    if (!deadline.valid)
    {
        arguments->show(arguments, S8("TEST_FIXTURE_WATCHDOG_V1 status=invalid-deadline value={S8} deadline_seconds={u64}\n"), text, deadline.seconds);
    }
    String8 status = S8("unsupported");
#if BUSTER_TEST_WATCHDOG_SUPPORTED
    BUSTER_CHECK(slot_count <= BUSTER_ARRAY_LENGTH(test_watch_slots) && !test_watchdog.thread);
    status = S8("disabled");
    if (deadline.seconds && is_debugger_present())
    {
        // A breakpoint would look exactly like a hang.
        status = S8("debugger");
    }
    else if (deadline.seconds)
    {
        memset(test_watch_slots, 0, sizeof(test_watch_slots));
        test_watchdog.slots = test_watch_slots;
        test_watchdog.slot_count = slot_count;
        test_watchdog.deadline_seconds = deadline.seconds;
        test_watchdog.stop = 0;
        // Untracked: it reads only its slots, and modules that assert the
        // process is serial must still see it that way.
        test_watchdog.thread = os_thread_create((ThreadCreateOptions){.callback = &test_watchdog_thread, .argument = &test_watchdog, .untracked = true});
        status = test_watchdog.thread ? S8("armed") : S8("thread-failed");
        result = result && test_watchdog.thread != 0;
        test_watchdog.slots = test_watchdog.thread ? test_watchdog.slots : 0;
    }
#else
    BUSTER_UNUSED(slot_count);
#endif
    if (arguments->memory_report || !result)
    {
        arguments->show(arguments, S8("TEST_FIXTURE_WATCHDOG_V1 status={S8} deadline_seconds={u64}\n"), status, deadline.seconds);
    }
    return result;
}

typedef struct TestModuleSelection TestModuleSelection;
struct TestModuleSelection
{
    u64 invalid_count;
    bool selected[TEST_ID_COUNT];
};

// Each comma-separated name must equal a descriptor name registered for this
// target. An unknown or empty name is invalid rather than ignored, so a typo
// can never turn into a vacuous pass.
BUSTER_GLOBAL_LOCAL TestModuleSelection test_module_selection_resolve(String8 list, bool report)
{
    TestModuleSelection result = {0};
    u64 start = 0;
    for (u64 index = 0; index <= list.length; index += 1)
    {
        if (index == list.length || list.pointer[index] == ',')
        {
            String8 name = string_slice(list, start, index);
            u64 id = 0;
            while (id < TEST_ID_COUNT && !string_equal(test_descriptors[id].name, name))
            {
                id += 1;
            }
            if (id < TEST_ID_COUNT)
            {
                result.selected[id] = true;
            }
            else
            {
                result.invalid_count += 1;
                if (report)
                {
                    string_print(S8("test: unknown module: '{S8}'\n"), name);
                }
            }
            start = index + 1;
        }
    }
    return result;
}

bool buster_test_module_selection_check(String8 selection)
{
    bool result = !test_module_selection_resolve(selection, true).invalid_count;
    if (!result)
    {
        string_print(S8("test: modules registered for this target:\n"));
        for (u64 id = 0; id < TEST_ID_COUNT; id += 1)
        {
            string_print(S8("  {S8}\n"), test_descriptors[id].name);
        }
    }
    return result;
}

// Match the native parent independently. Windows x64's checked frontend
// dominates the old rest group; ARM retains the better-balanced driver split.
BUSTER_GLOBAL_LOCAL String8 test_module_group_primary_name(void)
{
    String8 result = BUSTER_WINDOWS && BUSTER_CPU_ARCH_X86_64 ? S8("c_frontend_tests") : S8("compiler_driver_tests");
    return result;
}

typedef enum TestModuleGroup
{
    TEST_MODULE_GROUP_NONE,
    TEST_MODULE_GROUP_PRIMARY,
    TEST_MODULE_GROUP_REST,
    TEST_MODULE_GROUP_INVENTORY,
    TEST_MODULE_GROUP_INVALID,
} TestModuleGroup;

BUSTER_GLOBAL_LOCAL TestModuleGroup test_module_group_resolve(String8 name)
{
    TestModuleGroup result = TEST_MODULE_GROUP_INVALID;
    if (!name.length) result = TEST_MODULE_GROUP_NONE;
    else if (string_equal(name, S8("primary"))) result = TEST_MODULE_GROUP_PRIMARY;
    else if (string_equal(name, S8("rest"))) result = TEST_MODULE_GROUP_REST;
    else if (string_equal(name, S8("inventory"))) result = TEST_MODULE_GROUP_INVENTORY;
    return result;
}

BUSTER_GLOBAL_LOCAL TestModuleGroup test_module_group_owner(TestDescriptor descriptor)
{
    TestModuleGroup result = string_equal(descriptor.name, test_module_group_primary_name()) ? TEST_MODULE_GROUP_PRIMARY : TEST_MODULE_GROUP_REST;
    return result;
}

// Registered names are protocol tokens. Exactly one primary owner, at least one
// other module, and unique nonempty names prevent a changed registration table
// from silently turning the two-process union into partial coverage.
BUSTER_GLOBAL_LOCAL bool test_module_group_table_valid(TestDescriptor* descriptors, u64 count)
{
    bool result = count > 1;
    u64 primary_count = 0;
    for (u64 index = 0; index < count; index += 1)
    {
        String8 name = descriptors[index].name;
        result = result && name.length && descriptors[index].function != 0;
        for (u64 character = 0; character < name.length; character += 1)
        {
            u8 value = name.pointer[character];
            result = result && ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                                (value >= '0' && value <= '9') || value == '_');
        }
        for (u64 previous = 0; previous < index; previous += 1)
        {
            result = result && !string_equal(descriptors[previous].name, name);
        }
        bool primary = test_module_group_owner(descriptors[index]) == TEST_MODULE_GROUP_PRIMARY;
        result = result && (!primary || !descriptors[index].table_audit);
        primary_count += primary;
    }
    result = result && primary_count == 1;
    return result;
}

// Only the working copy changes. Audit flags, function ownership and all
// original indices remain intact, including disabled audit inventory rows.
BUSTER_GLOBAL_LOCAL u64 test_module_group_apply(TestDescriptor* descriptors, u64 count, TestModuleGroup group, bool valid)
{
    u64 result = 0;
    for (u64 index = 0; index < count; index += 1)
    {
        descriptors[index].deselected = descriptors[index].deselected || !valid || test_module_group_owner(descriptors[index]) != group;
        result += buster_test_descriptor_runs(descriptors[index]);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool test_module_group_union_valid(TestDescriptor* original, TestDescriptor* primary, TestDescriptor* rest, u64 count)
{
    bool result = test_module_group_table_valid(original, count);
    for (u64 index = 0; index < count; index += 1)
    {
        bool enabled = buster_test_descriptor_runs(original[index]);
        bool primary_selected = buster_test_descriptor_runs(primary[index]);
        bool rest_selected = buster_test_descriptor_runs(rest[index]);
        TestModuleGroup owner = test_module_group_owner(original[index]);
        result = result && primary_selected == (enabled && owner == TEST_MODULE_GROUP_PRIMARY) &&
                 rest_selected == (enabled && owner == TEST_MODULE_GROUP_REST);
        result = result && string_equal(original[index].name, primary[index].name) && string_equal(original[index].name, rest[index].name) &&
                 original[index].function == primary[index].function && original[index].function == rest[index].function &&
                 original[index].table_audit == primary[index].table_audit && original[index].table_audit == rest[index].table_audit &&
                 original[index].requires_temporary_root == primary[index].requires_temporary_root &&
                 original[index].requires_temporary_root == rest[index].requires_temporary_root &&
                 original[index].parallel_kind == primary[index].parallel_kind && original[index].parallel_kind == rest[index].parallel_kind;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void test_native_host_profile(UnitTestArguments* arguments)
{
    BUSTER_CT_CHECK(TARGET_CPU_FEATURE_WORD_COUNT == 4);
#if BUSTER_CPU_ARCH_X86_64
    String8 architecture = S8("x86_64");
    TargetCpuFeatures features = cpu_detect_features_x86_64();
    String8 feature_source = S8("cpuid-xcr0");
#else
    TargetCpuFeatures features = target_native.cpu_features;
    String8 feature_source = target_native.cpu_features_explicit ? S8("target-native") : S8("unavailable");
#if BUSTER_CPU_ARCH_AARCH64
    String8 architecture = S8("aarch64");
#else
    String8 architecture = cpu_arch_to_string_os(target_native.cpu_arch);
#endif
#endif
    // Storage bit n represents TargetCpuFeature ordinal n+1 (NONE has no bit).
    // Word n contains storage bits 64*n through 64*n+63. x86 probing includes
    // the OS/XCR0 usability gates;
    // other architectures report the explicit native target's feature oracle.
    arguments->show(arguments,
        S8("CI_UNIT_HOST_V1 architecture={S8} feature_source={S8} feature_word_count=4 word0={u64} word1={u64} word2={u64} word3={u64} simd_512_base={u32} simd_512={u32}\n"),
        architecture, feature_source, features.words[0], features.words[1], features.words[2], features.words[3],
        (u32)BUSTER_SIMD_512_BASE, (u32)BUSTER_SIMD_512);
}

BUSTER_GLOBAL_LOCAL void test_module_group_inventory(UnitTestArguments* arguments, TestDescriptor* selected)
{
    for (u64 index = 0; index < TEST_ID_COUNT; index += 1)
    {
        TestDescriptor descriptor = test_descriptors[index];
        String8 owner = test_module_group_owner(descriptor) == TEST_MODULE_GROUP_PRIMARY ? S8("primary") : S8("rest");
        arguments->show(arguments, S8("CI_UNIT_MODULE_V1 index={u64} module={S8} table_audit={u32} enabled={u32} selected={u32} group={S8}\n"),
                        index, descriptor.name, (u32)descriptor.table_audit, (u32)buster_test_descriptor_runs(descriptor),
                        (u32)buster_test_descriptor_runs(selected[index]), owner);
    }
}

BUSTER_GLOBAL_LOCAL bool test_watchdog_stop(void)
{
    bool result = true;
#if BUSTER_TEST_WATCHDOG_SUPPORTED
    if (test_watchdog.thread)
    {
        atomic_u64_increment(&test_watchdog.stop);
        result = os_thread_join(test_watchdog.thread);
        test_watchdog.thread = 0;
    }
#endif
    test_watchdog.slots = 0;
    test_watchdog.slot_count = 0;
    return result;
}

typedef struct TestParallelRecord TestParallelRecord;
struct TestParallelRecord
{
    TestTimingRecord timing;
    char8* output_pointer;
    u64 output_length;
    Arena* output_arena;
    bool completed;
    u8 reserved[7];
};

typedef struct TestParallelState TestParallelState;
struct TestParallelState
{
    TestDescriptor* descriptors;
    u64* eligible_indices;
    TestParallelRecord* records;
    TestWatchSlot* watch_slots;
    u64 eligible_count;
    bool memory_report;
    u8 reserved[7];
};

typedef struct TestParallelArguments TestParallelArguments;
struct TestParallelArguments
{
    UnitTestArguments base;
    Arena* output_arena;
};

BUSTER_GLOBAL_LOCAL void test_parallel_show(UnitTestArguments* arguments, String8 format, ...)
{
    TestParallelArguments* parallel_arguments = (TestParallelArguments*)arguments;
    if (!parallel_arguments->output_arena)
    {
        return;
    }
    va_list variable_arguments;
    va_start(variable_arguments, format);
    String8 text = string_format_va(parallel_arguments->output_arena, format, variable_arguments, STRING_FORMAT_VA_GP_SLOTS(3));
    va_end(variable_arguments);
    BUSTER_UNUSED(text);
}

// Lane-owned rows remain buffered. The serial runner publishes this complete
// deterministic set immediately before lane_run and only publishes its matching
// completion after every buffer has replayed in descriptor order.
BUSTER_GLOBAL_LOCAL void test_parallel_gang_report(UnitTestArguments* arguments, String8 status, TestDescriptor* descriptors,
                                                   u64* eligible_indices, u64 eligible_count)
{
    if (arguments->memory_report && eligible_count)
    {
        arguments->show(arguments, S8("TEST_PARALLEL_GANG_V1 status={S8} module_count={u64} modules="), status, eligible_count);
        for (u64 work_index = 0; work_index < eligible_count; work_index += 1)
        {
            String8 separator = work_index ? S8(",") : (String8){0};
            arguments->show(arguments, S8("{S8}{S8}"), separator, descriptors[eligible_indices[work_index]].name);
        }
        arguments->show(arguments, S8("\n"));
    }
}

// The boundary rows may bracket replay, but they must not alter a single byte
// of the buffered payload between them. Keep this outside registered totals.
BUSTER_GLOBAL_LOCAL bool test_parallel_gang_report_self_test(void)
{
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .flags = {.no_pool = true}});
    Arena* output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .flags = {.no_pool = true}});
    BUSTER_CHECK(arena != 0 && output != 0);
    TestParallelArguments arguments = {
        .base = {.arena = arena, .show = &test_parallel_show, .memory_report = true},
        .output_arena = output,
    };
    TestDescriptor descriptors[] = {
        {.name = S8("self_test_first")},
        {.name = S8("self_test_middle")},
        {.name = S8("self_test_last")},
    };
    u64 eligible_indices[] = {2, 0};
    test_parallel_gang_report(&arguments.base, S8("started"), descriptors, eligible_indices, BUSTER_ARRAY_LENGTH(eligible_indices));
    arguments.base.show(&arguments.base, S8("replayed-lane-output\n"));
    test_parallel_gang_report(&arguments.base, S8("completed"), descriptors, eligible_indices, BUSTER_ARRAY_LENGTH(eligible_indices));
    String8 text = {(char8*)arena_buffer_start(output), arena_buffer_size(output)};
    bool passed = string_equal(text,
        S8("TEST_PARALLEL_GANG_V1 status=started module_count=2 modules=self_test_last,self_test_first\n"
           "replayed-lane-output\n"
           "TEST_PARALLEL_GANG_V1 status=completed module_count=2 modules=self_test_last,self_test_first\n"));
    arguments.base.memory_report = false;
    test_parallel_gang_report(&arguments.base, S8("started"), descriptors, eligible_indices, BUSTER_ARRAY_LENGTH(eligible_indices));
    passed = passed && arena_buffer_size(output) == text.length;
    passed = arena_destroy(arena, 1) && passed;
    passed = arena_destroy(output, 1) && passed;
    return passed;
}

// The debugger-presence input is explicit so these positive and negative
// controls run without attaching a debugger or deliberately stopping CI.
BUSTER_GLOBAL_LOCAL bool test_debugger_failure_self_test(void)
{
    UnitTestArguments arguments = {0};
    bool passed = !arguments.suppress_debugger_break &&
                  !buster_test_debugger_stop_requested(0, false) && !buster_test_debugger_stop_requested(&arguments, false) &&
                  buster_test_debugger_stop_requested(0, true) && buster_test_debugger_stop_requested(&arguments, true);
    arguments.suppress_debugger_break = true;
    passed = passed && !buster_test_debugger_stop_requested(&arguments, false) && !buster_test_debugger_stop_requested(&arguments, true) &&
             buster_test_debugger_stop_requested(0, true);
    arguments.suppress_debugger_break = false;
    passed = passed && buster_test_debugger_stop_requested(&arguments, true);
    return passed;
}

// Harness regression: keep accounting out of the registered assertion totals.
// Failure output is deliberately buffered, rewound, overwritten, then inspected.
BUSTER_GLOBAL_LOCAL bool test_arena_self_test(void)
{
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .initial_size = BUSTER_KB(64), .flags = {.no_pool = 1}});
    Arena* output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .initial_size = BUSTER_KB(64), .flags = {.no_pool = 1}});
    BUSTER_CHECK(arena != 0 && output != 0);
    TestParallelArguments arguments = {
        .base = {.arena = arena, .show = &test_parallel_show, .memory_module = S8("arena_self_test"), .memory_report = true},
        .output_arena = output,
    };
    UnitTestResult result = {0};
    TestArenaScope outer = buster_test_arena_begin(&arguments.base, arena, S8("outer"), true);
    char8* live = arena_allocate(arena, char8, 3);
    memcpy(live, "abc", 3);
    TestArenaScope inner = buster_test_arena_begin(&arguments.base, arena, S8("inner"), false);
    memset(arena_allocate(arena, u8, 131077), 0x5a, 131077);
    arena_set_position(arena, inner.marks[0].start);
    memset(arena_allocate(arena, u8, 9), 0xa5, 9);
    buster_test_arena_end(&arguments.base, inner, true);
    bool passed = arena->position == 67 && arena->high_water == 131144 && memcmp(live, "abc", 3) == 0;
    u8* zeroed = arena_allocate_zeroed(arena, u8, 9);
    for (u32 index = 0; index < 9; index += 1)
    {
        passed = passed && zeroed[index] == 0;
    }
    arena_set_position(arena, inner.marks[0].start);
    buster_test_arena_end(&arguments.base, outer, true);

    TestArenaScope decommitted = buster_test_arena_begin(&arguments.base, arena, S8("decommitted"), false);
    memset(arena_allocate(arena, u8, 196608), 0x5a, 196608);
    passed = arena_set_position_and_decommit(arena, decommitted.marks[0].start) && passed;
    buster_test_arena_end(&arguments.base, decommitted, true);

    TestArenaScope retained = buster_test_arena_begin(&arguments.base, arena, S8("retained"), false);
    arena_allocate_bytes(arena, 17, 1);
    buster_test_arena_end(&arguments.base, retained, false);
    passed = passed && arena->position == 81;
    arena_set_position(arena, retained.marks[0].start);
    TestArenaScope empty = buster_test_arena_begin(&arguments.base, arena, S8("empty"), false);
    buster_test_arena_end(&arguments.base, empty, true);

    TestArenaScope failed = buster_test_arena_begin(&arguments.base, arena, S8("failed"), false);
    String8 diagnostic = string_format(arena, S8("fixture diagnostic survives rewind"));
    arguments.base.suppress_debugger_break = true;
    BUSTER_TEST_RAW(&arguments.base, false, diagnostic);
    arguments.base.suppress_debugger_break = false;
    buster_test_arena_end(&arguments.base, failed, true);
    memset(arena_allocate(arena, u8, 256), 0xa5, 256);
    passed = passed && result.test_count == 1 && result.succeeded_test_count == 0 &&
             buster_test_debugger_stop_requested(&arguments.base, true);
    String8 text = {(char8*)arena_buffer_start(output), arena_buffer_size(output)};
    passed = passed && string_first_sequence(text, S8("fixture diagnostic survives rewind failed at")) != BUSTER_STRING_NO_MATCH;
    passed = passed && string_first_sequence(text, S8("start=67 end=76 retained_bytes=9 high_water=131144 peak_bytes=131077 after=67 live_bytes=0 rewind=1")) != BUSTER_STRING_NO_MATCH;
    passed = passed && string_first_sequence(text, S8("start=64 end=67 retained_bytes=3 high_water=131144 peak_bytes=131080 after=64 live_bytes=0 rewind=1")) != BUSTER_STRING_NO_MATCH;
    passed = passed && string_first_sequence(text, S8("start=64 end=64 retained_bytes=0 high_water=196672 peak_bytes=196608 after=64 live_bytes=0 rewind=1")) != BUSTER_STRING_NO_MATCH;
    passed = passed && string_first_sequence(text, S8("start=64 end=81 retained_bytes=17 high_water=81 peak_bytes=17 after=81 live_bytes=17 rewind=0")) != BUSTER_STRING_NO_MATCH;
    passed = passed && string_first_sequence(text, S8("start=64 end=64 retained_bytes=0 high_water=64 peak_bytes=0 after=64 live_bytes=0 rewind=1")) != BUSTER_STRING_NO_MATCH;
    arguments.base.memory_report = false;
    TestArenaScope quiet = buster_test_arena_begin(&arguments.base, arena, S8("quiet"), false);
    arena_allocate_bytes(arena, 13, 1);
    buster_test_arena_end(&arguments.base, quiet, true);
    passed = passed && arena_buffer_size(output) == text.length;
    passed = arena_destroy(arena, 1) && passed;
    passed = arena_destroy(output, 1) && passed;
    return passed;
}

// Diagnostic-only clocks must not change assertion totals, arena ownership or
// failure output. This harness check has no duration threshold or sleep.
BUSTER_GLOBAL_LOCAL bool test_fixture_timing_self_test(void)
{
    bool passed = !test_fixture_timing_selected((String8){0}, S8("compiler_driver_tests")) &&
                  !test_fixture_timing_selected(S8("0"), S8("compiler_driver_tests")) &&
                  !test_fixture_timing_selected(S8("compiler_driver_tests"), S8("other")) &&
                  !test_fixture_timing_selected(S8("all"), (String8){0}) &&
                  test_fixture_timing_selected(S8("compiler_driver_tests"), S8("compiler_driver_tests")) &&
                  test_fixture_timing_selected(S8("all"), S8("other"));
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .initial_size = BUSTER_KB(64), .flags = {.no_pool = 1}});
    Arena* output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .initial_size = BUSTER_KB(64), .flags = {.no_pool = 1}});
    BUSTER_CHECK(arena != 0 && output != 0);
    TestParallelArguments arguments = {
        .base = {.arena = arena, .show = &test_parallel_show, .memory_module = S8("timing_self_test")},
        .output_arena = output,
    };
    UnitTestResult result = {0};
    TestArenaScope quiet = buster_test_arena_begin(&arguments.base, arena, S8("quiet"), false);
    buster_test_arena_end(&arguments.base, quiet, true);
    passed = passed && !quiet.timing && arena_buffer_size(output) == 0;

    arguments.base.fixture_timing_report = true;
    TestArenaScope module = buster_test_arena_begin(&arguments.base, arena, S8("body"), true);
    TestArenaScope first = buster_test_arena_begin(&arguments.base, arena, S8("repeated"), false);
    arena_allocate_bytes(arena, 17, 1);
    buster_test_arena_end(&arguments.base, first, true);
    passed = passed && arena->position == first.marks[0].start;
    TestArenaScope second = buster_test_arena_begin(&arguments.base, arena, S8("repeated"), false);
    String8 diagnostic = string_format(arena, S8("timed failure survives rewind"));
    arguments.base.suppress_debugger_break = true;
    BUSTER_TEST_RAW(&arguments.base, false, diagnostic);
    arguments.base.suppress_debugger_break = false;
    // A scope snapshots enablement; changing the next scope's policy must not
    // lose an already-started interval or expose an uninitialized timestamp.
    arguments.base.fixture_timing_report = false;
    buster_test_arena_end(&arguments.base, second, true);
    buster_test_arena_end(&arguments.base, module, true);
    memset(arena_allocate(arena, u8, 256), 0xa5, 256);
    String8 text = {(char8*)arena_buffer_start(output), arena_buffer_size(output)};
    passed = passed && module.timing && first.timing && second.timing && first.index == 0 && second.index == 1 &&
             arguments.base.memory_fixture_index == 2 && result.test_count == 1 && result.succeeded_test_count == 0 &&
             buster_test_debugger_stop_requested(&arguments.base, true);
    passed = passed && string_first_sequence(text, S8("TEST_FIXTURE_TIMING_V1 kind=fixture module=timing_self_test fixture=repeated index=0 duration_ns=")) != BUSTER_STRING_NO_MATCH;
    passed = passed && string_first_sequence(text, S8("TEST_FIXTURE_TIMING_V1 kind=fixture module=timing_self_test fixture=repeated index=1 duration_ns=")) != BUSTER_STRING_NO_MATCH;
    passed = passed && string_first_sequence(text, S8("TEST_FIXTURE_TIMING_V1 kind=module module=timing_self_test fixture=body index=0 duration_ns=")) != BUSTER_STRING_NO_MATCH;
    passed = passed && string_first_sequence(text, S8("measurement=inclusive-wall status=completed\n")) != BUSTER_STRING_NO_MATCH;
    passed = passed && string_first_sequence(text, S8("timed failure survives rewind failed at")) != BUSTER_STRING_NO_MATCH;
    passed = passed && string_first_sequence(text, S8("status=pass")) == BUSTER_STRING_NO_MATCH;
    quiet = buster_test_arena_begin(&arguments.base, arena, S8("quiet_again"), false);
    buster_test_arena_end(&arguments.base, quiet, true);
    passed = passed && !quiet.timing && arena_buffer_size(output) == text.length;
    passed = arena_destroy(arena, 1) && passed;
    passed = arena_destroy(output, 1) && passed;
    return passed;
}

// The watchdog's harness regression, without a thread, sleep or duration
// threshold: deadline parsing, start records, the transition protocol through
// nested scopes, expiry and the timeout record. test_fixture_watchdog_child
// in os_tests covers the thread and the process exit.
BUSTER_GLOBAL_LOCAL bool test_fixture_watchdog_self_test(void)
{
    TestFixtureDeadline defaulted = test_fixture_deadline_parse((String8){0});
    TestFixtureDeadline disabled = test_fixture_deadline_parse(S8("0"));
    TestFixtureDeadline largest = test_fixture_deadline_parse(S8("18446744073709"));
    bool passed = defaulted.valid && defaulted.seconds == TEST_FIXTURE_TIMEOUT_DEFAULT_SECONDS && disabled.valid && !disabled.seconds &&
                  largest.valid && largest.seconds == 18446744073709ull;
    String8 invalid[] = {S8("1s"), S8("-1"), S8(" 1"), S8("18446744073710")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid); index += 1)
    {
        TestFixtureDeadline parsed = test_fixture_deadline_parse(invalid[index]);
        passed = passed && !parsed.valid && parsed.seconds == TEST_FIXTURE_TIMEOUT_DEFAULT_SECONDS;
    }

    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .initial_size = BUSTER_KB(64), .flags = {.no_pool = 1}});
    Arena* output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .initial_size = BUSTER_KB(64), .flags = {.no_pool = 1}});
    BUSTER_CHECK(arena != 0 && output != 0);
    TestWatchSlot slot = {0};
    TestParallelArguments arguments = {
        .base = {.arena = arena, .show = &test_parallel_show, .memory_module = S8("watchdog_self_test"), .memory_report = true, .watch_slot = &slot},
        .output_arena = output,
    };
    TestArenaScope module = buster_test_arena_begin(&arguments.base, arena, S8("body"), true);
    TestWatchSnapshot body = test_watch_snapshot(&slot);
    TestArenaScope outer = buster_test_arena_begin(&arguments.base, arena, S8("outer"), false);
    TestArenaScope inner = buster_test_arena_begin(&arguments.base, arena, S8("inner"), false);
    TestWatchSnapshot nested = test_watch_snapshot(&slot);
    buster_test_arena_end(&arguments.base, inner, true);
    TestWatchSnapshot resumed = test_watch_snapshot(&slot);
    buster_test_arena_end(&arguments.base, outer, true);
    TestWatchSnapshot body_again = test_watch_snapshot(&slot);
    // Watching does not depend on reporting: a quiet scope still moves the slot.
    arguments.base.memory_report = false;
    u64 reported = arena_buffer_size(output);
    TestArenaScope quiet = buster_test_arena_begin(&arguments.base, arena, S8("quiet"), false);
    TestWatchSnapshot quiet_nested = test_watch_snapshot(&slot);
    buster_test_arena_end(&arguments.base, quiet, true);
    buster_test_arena_end(&arguments.base, module, true);
    TestWatchSnapshot closed = test_watch_snapshot(&slot);

    passed = passed && body.consistent && body.module_scope && string_equal(body.module, S8("watchdog_self_test")) &&
             string_equal(body.fixture, S8("body")) && !body.last_completed.length;
    passed = passed && nested.consistent && !nested.module_scope && string_equal(nested.fixture, S8("inner")) && nested.index == 1 &&
             !nested.last_completed.length;
    passed = passed && resumed.consistent && !resumed.module_scope && string_equal(resumed.fixture, S8("outer")) && resumed.index == 0 &&
             string_equal(resumed.last_completed, S8("inner"));
    passed = passed && body_again.module_scope && string_equal(body_again.fixture, S8("body")) && string_equal(body_again.last_completed, S8("outer"));
    passed = passed && string_equal(quiet_nested.fixture, S8("quiet")) && arena_buffer_size(output) == reported;
    passed = passed && closed.consistent && !closed.fixture.length && string_equal(closed.last_completed, S8("body")) &&
             !test_watch_expired(closed, UINT64_MAX, 0);

    // A start record precedes everything its scope reports.
    String8 text = {(char8*)arena_buffer_start(output), arena_buffer_size(output)};
    u64 inner_start = string_first_sequence(text, S8("TEST_FIXTURE_START_V1 kind=fixture module=watchdog_self_test fixture=inner index=1\n"));
    u64 inner_end = string_first_sequence(text, S8("TEST_ARENA_V1 kind=fixture module=watchdog_self_test fixture=inner index=1 arena_slot=0 "));
    passed = passed && string_first_sequence(text, S8("TEST_FIXTURE_START_V1 kind=module module=watchdog_self_test fixture=body index=0\n")) == 0;
    passed = passed && string_first_sequence(text, S8("TEST_FIXTURE_START_V1 kind=fixture module=watchdog_self_test fixture=outer index=0\n")) != BUSTER_STRING_NO_MATCH;
    passed = passed && inner_start != BUSTER_STRING_NO_MATCH && inner_end != BUSTER_STRING_NO_MATCH && inner_start < inner_end;
    passed = passed && string_first_sequence(text, S8("fixture=quiet")) == BUSTER_STRING_NO_MATCH;

    TestWatchSnapshot stalled = resumed;
    stalled.since_microseconds = 5000000;
    passed = passed && !test_watch_expired(stalled, 5999999, 1000000) && test_watch_expired(stalled, 6000000, 1000000) &&
             !test_watch_expired(stalled, 4000000, 1000000);
    String8 report = test_watch_report(arena, stalled, 6250000, 1);
    passed = passed && string_equal(report,
        S8("TEST_FIXTURE_TIMEOUT_V1 kind=fixture module=watchdog_self_test fixture=outer index=0 elapsed_ms=1250 deadline_seconds=1 last_completed=inner\n"));
    stalled.consistent = false;
    passed = passed && !test_watch_expired(stalled, 6000000, 1000000);
    passed = arena_destroy(arena, 1) && passed;
    passed = arena_destroy(output, 1) && passed;
    return passed;
}

BUSTER_GLOBAL_LOCAL bool test_require_self_test(void)
{
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .flags = {.no_pool = true}});
    Arena* output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .flags = {.no_pool = true}});
    BUSTER_CHECK(arena != 0 && output != 0);
    TestParallelArguments arguments = {
        .base = {.arena = arena, .show = &test_parallel_show},
        .output_arena = output,
    };
    UnitTestResult result = {0};
    String8 missing_path = string_format_z(arena, S8("tests/buster-require-missing-fixture-{u64}"), os_get_current_process_id());
    ByteSlice missing_fixture = file_read(arena, missing_path, (FileReadOptions){0});
    bool failed_dependent_called = false;
    arguments.base.suppress_debugger_break = true;
    bool fixture_available = BUSTER_REQUIRE(&arguments.base, missing_fixture.pointer != 0);
    arguments.base.suppress_debugger_break = false;
    if (fixture_available)
    {
        failed_dependent_called = true;
        BUSTER_TEST(&arguments.base, missing_fixture.pointer[0] == 0);
    }
    bool successful_dependent_called = false;
    bool prerequisite_available = true;
    if (BUSTER_REQUIRE(&arguments.base, prerequisite_available))
    {
        successful_dependent_called = true;
        BUSTER_TEST(&arguments.base, true);
    }
    String8 text = {(char8*)arena_buffer_start(output), arena_buffer_size(output)};
    bool passed = !failed_dependent_called && successful_dependent_called && result.test_count == 3 && result.succeeded_test_count == 2 &&
                  buster_test_debugger_stop_requested(&arguments.base, true) &&
                  string_first_sequence(text, S8("missing_fixture.pointer != 0 failed at")) != BUSTER_STRING_NO_MATCH;
    passed = arena_destroy(arena, 1) && passed;
    passed = arena_destroy(output, 1) && passed;
    return passed;
}

BUSTER_GLOBAL_LOCAL UnitTestResult test_parallel_call(TestDescriptorParallelKind kind, UnitTestArguments* arguments)
{
    switch (kind)
    {
        case TEST_DESCRIPTOR_PARALLEL_AARCH64_DIRECT_SIMD: return aarch64_direct_simd_tests(arguments);
        case TEST_DESCRIPTOR_PARALLEL_AARCH64_COMPLEX_SIMD: return aarch64_complex_simd_tests(arguments);
        case TEST_DESCRIPTOR_PARALLEL_AARCH64_MEMORY_SEMANTICS: return aarch64_memory_semantics_tests(arguments);
        case TEST_DESCRIPTOR_PARALLEL_NONE: break;
    }
    return (UnitTestResult){0};
}

BUSTER_GLOBAL_LOCAL ThreadReturnType test_parallel_lane(void* argument)
{
    TestParallelState* state = (TestParallelState*)argument;
    String8 crash_module = os_get_environment_variable(S8("BUSTER_TEST_PARALLEL_CRASH_CHILD_MODE"));
    LaneRange range = lane_range(state->eligible_count);
    for (u64 work_index = range.start; work_index < range.end; work_index += 1)
    {
        u64 descriptor_index = state->eligible_indices[work_index];
        TestDescriptor descriptor = state->descriptors[descriptor_index];
        // Lazy reservation shared with the serial runner. Fixture marks bound
        // logical retention; this is capacity, not an RSS measurement or budget.
        Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(512), .initial_size = BUSTER_KB(64)});
        Arena* output_arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .initial_size = BUSTER_KB(64)});
        BUSTER_CHECK(arena != 0 && output_arena != 0);
        TestParallelArguments arguments = {
            .base = {.arena = arena, .show = &test_parallel_show, .memory_module = descriptor.name, .memory_report = state->memory_report,
                     .fixture_timing_report = test_fixture_timing_selected(os_get_environment_variable(S8("BUSTER_TEST_FIXTURE_TIMING")), descriptor.name),
                     .watch_slot = state->watch_slots ? state->watch_slots + descriptor_index : 0},
            .output_arena = output_arena,
        };
        TestArenaScope module_scope = buster_test_arena_begin(&arguments.base, arena, S8("body"), true);
        if (string_equal(crash_module, descriptor.name))
        {
            os_fail_message(S8("parallel lane crash self-test"));
        }
        TimeDataType start = timestamp_take();
        UnitTestResult result = test_parallel_call(descriptor.parallel_kind, &arguments.base);
        TimeDataType end = timestamp_take();
        buster_test_arena_end(&arguments.base, module_scope, true);
        TestParallelRecord* record = &state->records[descriptor_index];
        record->timing = (TestTimingRecord){.index = descriptor_index, .module = descriptor.name, .duration_ns = timestamp_ns_between(start, end), .result = result};
        record->output_pointer = (char8*)arena_buffer_start(output_arena);
        record->output_length = arena_buffer_size(output_arena);
        record->output_arena = output_arena;
        record->completed = true;
        BUSTER_CHECK(arena_destroy(arena, 1));
    }
}

#endif

bool unit_test_succeeded(UnitTestResult result)
{
    return result.succeeded_test_count == result.test_count;
}

void consume_unit_tests(BatchTestResult* batch, UnitTestResult unit_test)
{
    batch->succeeded_unit_test_count += unit_test.succeeded_test_count;
    batch->unit_test_count += unit_test.test_count;
    batch->succeeded_module_test_count += unit_test_succeeded(unit_test);
    batch->module_test_count += 1;
}

void consume_external_tests(BatchTestResult* batch, ProcessResult result)
{
    batch->succeeded_external_test_count += result == PROCESS_RESULT_SUCCESS;
    batch->external_test_count += 1;
}

void buster_test_error(u32 line, String8 function, String8 file_path, String8 format, ...)
{
    TemporalArena scratch = scratch_begin(0, 0);
    va_list variable_arguments;
    va_start(variable_arguments, format);
    String8 message = string_format_va(scratch.arena, format, variable_arguments, STRING_FORMAT_VA_GP_SLOTS(7));
    va_end(variable_arguments);

    string_print(S8("{S8} failed at {S8}:{S8}:{u32}\n"), message, file_path, function, line);
    scratch_end(scratch);

    if (buster_test_debugger_stop_requested(0, is_debugger_present()))
    {
        os_fail();
    }
}

void buster_test_error_arguments(UnitTestArguments* arguments, u32 line, String8 function, String8 file_path, String8 format, ...)
{
    TemporalArena scratch = scratch_begin(0, 0);
    va_list variable_arguments;
    va_start(variable_arguments, format);
    String8 message = string_format_va(scratch.arena, format, variable_arguments, STRING_FORMAT_VA_GP_SLOTS(8));
    va_end(variable_arguments);

    arguments->show(arguments, S8("{S8} failed at {S8}:{S8}:{u32}\n"), message, file_path, function, line);
    scratch_end(scratch);

    if (buster_test_debugger_stop_requested(arguments, is_debugger_present()))
    {
        os_fail();
    }
}

bool buster_test_require_arguments(UnitTestArguments* arguments, UnitTestResult* result, bool success, u32 line, String8 function, String8 file_path,
                                   String8 expression)
{
    if (!success)
    {
        buster_test_error_arguments(arguments, line, function, file_path, S8("{S8}"), expression);
    }
    result->succeeded_test_count += success;
    result->test_count += 1;
    return success;
}

BUSTER_GLOBAL_LOCAL String8 buster_test_temporary_root;
BUSTER_GLOBAL_LOCAL u64 buster_test_temporary_root_serial;
BUSTER_GLOBAL_LOCAL Arena* buster_test_temporary_root_arena;
BUSTER_GLOBAL_LOCAL UnitTestArguments* buster_test_temporary_arguments;
BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_failed;
BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_owned;
BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_ready;
BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_failure_injected;
BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_failure_self_test_active;
#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_failure_body_called;
#endif
BUSTER_GLOBAL_LOCAL u64 buster_test_temporary_path_call_count;
BUSTER_GLOBAL_LOCAL AtomicU64 buster_test_temporary_unique_path_serial;

BUSTER_GLOBAL_LOCAL String8 buster_test_temporary_base(void)
{
#if BUSTER_ANDROID
    String8 result = buster_android_internal_data_path.length ? buster_android_internal_data_path : S8(".");
#else
    // The GitHub-hosted privacy broker keeps every source-derived byte on a
    // memory-backed filesystem; this override points the fixture root there
    // instead of the platform default, which persists on the host disk.
    String8 result = os_get_environment_variable(S8("BUSTER_TEST_TEMPORARY_BASE"));
    if (!result.length)
    {
#if BUSTER_WINDOWS
        result = S8("build/");
#else
        result = S8("/tmp/");
#endif
    }
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_make_directory(String8 path)
{
#if defined(__linux__) || defined(__APPLE__)
    BUSTER_CHECK(!path.pointer[path.length]);
    return mkdir((const char*)path.pointer, 0700) == 0;
#elif defined(_WIN32)
    TemporalArena scratch = scratch_begin(0, 0);
    String16 path_w = string16_from_string8(scratch.arena, path, true);
    bool result = CreateDirectoryW(path_w.pointer, 0) != 0;
    scratch_end(scratch);
    return result;
#else
    BUSTER_UNUSED(path);
    return false;
#endif
}

BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_create(void)
{
    UnitTestArguments* arguments = buster_test_temporary_arguments;
    Arena* arena = buster_test_temporary_root_arena;
    if (!arguments || !arena)
    {
        string_print(S8("TEST_TEMPORARY_ROOT_SETUP status=failed reason=no active library test run\n"));
        return false;
    }

    if (buster_test_temporary_root_failure_injected)
    {
        return false;
    }

    String8 base = buster_test_temporary_base();
    String8 separator = base.length && (base.pointer[base.length - 1] == '/' || base.pointer[base.length - 1] == '\\') ? S8("") : S8("/");
    // The PID separates simultaneous test processes. The serial separates
    // repeated library_tests calls in one process, and the monotonic clock
    // prevents a reused PID from selecting an old root in the same boot.
    u64 process_id = os_get_current_process_id();
    u64 serial = ++buster_test_temporary_root_serial;
    u64 timestamp = timestamp_ns_between((TimeDataType){0}, timestamp_take());
    buster_test_temporary_root = string_format_z(arena, S8("{S8}{S8}buster-tests-{u64}-{u64}-{u64}"), base, separator, process_id, serial, timestamp);

    // The shared OS helper intentionally has no status return. The harness
    // needs exclusive ownership so a collision can never make teardown delete
    // a foreign directory, so create the leaf with the platform's exclusive
    // directory primitive and diagnose every failure.
    if (!buster_test_temporary_root_make_directory(buster_test_temporary_root))
    {
        arguments->show(arguments, S8("TEST_TEMPORARY_ROOT_SETUP status=failed path={S8} error={EOs}\n"), buster_test_temporary_root,
                        os_get_last_error());
        buster_test_temporary_root = (String8){0};
        return false;
    }
    buster_test_temporary_root_owned = true;

    // Probe the owned root before publishing it to callers. This catches a
    // read-only or otherwise unusable base without returning an invalid path.
    String8 probe_path = string_format_z(arena, S8("{S8}/.root-probe"), buster_test_temporary_root);
    bool probe_contained = string_starts_with_sequence(probe_path, buster_test_temporary_root) && probe_path.length > buster_test_temporary_root.length &&
                           probe_path.pointer[buster_test_temporary_root.length] == '/';
    BUSTER_CHECK(probe_contained);
    OsFileDescriptor* probe = os_file_open(probe_path, (OpenFlags){.read = 1, .write = 1, .create = 1},
                                           (OpenPermissions){.read = 1, .write = 1});
    bool result = probe != 0;
    if (probe)
    {
        result = os_file_close(probe) && result;
        result = os_file_delete(probe_path) && result;
    }

    if (!result)
    {
        arguments->show(arguments, S8("TEST_TEMPORARY_ROOT_SETUP status=failed path={S8} error={EOs}\n"), buster_test_temporary_root,
                        os_get_last_error());
        buster_test_temporary_root_ready = false;
        return false;
    }

    buster_test_temporary_root_ready = true;
    return result;
}

BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_delete(UnitTestArguments* arguments)
{
    String8 root = buster_test_temporary_root;
    bool result = buster_test_temporary_root_owned && os_directory_delete(root);
    if (!result)
    {
        arguments->show(arguments, S8("TEST_TEMPORARY_ROOT_CLEANUP status=failed path={S8} error={EOs}\n"), root, os_get_last_error());
    }
    buster_test_temporary_root = (String8){0};
    buster_test_temporary_root_owned = false;
    buster_test_temporary_root_ready = false;
    return result;
}

BUSTER_GLOBAL_LOCAL bool buster_test_temporary_component_is_safe(String8 component)
{
    if (string_equal(component, S8(".")) || string_equal(component, S8("..")))
    {
        return false;
    }
    for (u64 index = 0; index < component.length; index += 1)
    {
        if (component.pointer[index] == '/' || component.pointer[index] == '\\')
        {
            return false;
        }
    }
    return true;
}

String8 buster_test_temporary_path(Arena* arena, String8 name, String8 suffix)
{
    if (buster_test_temporary_root_failure_self_test_active)
    {
        buster_test_temporary_path_call_count += 1;
    }

    // Some tests intentionally exec this binary and exit from a child mode.
    // Create the root lazily so those children do not leave an empty root when
    // they never request a test artifact; the parent still owns final cleanup.
    if (!buster_test_temporary_root.length && !buster_test_temporary_root_failed)
    {
        buster_test_temporary_root_failed = !buster_test_temporary_root_create();
    }
    if (!buster_test_temporary_root_ready)
    {
        return (String8){0};
    }

    BUSTER_CHECK(buster_test_temporary_component_is_safe(name));
    BUSTER_CHECK(buster_test_temporary_component_is_safe(suffix));
    String8 result = string_format_z(arena, S8("{S8}/{S8}-{u64}{S8}"), buster_test_temporary_root, name, os_get_current_process_id(), suffix);
    bool root_contained = string_starts_with_sequence(result, buster_test_temporary_root) && result.length > buster_test_temporary_root.length &&
                          (result.pointer[buster_test_temporary_root.length] == '/' || result.pointer[buster_test_temporary_root.length] == '\\');
    BUSTER_CHECK(root_contained);
    return result;
}

String8 buster_test_temporary_unique_path(Arena* arena, String8 name, String8 suffix)
{
    // Concurrent fixtures share the counter, so take each serial atomically.
    u64 serial = atomic_u64_increment(&buster_test_temporary_unique_path_serial);
    String8 unique_suffix = string_format(arena, S8("-{u64}{S8}"), serial, suffix);
    String8 result = buster_test_temporary_path(arena, name, unique_suffix);
    return result;
}

void default_show(UnitTestArguments* arguments, String8 format, ...)
{
    BUSTER_UNUSED(arguments);
    TemporalArena scratch = scratch_begin(0, 0);
    va_list variable_arguments;
    va_start(variable_arguments, format);
    String8 string = string_format_va(scratch.arena, format, variable_arguments, STRING_FORMAT_VA_GP_SLOTS(3));
    va_end(variable_arguments);

    if (string.length)
    {
        os_file_write(os_get_stdout(), BUSTER_SLICE_TO_BYTE_SLICE(string));
    }

    scratch_end(scratch);
}

bool batch_test_succeeded(BatchTestResult test)
{
    bool unit_result = test.succeeded_unit_test_count == test.unit_test_count;
    bool module_result = test.succeeded_module_test_count == test.module_test_count;
    bool external_result = test.succeeded_external_test_count == test.external_test_count;

    bool result = unit_result && module_result && external_result;
    return result;
}

bool batch_test_report(UnitTestArguments* arguments, BatchTestResult test)
{
    // A selected run states its share of the suite so it cannot be mistaken
    // for a full one; a full run keeps the unannotated line.
    if (test.registered_module_count)
    {
        arguments->show(arguments, S8("[{u64}/{u64}] Unit tests ({u64} of {u64} modules selected)\n"), test.succeeded_unit_test_count,
                        test.unit_test_count, test.selected_module_count, test.registered_module_count);
    }
    else
    {
        arguments->show(arguments, S8("[{u64}/{u64}] Unit tests\n"), test.succeeded_unit_test_count, test.unit_test_count);
    }
    arguments->show(arguments, S8("[{u64}/{u64}] Module tests\n"), test.succeeded_module_test_count, test.module_test_count);
    arguments->show(arguments, S8("[{u64}/{u64}] External tests\n"), test.succeeded_external_test_count, test.external_test_count);
    return batch_test_succeeded(test);
}

#if BUSTER_INCLUDE_TESTS
u64 buster_test_worker_count(u64 requested)
{
#if BUSTER_SINGLE_THREADED
    BUSTER_UNUSED(requested);
    return 1;
#else
    u64 result = BUSTER_MAX(requested, (u64)1);
    String8 jobs_text = os_get_environment_variable(S8("BUSTER_TEST_JOBS"));
    if (jobs_text.length)
    {
        IntegerParsingU64 parsed = string8_parse_u64_decimal(jobs_text);
        if (parsed.status == INTEGER_PARSING_SUCCESS && parsed.length == jobs_text.length && parsed.value)
        {
            result = BUSTER_MIN(result, parsed.value);
        }
    }
    return result;
#endif
}

BUSTER_GLOBAL_LOCAL BatchTestResult buster_test_run_descriptors(UnitTestArguments* arguments, TestDescriptor* descriptors, u64 descriptor_count,
                                                                 bool timing_enabled, u64* timing_record_count, u64 descriptor_index_base)
{
    BatchTestResult result = {0};
    for (u64 i = 0; i < descriptor_count; i += 1)
    {
        TestDescriptor descriptor = descriptors[i];
        BatchTestResult result_before_descriptor = result;

        // A module selection left this descriptor out, or another tree in
        // this matrix owns the whole-table audits. Skip without a timing row
        // so the module's per-runner timing series stays a series of real
        // runs rather than one salted with zeroes.
        if (!buster_test_descriptor_runs(descriptor)) continue;

        // A descriptor that can create an artifact must not enter its body
        // until the root has been created and probed. Otherwise a setup
        // failure would turn the empty path returned by the accessor into
        // paths such as /top.txt when the descriptor formats a child path.
        if (descriptor.requires_temporary_root && !buster_test_temporary_root.length && !buster_test_temporary_root_failed)
        {
            buster_test_temporary_root_failed = !buster_test_temporary_root_create();
        }
        if (buster_test_temporary_root_failed)
        {
            result = result_before_descriptor;
            result.unit_test_count += 1;
            break;
        }

        arguments->memory_module = descriptor.name;
        arguments->memory_fixture_index = 0;
        arguments->fixture_timing_report = test_fixture_timing_selected(os_get_environment_variable(S8("BUSTER_TEST_FIXTURE_TIMING")), descriptor.name);
        // Indexed like the parallel lanes', by the descriptor's table position.
        BUSTER_CHECK(!test_watchdog.slots || descriptor_index_base + i < test_watchdog.slot_count);
        arguments->watch_slot = test_watchdog.slots ? test_watchdog.slots + descriptor_index_base + i : 0;
        TestArenaScope module_scope = buster_test_arena_begin(arguments, arguments->arena, S8("body"), true);
        if (timing_enabled)
        {
            TestTimingRecord timing = test_timing_run_descriptor(arguments, descriptor, descriptor_index_base + i);
            consume_unit_tests(&result, timing.result);
            buster_test_arena_end(arguments, module_scope, true);
            test_timing_report(arguments, timing);
            *timing_record_count += 1;
        }
        else
        {
            UnitTestResult unit_test_result = descriptor.function(arguments);
            consume_unit_tests(&result, unit_test_result);
            buster_test_arena_end(arguments, module_scope, true);
        }
        arguments->watch_slot = 0;

        if (buster_test_temporary_root_failed)
        {
            // A failed root makes every later artifact path unusable. Discard
            // the descriptor's downstream path failures and report exactly
            // one deterministic harness failure instead, preserving all
            // aggregate totals completed before this descriptor.
            result = result_before_descriptor;
            result.unit_test_count += 1;
            break;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL BatchTestResult buster_test_run_parallel_descriptors(UnitTestArguments* arguments, TestDescriptor* descriptors, u64 descriptor_count,
                                                                          bool timing_enabled, u64* timing_record_count)
{
    BatchTestResult result = {0};
    // The table places the initial lane's group, whichever of its members
    // run, so a module selection cannot move a module across the gang.
    u64 group_start = descriptor_count;
    u64 group_end = 0;
    for (u64 index = 0; index < descriptor_count; index += 1)
    {
        if (descriptors[index].parallel_kind != TEST_DESCRIPTOR_PARALLEL_NONE)
        {
            group_start = BUSTER_MIN(group_start, index);
            group_end = index + 1;
        }
    }
    if (group_start == descriptor_count)
    {
        return buster_test_run_descriptors(arguments, descriptors, descriptor_count, timing_enabled, timing_record_count, 0);
    }

    u64 arena_position = arguments->arena->position;
    u64* eligible_indices = arena_allocate(arguments->arena, u64, group_end - group_start);
    TestParallelRecord* records = arena_allocate(arguments->arena, TestParallelRecord, descriptor_count);
    memset(records, 0, sizeof(*records) * descriptor_count);
    u64 eligible_count = 0;
    bool group_or_suffix_runs = false;
    for (u64 index = group_start; index < descriptor_count; index += 1)
    {
        bool runs = buster_test_descriptor_runs(descriptors[index]);
        if (index < group_end)
        {
            // The initial lane is one contiguous, side-effect-free group.
            BUSTER_CHECK(descriptors[index].parallel_kind != TEST_DESCRIPTOR_PARALLEL_NONE);
            if (runs)
            {
                eligible_indices[eligible_count++] = index;
            }
        }
        group_or_suffix_runs = group_or_suffix_runs || runs;
    }

    // Lane rows stay buffered for deterministic replay. A live serial gang
    // boundary names their exact set on a crash; watchdog slots name hangs down
    // to the innermost open scope.
    TestParallelState state = {
        .descriptors = descriptors,
        .eligible_indices = eligible_indices,
        .records = records,
        .watch_slots = test_watchdog.slots,
        .eligible_count = eligible_count,
        .memory_report = arguments->memory_report,
    };

    // Run the serial prefix first and suffix after replay so
    // thread-count-sensitive modules (notably os_tests) never overlap the gang.
    if (group_start)
    {
        BatchTestResult prefix = buster_test_run_descriptors(arguments, descriptors, group_start, timing_enabled, timing_record_count, 0);
        result.succeeded_unit_test_count += prefix.succeeded_unit_test_count;
        result.unit_test_count += prefix.unit_test_count;
        result.succeeded_module_test_count += prefix.succeeded_module_test_count;
        result.module_test_count += prefix.module_test_count;
        result.succeeded_external_test_count += prefix.succeeded_external_test_count;
        result.external_test_count += prefix.external_test_count;
        if (buster_test_temporary_root_failed)
        {
            arena_set_position(arguments->arena, arena_position);
            return result;
        }
    }

    // Group and suffix modules start from these tables in a full run, so
    // they do in a selected run too, even one that runs none of the group.
    if (group_or_suffix_runs)
    {
        compiler_prewarm();
        // The lanes below query arbitrary forms, so every form is prepared here
        // rather than on first touch; the compiler itself never needs this walk.
        buster_x86_metadata_prewarm_all_forms();
        // The metadata and machine suites exercise x86 emission on every host,
        // including AArch64 CI. Prepare the exact-plan tables, with every shape
        // resolved, before their lanes.
        machine_x86_64_exact_prewarm_all_shapes();
        // Every lane in the gang below is an aarch64 suite, and each one queries
        // canonical form validity per encode and per decode.
        buster_aarch64_prewarm();
        buster_aarch64_semantics_prewarm();
    }
    if (eligible_count)
    {
        u64 requested_lanes = buster_test_worker_count(eligible_count);
        test_parallel_gang_report(arguments, S8("started"), descriptors, eligible_indices, eligible_count);
        lane_run(BUSTER_MIN(requested_lanes, eligible_count), &test_parallel_lane, &state);
    }

    for (u64 work_index = 0; work_index < eligible_count; work_index += 1)
    {
        TestParallelRecord* record = &records[eligible_indices[work_index]];
        BUSTER_CHECK(record->completed);
        consume_unit_tests(&result, record->timing.result);
        if (record->output_length)
        {
            arguments->show(arguments, S8("{S8}"), (String8){record->output_pointer, record->output_length});
        }
        if (timing_enabled)
        {
            test_timing_report(arguments, record->timing);
            *timing_record_count += 1;
        }
        BUSTER_CHECK(arena_destroy(record->output_arena, 1));
    }
    test_parallel_gang_report(arguments, S8("completed"), descriptors, eligible_indices, eligible_count);

    if (group_end < descriptor_count)
    {
        BatchTestResult suffix = buster_test_run_descriptors(arguments, descriptors + group_end, descriptor_count - group_end,
                                                             timing_enabled, timing_record_count, group_end);
        result.succeeded_unit_test_count += suffix.succeeded_unit_test_count;
        result.unit_test_count += suffix.unit_test_count;
        result.succeeded_module_test_count += suffix.succeeded_module_test_count;
        result.module_test_count += suffix.module_test_count;
        result.succeeded_external_test_count += suffix.succeeded_external_test_count;
        result.external_test_count += suffix.external_test_count;
    }
    arena_set_position(arguments->arena, arena_position);
    return result;
}

#if BUSTER_TEST_WATCHDOG_SUPPORTED
BUSTER_GLOBAL_LOCAL UnitTestResult test_watchdog_hang_fixture(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    // Spins like the stale-index scan behind #1575. The bound matters only if
    // the watchdog failed, and the parent's wait deadline ends the child first.
    enum { TEST_WATCHDOG_CHILD_SPIN_MICROSECONDS = 60000000 };
    u64 start = os_now_microseconds();
    while (os_now_microseconds() - start < TEST_WATCHDOG_CHILD_SPIN_MICROSECONDS)
    {
    }
    return (UnitTestResult){0, 1};
}

BUSTER_GLOBAL_LOCAL UnitTestResult test_watchdog_hang_module(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, test_watchdog_hang_fixture);
    return result;
}

// The private payload of test_fixture_watchdog_child: one hanging descriptor
// through the ordinary serial runner under the environment's deadline, with no
// prewarm and no registered module. Reaching the exit means the watchdog never
// ended the process.
BUSTER_GLOBAL_LOCAL void test_watchdog_child_run(UnitTestArguments* arguments)
{
    enum { TEST_WATCHDOG_CHILD_UNEXPECTED_RETURN = 125 };
    TestDescriptor descriptor = {.name = S8("fixture_watchdog_self_test"), .function = &test_watchdog_hang_module};
    arguments->memory_report = true;
    if (test_watchdog_start(arguments, 1))
    {
        u64 timing_record_count = 0;
        buster_test_run_descriptors(arguments, &descriptor, 1, false, &timing_record_count, 0);
    }
    os_exit(TEST_WATCHDOG_CHILD_UNEXPECTED_RETURN);
}
#endif

// Harness regression for `--module=`: names match exactly, every bad entry is
// counted rather than skipped, and a deselected descriptor neither runs nor
// reports a timing row while later rows keep their table index.
BUSTER_GLOBAL_LOCAL bool test_module_selection_self_test(void)
{
    TestModuleSelection pair = test_module_selection_resolve(S8("object_tests,ir_tests,object_tests"), false);
    u64 selected_count = 0;
    for (u64 id = 0; id < TEST_ID_COUNT; id += 1)
    {
        selected_count += pair.selected[id];
    }
    bool passed = !pair.invalid_count && selected_count == 2 && pair.selected[TEST_ID_OBJECT] && pair.selected[TEST_ID_IR];
    String8 invalid[] = {S8(""), S8("object_tests,"), S8(",object_tests"), S8("object_tests,,ir_tests"), S8("Object_tests"), S8("object_test"),
                         S8("object_tests ")};
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(invalid); index += 1)
    {
        passed = passed && test_module_selection_resolve(invalid[index], false).invalid_count == 1;
    }

    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .flags = {.no_pool = true}});
    Arena* output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .flags = {.no_pool = true}});
    BUSTER_CHECK(arena != 0 && output != 0);
    TestParallelArguments arguments = {
        .base = {.arena = arena, .show = &test_parallel_show},
        .output_arena = output,
    };
    TestDescriptor descriptors[] = {
        {.name = S8("self_test_first"), .function = &test_timing_self_test_pass},
        {.name = S8("self_test_deselected"), .function = &test_timing_self_test_fail, .deselected = true},
        {.name = S8("self_test_last"), .function = &test_timing_self_test_pass},
    };
    u64 timing_record_count = 0;
    BatchTestResult batch = buster_test_run_descriptors(&arguments.base, descriptors, BUSTER_ARRAY_LENGTH(descriptors), true, &timing_record_count, 0);
    String8 text = {(char8*)arena_buffer_start(output), arena_buffer_size(output)};
    passed = passed && timing_record_count == 2 && batch.module_test_count == 2 && batch.succeeded_module_test_count == 2 && batch.unit_test_count == 4 &&
             batch.succeeded_unit_test_count == 4;
    passed = passed && string_first_sequence(text, S8("module=self_test_deselected")) == BUSTER_STRING_NO_MATCH;
    passed = passed && string_first_sequence(text, S8("TEST_MODULE_TIMING index=2 module=self_test_last ")) != BUSTER_STRING_NO_MATCH;
    passed = arena_destroy(arena, 1) && passed;
    passed = arena_destroy(output, 1) && passed;
    return passed;
}

// Check the real registration table, then corrupt a small table's identity,
// audit policy and coverage. Neither a missing owner nor a successful empty
// partition can satisfy the semantic union used by the process orchestrator.
BUSTER_GLOBAL_LOCAL bool test_module_group_self_test(void)
{
    TestDescriptor primary[TEST_ID_COUNT];
    TestDescriptor rest[TEST_ID_COUNT];
    memcpy(primary, test_descriptors, sizeof(primary));
    memcpy(rest, test_descriptors, sizeof(rest));
    u64 primary_count = test_module_group_apply(primary, TEST_ID_COUNT, TEST_MODULE_GROUP_PRIMARY, true);
    u64 rest_count = test_module_group_apply(rest, TEST_ID_COUNT, TEST_MODULE_GROUP_REST, true);
    u64 enabled_count = 0;
    for (u64 index = 0; index < TEST_ID_COUNT; index += 1)
    {
        enabled_count += buster_test_descriptor_runs(test_descriptors[index]);
    }
    bool result = primary_count == 1 && rest_count > 0 && primary_count + rest_count == enabled_count &&
                  test_module_group_union_valid(test_descriptors, primary, rest, TEST_ID_COUNT);
    result = result && test_module_group_resolve(S8("")) == TEST_MODULE_GROUP_NONE &&
             test_module_group_resolve(S8("primary")) == TEST_MODULE_GROUP_PRIMARY &&
             test_module_group_resolve(S8("rest")) == TEST_MODULE_GROUP_REST &&
             test_module_group_resolve(S8("inventory")) == TEST_MODULE_GROUP_INVENTORY;
    String8 invalid[] = {S8("all"), S8("driver"), S8("frontend"), S8("Primary"), S8("primary,rest"), S8(" primary"), S8("rest ")};
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(invalid); index += 1)
    {
        result = result && test_module_group_resolve(invalid[index]) == TEST_MODULE_GROUP_INVALID;
    }

    TestDescriptor original[] = {
        {.name = S8("self_test_first"), .function = &test_timing_self_test_pass},
        {.name = test_module_group_primary_name(), .function = &test_timing_self_test_pass},
        {.name = S8("self_test_audit"), .function = &test_timing_self_test_pass, .table_audit = true},
    };
    TestDescriptor synthetic_primary[BUSTER_ARRAY_LENGTH(original)];
    TestDescriptor synthetic_rest[BUSTER_ARRAY_LENGTH(original)];
    memcpy(synthetic_primary, original, sizeof(original));
    memcpy(synthetic_rest, original, sizeof(original));
    u64 synthetic_primary_count = test_module_group_apply(synthetic_primary, BUSTER_ARRAY_LENGTH(original), TEST_MODULE_GROUP_PRIMARY, true);
    u64 synthetic_rest_count = test_module_group_apply(synthetic_rest, BUSTER_ARRAY_LENGTH(original), TEST_MODULE_GROUP_REST, true);
    result = result && synthetic_primary_count == 1 && synthetic_rest_count == 1 + (u64)buster_test_descriptor_runs(original[2]) &&
             test_module_group_union_valid(original, synthetic_primary, synthetic_rest, BUSTER_ARRAY_LENGTH(original));
    synthetic_rest[1].deselected = false;
    result = result && !test_module_group_union_valid(original, synthetic_primary, synthetic_rest, BUSTER_ARRAY_LENGTH(original));
    synthetic_rest[1].deselected = true;
    synthetic_rest[0].deselected = true;
    result = result && !test_module_group_union_valid(original, synthetic_primary, synthetic_rest, BUSTER_ARRAY_LENGTH(original));
    synthetic_rest[0].deselected = false;
    synthetic_rest[2].table_audit = false;
    result = result && !test_module_group_union_valid(original, synthetic_primary, synthetic_rest, BUSTER_ARRAY_LENGTH(original));
    synthetic_rest[2].table_audit = true;
    original[2].name = original[0].name;
    result = result && !test_module_group_table_valid(original, BUSTER_ARRAY_LENGTH(original));
    original[2].name = S8("self_test_audit");
    original[1].name = S8("compiler_driver_test");
    result = result && !test_module_group_table_valid(original, BUSTER_ARRAY_LENGTH(original));
    original[1].name = test_module_group_primary_name();
    original[1].table_audit = true;
    result = result && !test_module_group_table_valid(original, BUSTER_ARRAY_LENGTH(original));
    original[1].table_audit = false;
    original[2].name = S8("bad name");
    result = result && !test_module_group_table_valid(original, BUSTER_ARRAY_LENGTH(original));
    original[2].name = S8("self_test_audit");
    original[0].deselected = true;
    memcpy(synthetic_primary, original, sizeof(original));
    memcpy(synthetic_rest, original, sizeof(original));
    test_module_group_apply(synthetic_primary, BUSTER_ARRAY_LENGTH(original), TEST_MODULE_GROUP_PRIMARY, true);
    test_module_group_apply(synthetic_rest, BUSTER_ARRAY_LENGTH(original), TEST_MODULE_GROUP_REST, true);
    result = result && test_module_group_union_valid(original, synthetic_primary, synthetic_rest, BUSTER_ARRAY_LENGTH(original)) &&
             synthetic_primary[0].deselected && synthetic_rest[0].deselected;
    result = result && !test_module_group_apply(synthetic_primary, BUSTER_ARRAY_LENGTH(original), TEST_MODULE_GROUP_INVALID, false) &&
             !test_module_group_apply(synthetic_rest, BUSTER_ARRAY_LENGTH(original), TEST_MODULE_GROUP_REST, false);
    memcpy(synthetic_primary, original, sizeof(original));
    result = result && !test_module_group_apply(synthetic_primary, BUSTER_ARRAY_LENGTH(original), TEST_MODULE_GROUP_INVENTORY, true);

    // Execute the synthetic primary through the original descriptor runner;
    // the selected module keeps its canonical index and assertion totals.
    memcpy(synthetic_primary, original, sizeof(original));
    test_module_group_apply(synthetic_primary, BUSTER_ARRAY_LENGTH(original), TEST_MODULE_GROUP_PRIMARY, true);
    Arena* arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .flags = {.no_pool = true}});
    Arena* output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(1), .flags = {.no_pool = true}});
    BUSTER_CHECK(arena != 0 && output != 0);
    TestParallelArguments arguments = {.base = {.arena = arena, .show = &test_parallel_show}, .output_arena = output};
    u64 timing_record_count = 0;
    BatchTestResult batch = buster_test_run_descriptors(&arguments.base, synthetic_primary, BUSTER_ARRAY_LENGTH(original), true,
                                                      &timing_record_count, 0);
    String8 text = {(char8*)arena_buffer_start(output), arena_buffer_size(output)};
    result = result && timing_record_count == 1 && batch.module_test_count == 1 && batch.succeeded_module_test_count == 1 &&
             batch.unit_test_count == 2 && batch.succeeded_unit_test_count == 2 &&
             string_first_sequence(text, string_format(arena, S8("TEST_MODULE_TIMING index=1 module={S8} "), test_module_group_primary_name())) != BUSTER_STRING_NO_MATCH &&
             string_first_sequence(text, S8("module=self_test_")) == BUSTER_STRING_NO_MATCH;
    result = arena_destroy(arena, 1) && result;
    result = arena_destroy(output, 1) && result;
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult buster_test_temporary_root_failure_body(UnitTestArguments* arguments)
{
    buster_test_temporary_root_failure_body_called = true;
    String8 path = buster_test_temporary_path(arguments->arena, S8("should-not-run"), S8(""));
    BUSTER_UNUSED(path);
    return (UnitTestResult){1, 1};
}

BUSTER_GLOBAL_LOCAL bool buster_test_temporary_root_failure_self_test(UnitTestArguments* arguments)
{
    TestDescriptor descriptor = {
        .name = S8("temporary_root_failure_self_test"),
        .function = &buster_test_temporary_root_failure_body,
        .requires_temporary_root = true,
    };
    buster_test_temporary_root_failure_injected = true;
    buster_test_temporary_root_failure_self_test_active = true;
    buster_test_temporary_root_failure_body_called = false;
    buster_test_temporary_path_call_count = 0;
    buster_test_temporary_root = (String8){0};
    buster_test_temporary_root_failed = false;
    buster_test_temporary_root_owned = false;
    buster_test_temporary_root_ready = false;

    u64 timing_record_count = 0;
    BatchTestResult test = buster_test_run_descriptors(arguments, &descriptor, 1, false, &timing_record_count, 0);
    bool result = !buster_test_temporary_root_failure_body_called && buster_test_temporary_path_call_count == 0 && timing_record_count == 0 &&
                  test.succeeded_unit_test_count == 0 && test.unit_test_count == 1 && test.succeeded_module_test_count == 0 && test.module_test_count == 0 &&
                  test.succeeded_external_test_count == 0 && test.external_test_count == 0;

    buster_test_temporary_root_failure_injected = false;
    buster_test_temporary_root_failure_self_test_active = false;
    buster_test_temporary_root_failure_body_called = false;
    buster_test_temporary_path_call_count = 0;
    buster_test_temporary_root = (String8){0};
    buster_test_temporary_root_failed = false;
    buster_test_temporary_root_owned = false;
    buster_test_temporary_root_ready = false;
    return result;
}

BatchTestResult library_tests(UnitTestArguments* arguments)
{
#if BUSTER_IOS
    buster_ios_launch_trace(S8("suite-entry"));
#endif
    BatchTestResult result = {0};
    if (!arguments || !arguments->arena || !arguments->show)
    {
        result.unit_test_count = 1;
        return result;
    }

    os_test_process_child_run(arguments);
#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
    if (os_get_environment_variable(S8("BUSTER_OS_FATAL_OUTPUT_MODE")).length)
    {
        // The selected payload is the first block of os_tests and must exit.
        // Do not reach it through the descriptor table: that runs compiler
        // prewarm and seven unrelated modules (including sanitizer children).
        // Keep the payload and all its stream semantics in their existing owner.
        os_tests(arguments);
        enum { TEST_FATAL_CHILD_UNEXPECTED_RETURN = 125 };
        os_exit(TEST_FATAL_CHILD_UNEXPECTED_RETURN);
    }
#endif
    compiler_driver_test_wasm_node_child_run();
#if BUSTER_TEST_WATCHDOG_SUPPORTED
    if (os_get_environment_variable(S8("BUSTER_TEST_WATCHDOG_CHILD_MODE")).length)
    {
        test_watchdog_child_run(arguments);
    }
#endif

    // Some test modules intentionally leave a resident lane gang available
    // for later work on their selected context. Fill every compiler-global
    // read-only table before the first module can create those workers.
    compiler_prewarm();
#if BUSTER_IOS
    buster_ios_launch_trace(S8("prewarm-ready"));
#endif
    BUSTER_CHECK(c_test_lex_compact_tables_ready());
    // Keep the root path outside the per-descriptor arena rewind points. A
    // child that exits before requesting a path therefore owns no filesystem
    // root, while a parent run keeps its root alive until teardown.
    buster_test_temporary_arguments = arguments;
    buster_test_temporary_root_arena = arena_create((ArenaCreation){0});
    buster_test_temporary_root = (String8){0};
    buster_test_temporary_root_failed = false;
    buster_test_temporary_root_owned = false;
    buster_test_temporary_root_ready = false;
    buster_test_temporary_root_failure_injected = false;

    if (!buster_test_temporary_root_arena)
    {
        string_print(S8("TEST_TEMPORARY_ROOT_SETUP status=failed reason=arena allocation\n"));
        result.unit_test_count = 1;
        buster_test_temporary_arguments = 0;
        buster_test_temporary_root_arena = 0;
        buster_test_temporary_root_failed = false;
        return result;
    }

    BUSTER_CHECK(buster_test_temporary_root_failure_self_test(arguments));
    BUSTER_VALIDATE(test_debugger_failure_self_test());
    BUSTER_VALIDATE(test_arena_self_test());
    BUSTER_VALIDATE(test_require_self_test());
    BUSTER_CHECK(test_fixture_timing_self_test());
    BUSTER_CHECK(test_fixture_watchdog_self_test());
    BUSTER_CHECK(test_module_selection_self_test());
    BUSTER_CHECK(test_module_group_self_test());
    BUSTER_VALIDATE(test_parallel_gang_report_self_test());

    String8 module_group_name = os_get_environment_variable(S8("BUSTER_TEST_MODULE_GROUP"));
    TestModuleGroup module_group = test_module_group_resolve(module_group_name);
    bool grouped = module_group != TEST_MODULE_GROUP_NONE;
    bool timing_enabled = grouped || (program_state != 0 && program_flag_get(PROGRAM_FLAG_VERBOSE));
    arguments->memory_report = program_state != 0 && (timing_enabled || program_flag_get(PROGRAM_FLAG_CI));
    if (timing_enabled)
    {
        BUSTER_CHECK(test_timing_self_test(arguments));
    }

    // A module selection marks the modules it leaves out in a working copy,
    // so the registration table stays whole for later runs in this process.
    TestDescriptor descriptors[TEST_ID_COUNT];
    memcpy(descriptors, test_descriptors, sizeof(descriptors));
    bool selection_valid = true;
    u64 selected_module_count = 0;
    if (grouped)
    {
        selection_valid = module_group != TEST_MODULE_GROUP_INVALID && !arguments->module_selection.length &&
                          test_module_group_table_valid(test_descriptors, TEST_ID_COUNT);
        selected_module_count = test_module_group_apply(descriptors, TEST_ID_COUNT, module_group, selection_valid);
        if (!selection_valid)
        {
            arguments->show(arguments, S8("test: invalid BUSTER_TEST_MODULE_GROUP='{S8}', incompatible --module, or invalid registration table\n"),
                            module_group_name);
        }
        test_module_group_inventory(arguments, descriptors);
        if (module_group == TEST_MODULE_GROUP_INVENTORY)
        {
            test_native_host_profile(arguments);
        }
    }
    else if (arguments->module_selection.length)
    {
        // `ide test` rejects an invalid list, naming each bad entry, before
        // this point. For any other caller it runs nothing and fails.
        TestModuleSelection selection = test_module_selection_resolve(arguments->module_selection, false);
        selection_valid = !selection.invalid_count;
        for (u64 id = 0; id < TEST_ID_COUNT; id += 1)
        {
            descriptors[id].deselected = !selection_valid || !selection.selected[id];
            // A module named explicitly runs even where another tree owns
            // its table audit, rather than passing with nothing run.
            descriptors[id].table_audit = descriptors[id].table_audit && !selection.selected[id];
            selected_module_count += !descriptors[id].deselected;
        }
        if (!selection_valid)
        {
            arguments->show(arguments, S8("test: invalid module selection: '{S8}'\n"), arguments->module_selection);
        }
    }

    bool watchdog_started = test_watchdog_start(arguments, TEST_ID_COUNT);
#if BUSTER_IOS
    buster_ios_launch_trace(S8("fixtures-ready"));
#endif
    u64 timing_record_count = 0;
    result = buster_test_run_parallel_descriptors(arguments, descriptors, TEST_ID_COUNT, timing_enabled, &timing_record_count);
    bool watchdog_stopped = test_watchdog_stop();
    result.unit_test_count += (u64)!watchdog_started + (u64)!watchdog_stopped;
    // Every descriptor that ran must have reported a timing row. Audits this
    // tree does not own and modules a selection left out report nothing at
    // all rather than a zero row, so they come out of the expected count
    // instead of out of the invariant.
    u64 expected_timing_records = 0;
    for (u64 index = 0; index < TEST_ID_COUNT; index += 1)
    {
        expected_timing_records += buster_test_descriptor_runs(descriptors[index]);
    }
    BUSTER_CHECK(!timing_enabled || buster_test_temporary_root_failed || timing_record_count == expected_timing_records);
    if (grouped || arguments->module_selection.length)
    {
        result.selected_module_count = selected_module_count;
        result.registered_module_count = TEST_ID_COUNT;
        result.unit_test_count += !selection_valid;
    }

    bool temporary_root_succeeded = true;
    if (buster_test_temporary_root.length)
    {
        temporary_root_succeeded = buster_test_temporary_root_delete(arguments);
    }
    temporary_root_succeeded = temporary_root_succeeded && !buster_test_temporary_root_failed;
    if (!temporary_root_succeeded && !buster_test_temporary_root_failed)
    {
        result.unit_test_count += 1;
    }

    bool temporary_root_arena_destroyed = arena_destroy(buster_test_temporary_root_arena, 1);
    if (grouped && !temporary_root_arena_destroyed)
    {
        result.unit_test_count += 1;
    }
    buster_test_temporary_root_arena = 0;
    buster_test_temporary_arguments = 0;
    buster_test_temporary_root_failed = false;

    if (grouped)
    {
        String8 group = module_group == TEST_MODULE_GROUP_PRIMARY ? S8("primary") :
                        module_group == TEST_MODULE_GROUP_REST ? S8("rest") :
                        module_group == TEST_MODULE_GROUP_INVENTORY ? S8("inventory") : S8("invalid");
        String8 status = batch_test_succeeded(result) ? S8("pass") : S8("fail");
        if (module_group == TEST_MODULE_GROUP_INVENTORY && batch_test_succeeded(result))
        {
            status = S8("inventory");
        }
        arguments->show(arguments,
            S8("CI_UNIT_BATCH_V1 group={S8} modules={u64} modules_passed={u64} assertions={u64} passed={u64} failed={u64} external={u64} external_passed={u64} status={S8}\n"),
            group, result.module_test_count, result.succeeded_module_test_count, result.unit_test_count, result.succeeded_unit_test_count,
            result.unit_test_count - result.succeeded_unit_test_count, result.external_test_count, result.succeeded_external_test_count, status);
    }

    return result;
}
#endif
