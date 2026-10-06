// Production Clang PGO/LTO workflow, included by build.c.
//
// The workflow deliberately stays outside ordinary generate/build defaults:
// it creates fresh, named trees, trains instrumented Release compilers on the
// deterministic native throughput corpus, seals each merged profile with a
// source/tool/workload manifest, and only then admits PGO-use builds.
//
// Clang's IR PGO hashes each function's control flow where instrumentation
// runs, and the ThinLTO pre-link pipeline reaches that point with different
// simplification than a non-LTO build (for example, it skips IPSCCP function
// specialization). A profile therefore only matches use builds in its own LTO
// mode. Each ProductionProfileTraining in production_profile_main owns one
// mode's instrumented tree and profile: the ThinLTO one seals the production
// pgo-lto compiler and the non-LTO one seals the pgo comparison.
// production_profile_training_contract puts the mode in the fingerprint,
// production_profile_seal repeats it in the manifest, and CMake rejects a
// PGO-use tree whose BUSTER_LTO differs.
//
// Phase accounting: production_profile_command records each labelled child
// in its fixed BUSTER_PRODUCTION_PROFILE_SLOT_* slot of the
// ProductionProfileLedger; production_profile_ledger_record rewrites
// evidence/phases.tsv and evidence/phases.md (production_profile_ledger_tsv/
// _markdown) after every phase, and production_profile_main fails unless the
// count matches production_profile_phase_expected.
//
// Build lanes (#2790): every tree's ide target is one unity compile plus one
// link, so Ninja parallelism inside a tree is idle. production_profile_main
// therefore schedules ProductionProfileTask chains (instrumented build ->
// train/merge/seal -> matching PGO-use build, or one plain build) onto
// production_profile_build_lanes lanes of the persistent gang, splitting the
// --jobs worker budget between them. production_profile_task_lane admits
// tasks longest first; the first failure cancels admission and terminates
// in-flight children through ProcessGroupControlState. Correctness tests and
// timed comparisons run afterwards, serially and uncontended.
#include <signal.h>

#define BUSTER_PRODUCTION_PROFILE_TIMEOUT_SECONDS_DEFAULT 3600ull
#define BUSTER_PRODUCTION_PROFILE_TRAINING_PAIRS 1ull
#define BUSTER_PRODUCTION_PROFILE_TRAINING_WARMUPS 1ull
#define BUSTER_PRODUCTION_PROFILE_MANIFEST_VERSION "BUSTER_PGO_PROFILE_V2"
#define BUSTER_PRODUCTION_PROFILE_CONTRACT_VERSION "BUSTER_PGO_TRAINING_V2"
#define BUSTER_PRODUCTION_PROFILE_LEDGER_VERSION "BUSTER_PGO_PHASES_V1"
// Three toolchain identity probes, generate+build for seven trees, train+merge
// for two profiles, section inspection, test_all, and run+compare for four
// comparisons. production_profile_phase_expected owns the exact count.
#define BUSTER_PRODUCTION_PROFILE_PHASE_CAPACITY 32ull
// Fixed ledger slots, in the canonical (formerly serial) phase order, so the
// evidence is identical however lanes interleave. A tree's configure slot is
// followed by its build slot, and a training's train slot by its merge slot.
#define BUSTER_PRODUCTION_PROFILE_SLOT_TOOLCHAIN 0ull
#define BUSTER_PRODUCTION_PROFILE_SLOT_RELEASE 3ull
#define BUSTER_PRODUCTION_PROFILE_SLOT_G0 5ull
#define BUSTER_PRODUCTION_PROFILE_SLOT_LTO 7ull
#define BUSTER_PRODUCTION_PROFILE_SLOT_INSTRUMENTED 9ull
#define BUSTER_PRODUCTION_PROFILE_SLOT_INSTRUMENTED_LTO 11ull
#define BUSTER_PRODUCTION_PROFILE_SLOT_TRAIN_NO_LTO 13ull
#define BUSTER_PRODUCTION_PROFILE_SLOT_TRAIN_LTO 15ull
#define BUSTER_PRODUCTION_PROFILE_SLOT_PGO 17ull
#define BUSTER_PRODUCTION_PROFILE_SLOT_PGO_LTO 19ull
#define BUSTER_PRODUCTION_PROFILE_SLOT_INSPECT 21ull
#define BUSTER_PRODUCTION_PROFILE_SLOT_TEST 22ull
#define BUSTER_PRODUCTION_PROFILE_SLOT_BENCHMARK 23ull
// Measured on the #2790 ledgers: one tree build peaks at 2.4-2.9 GiB RSS in a
// single compiler or linker process. Admit one build lane per 4 GiB.
#define BUSTER_PRODUCTION_PROFILE_LANE_MEMORY_MIB 4096ull
#define BUSTER_PRODUCTION_PROFILE_TASK_STEP_CAPACITY 3ull
// The two instrumented -> train -> PGO-use chains plus release, g0 and lto.
#define BUSTER_PRODUCTION_PROFILE_TASK_COUNT 5ull

typedef struct ProductionProfileOptions ProductionProfileOptions;
struct ProductionProfileOptions
{
    String8 output;
    String8 clang;
    String8 llvm_profdata;
    String8 llvm_readobj;
    String8 benchmark_profile;
    u64 jobs;
    u64 build_lanes;
    u64 pairs;
    u64 warmups;
    u64 timeout_seconds;
    bool clean;
    bool benchmark;
};

typedef struct ProductionProfileCommandResult ProductionProfileCommandResult;
struct ProductionProfileCommandResult
{
    ProcessWaitResult wait;
    String8 output;
    String8 error;
    bool success;
};

// Phase accounting (#2790). Every evidence-labelled child process is one
// phase: its monotonic start/duration relative to the run origin, outcome and
// wait4/job resource usage. production_profile_ledger_record rewrites the TSV
// and Markdown ledgers after each phase, so a failed or cancelled run still
// retains every phase that finished before it stopped.
typedef struct ProductionProfilePhase ProductionProfilePhase;
struct ProductionProfilePhase
{
    String8 label;
    String8 kind;
    String8 variant;
    u64 start_us;
    u64 duration_us;
    ProcessResourceUsage resources;
    bool success;
    bool timed_out;
    // Terminated because another lane failed first.
    bool cancelled;
    bool recorded;
};

// Ledger identity of one child process.
typedef struct ProductionProfilePhaseSpec ProductionProfilePhaseSpec;
struct ProductionProfilePhaseSpec
{
    String8 label;
    String8 kind;
    String8 variant;
    u64 slot;
};

typedef struct ProductionProfileLedger ProductionProfileLedger;
struct ProductionProfileLedger
{
    String8 tsv_path;
    String8 markdown_path;
    String8 identity;
    u64 origin_us;
    u64 expected;
    u64 count;
    bool complete;
    // Serializes recording and rewriting when build lanes run concurrently.
    OsMutexHandle* mutex;
    // Owns recorded strings: a phase's label may come from a lane arena that
    // is destroyed when its lane finishes.
    Arena* arena;
    ProductionProfilePhase phases[BUSTER_PRODUCTION_PROFILE_PHASE_CAPACITY];
};

typedef struct ProductionProfileContext ProductionProfileContext;
struct ProductionProfileContext
{
    Arena* arena;
    ProductionProfileOptions options;
    String8 driver;
    String8 repository;
    String8 output;
    String8 evidence;
    String8 clang;
    String8 llvm_profdata;
    String8 llvm_readobj;
    String8 revision;
    String8 tree;
    String8 clang_sha;
    String8 profdata_sha;
    ProductionProfileLedger* ledger;
    // Admitted lanes and the Ninja jobs each tree build receives.
    u64 build_lanes;
    u64 build_jobs;
    // Held around spawning so a concurrent child cannot inherit another
    // child's pipe ends; zero when only one lane runs.
    OsMutexHandle* spawn_mutex;
    // Held around a training run: bench_throughput rebuilds the shared
    // build/throughput-tools/throughput binary on every invocation and
    // --cpu auto pins every run to the same first allowed CPU.
    OsMutexHandle* training_mutex;
    ProcessControlAtomic cancellation_signal;
    ProcessControlAtomic cancellation_escalated;
};

// One instrumented tree, its training run and the profile it seals. A use
// variant may only consume the training whose lto matches its own.
typedef struct ProductionProfileTraining ProductionProfileTraining;
struct ProductionProfileTraining
{
    String8 instrumented;
    String8 directory;
    String8 label;
    String8 fingerprint;
    String8 profile;
    String8 manifest;
    u64 slot;
    bool lto;
};

typedef struct ProductionProfileVariant ProductionProfileVariant;
struct ProductionProfileVariant
{
    String8 name;
    ProductionProfileTraining* training;
    u64 slot;
    bool debug_info;
    bool lto;
    bool generate;
    bool use;
};

typedef enum ProductionProfileStepKind
{
    PRODUCTION_PROFILE_STEP_BUILD,
    PRODUCTION_PROFILE_STEP_TRAIN,
    // A plain recorded command; the self-test drives the scheduler with it.
    PRODUCTION_PROFILE_STEP_COMMAND,
} ProductionProfileStepKind;

typedef struct ProductionProfileStep ProductionProfileStep;
struct ProductionProfileStep
{
    ProductionProfileStepKind kind;
    ProductionProfileVariant variant;
    ProductionProfileTraining* training;
    SliceString8 command;
    ProductionProfilePhaseSpec phase;
};

// Steps of one task run in order on one lane; tasks are independent.
typedef struct ProductionProfileTask ProductionProfileTask;
struct ProductionProfileTask
{
    ProductionProfileStep steps[BUSTER_PRODUCTION_PROFILE_TASK_STEP_CAPACITY];
    u64 step_count;
};

typedef struct ProductionProfileWork ProductionProfileWork;
struct ProductionProfileWork
{
    ProductionProfileContext* context;
    ProductionProfileTask* tasks;
    u64 task_count;
    AtomicU64 next;
    AtomicU64 failed;
};

BUSTER_GLOBAL_LOCAL bool production_profile_u64(String8 text, u64* value)
{
    bool result = text.length != 0;
    u64 parsed = 0;
    for (u64 index = 0; result && index < text.length; index += 1)
    {
        u8 digit = (u8)text.pointer[index];
        result = digit >= '0' && digit <= '9';
        if (result)
        {
            u64 value_digit = (u64)(digit - '0');
            result = parsed <= (UINT64_MAX - value_digit) / 10;
            if (result)
            {
                parsed = parsed * 10 + value_digit;
            }
        }
    }
    if (result)
    {
        *value = parsed;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 production_profile_trim(String8 text)
{
    u64 first = 0;
    u64 one_past_last = text.length;
    while (first < one_past_last)
    {
        char8 byte = text.pointer[first];
        if (byte != ' ' && byte != '\t' && byte != '\r' && byte != '\n')
        {
            break;
        }
        first += 1;
    }
    while (one_past_last > first)
    {
        char8 byte = text.pointer[one_past_last - 1];
        if (byte != ' ' && byte != '\t' && byte != '\r' && byte != '\n')
        {
            break;
        }
        one_past_last -= 1;
    }
    return (String8){.pointer = text.pointer + first, .length = one_past_last - first};
}

BUSTER_GLOBAL_LOCAL bool production_profile_contains(String8 text, String8 needle)
{
    return needle.length != 0 && string_first_sequence(text, needle) != BUSTER_STRING_NO_MATCH;
}

BUSTER_GLOBAL_LOCAL String8 production_profile_sha256_bytes(Arena* arena, void const* bytes, u64 size)
{
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, bytes, size);
    char8* digits = arena_allocate(arena, char8, SHA256_HEX_CAPACITY);
    sha256_finish_hex(&hash, digits);
    return (String8){.pointer = digits, .length = SHA256_HEX_CAPACITY - 1};
}

BUSTER_GLOBAL_LOCAL String8 production_profile_sha256_text(Arena* arena, String8 text)
{
    return production_profile_sha256_bytes(arena, text.pointer, text.length);
}

BUSTER_GLOBAL_LOCAL String8 production_profile_sha256_file(Arena* arena, String8 path)
{
    ByteSlice bytes = file_read(arena, path, (FileReadOptions){0});
    String8 result = {0};
    if (bytes.pointer && bytes.length)
    {
        result = production_profile_sha256_bytes(arena, bytes.pointer, bytes.length);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool production_profile_write(String8 path, String8 text)
{
    return file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(text));
}

BUSTER_GLOBAL_LOCAL String8 production_profile_argv_text(Arena* arena, SliceString8 arguments)
{
    String8List lines = {0};
    for (u64 index = 0; index < arguments.length; index += 1)
    {
        string8_list_push(arena, &lines, string_format(arena, S8("{u64}:{S8}\n"), arguments.pointer[index].length, arguments.pointer[index]));
    }
    return string_join_arena(arena, string8_list_to_slice(arena, lines), false);
}

BUSTER_GLOBAL_LOCAL bool production_profile_environment_key_equal(String8 left, String8 right)
{
    bool result = left.length == right.length;
    for (u64 index = 0; result && index < left.length; index += 1)
    {
        char8 left_byte = left.pointer[index];
        char8 right_byte = right.pointer[index];
#if BUSTER_WINDOWS
        if (left_byte >= 'A' && left_byte <= 'Z')
        {
            left_byte = (char8)(left_byte + ('a' - 'A'));
        }
        if (right_byte >= 'A' && right_byte <= 'Z')
        {
            right_byte = (char8)(right_byte + ('a' - 'A'));
        }
#endif
        result = left_byte == right_byte;
    }
    return result;
}

// os_process_spawn intentionally treats an explicit key/value list as the
// complete child environment. Materialize the captured parent environment,
// remove entries replaced by the caller, and append the caller's values.
BUSTER_GLOBAL_LOCAL bool production_profile_environment_merge(
    Arena* arena,
    SliceString8 inherited_keys, SliceString8 inherited_values,
    SliceString8 override_keys, SliceString8 override_values,
    SliceString8* result_keys, SliceString8* result_values)
{
    bool result = inherited_keys.length == inherited_values.length &&
                  override_keys.length == override_values.length &&
                  (!inherited_keys.length || (inherited_keys.pointer && inherited_values.pointer)) &&
                  (!override_keys.length || (override_keys.pointer && override_values.pointer)) &&
                  inherited_keys.length <= UINT64_MAX - override_keys.length;
    if (!result)
    {
        return false;
    }

    u64 capacity = inherited_keys.length + override_keys.length;
    String8* keys = capacity ? arena_allocate(arena, String8, capacity) : 0;
    String8* values = capacity ? arena_allocate(arena, String8, capacity) : 0;
    u64 count = 0;

    for (u64 inherited_index = 0; inherited_index < inherited_keys.length; inherited_index += 1)
    {
        bool replaced = false;
        for (u64 override_index = 0; override_index < override_keys.length; override_index += 1)
        {
            if (production_profile_environment_key_equal(
                    inherited_keys.pointer[inherited_index], override_keys.pointer[override_index]))
            {
                replaced = true;
                break;
            }
        }
        if (!replaced)
        {
            keys[count] = inherited_keys.pointer[inherited_index];
            values[count] = inherited_values.pointer[inherited_index];
            count += 1;
        }
    }

    for (u64 override_index = 0; override_index < override_keys.length; override_index += 1)
    {
        keys[count] = override_keys.pointer[override_index];
        values[count] = override_values.pointer[override_index];
        count += 1;
    }

    *result_keys = (SliceString8){.pointer = keys, .length = count};
    *result_values = (SliceString8){.pointer = values, .length = count};
    return true;
}

BUSTER_GLOBAL_LOCAL u64 production_profile_phase_expected(bool benchmark)
{
    u64 toolchain_probes = 3;
    u64 trees = 7;
    u64 trainings = 2;
    u64 validation = 2;
    u64 comparisons = benchmark ? 4 : 0;
    return toolchain_probes + trees * 2 + trainings * 2 + validation + comparisons * 2;
}

// Whole seconds and one truncated decimal, so ledgers need no floating point.
BUSTER_GLOBAL_LOCAL String8 production_profile_seconds_text(Arena* arena, u64 microseconds)
{
    return string_format(arena, S8("{u64}.{u64}"), microseconds / 1000000, (microseconds % 1000000) / 100000);
}

BUSTER_GLOBAL_LOCAL String8 production_profile_phase_outcome(ProductionProfilePhase* phase)
{
    return phase->timed_out ? S8("timed_out") : phase->cancelled ? S8("cancelled") : phase->success ? S8("passed") : S8("failed");
}

BUSTER_GLOBAL_LOCAL u64 production_profile_ledger_elapsed(ProductionProfileLedger* ledger)
{
    u64 result = 0;
    for (u64 slot = 0; slot < BUSTER_PRODUCTION_PROFILE_PHASE_CAPACITY; slot += 1)
    {
        ProductionProfilePhase* phase = ledger->phases + slot;
        if (phase->recorded)
        {
            result = BUSTER_MAX(result, phase->start_us + phase->duration_us);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 production_profile_ledger_tsv(Arena* arena, ProductionProfileLedger* ledger)
{
    String8List lines = {0};
    string8_list_push(arena, &lines, string_format(arena, S8(BUSTER_PRODUCTION_PROFILE_LEDGER_VERSION "\tcomplete={u32}\tphases={u64}\texpected={u64}\n"),
        (u32)ledger->complete, ledger->count, ledger->expected));
    string8_list_push(arena, &lines, S8("index\tlabel\tkind\tvariant\tstart_us\tduration_us\toutcome\tuser_cpu_us\tsystem_cpu_us\tpeak_memory_bytes\n"));
    for (u64 slot = 0; slot < BUSTER_PRODUCTION_PROFILE_PHASE_CAPACITY; slot += 1)
    {
        ProductionProfilePhase* phase = ledger->phases + slot;
        if (!phase->recorded)
        {
            continue;
        }
        String8 variant = S8("-");
        String8 user_cpu = S8("-");
        String8 system_cpu = S8("-");
        String8 peak_memory = S8("-");
        if (phase->variant.length)
        {
            variant = phase->variant;
        }
        if (phase->resources.cpu_status == PROCESS_RESOURCE_OBSERVED)
        {
            user_cpu = string_format(arena, S8("{u64}"), phase->resources.user_cpu_us);
            system_cpu = string_format(arena, S8("{u64}"), phase->resources.system_cpu_us);
        }
        if (phase->resources.memory_status == PROCESS_RESOURCE_OBSERVED)
        {
            peak_memory = string_format(arena, S8("{u64}"), phase->resources.peak_memory_bytes);
        }
        string8_list_push(arena, &lines, string_format(arena, S8("{u64}\t{S8}\t{S8}\t{S8}\t{u64}\t{u64}\t{S8}\t{S8}\t{S8}\t{S8}\n"),
            slot + 1, phase->label, phase->kind, variant, phase->start_us, phase->duration_us,
            production_profile_phase_outcome(phase), user_cpu, system_cpu, peak_memory));
    }
    return string_join_arena(arena, string8_list_to_slice(arena, lines), false);
}

// The Actions step summary: identity, per-kind totals in slot order, then
// every phase. Shares are of the elapsed span covered by recorded phases, so
// kinds that ran on concurrent lanes can sum past 100%.
BUSTER_GLOBAL_LOCAL String8 production_profile_ledger_markdown(Arena* arena, ProductionProfileLedger* ledger)
{
    u64 elapsed = production_profile_ledger_elapsed(ledger);
    String8List lines = {0};
    string8_list_push(arena, &lines, string_format(arena, S8(
        "## Production PGO/LTO phase accounting\n\n"
        "Result: **{S8}** ({u64} of {u64} phases recorded, {S8} s elapsed).\n\n"
        "```text\n{S8}```\n\n"
        "### By kind\n\n"
        "| Kind | Phases | Phase s | Share | CPU s |\n"
        "| --- | ---: | ---: | ---: | ---: |\n"),
        ledger->complete ? S8("complete") : S8("incomplete"), ledger->count, ledger->expected,
        production_profile_seconds_text(arena, elapsed), ledger->identity));

    String8 kinds[BUSTER_PRODUCTION_PROFILE_PHASE_CAPACITY];
    u64 kind_count = 0;
    for (u64 slot = 0; slot < BUSTER_PRODUCTION_PROFILE_PHASE_CAPACITY; slot += 1)
    {
        if (!ledger->phases[slot].recorded)
        {
            continue;
        }
        String8 kind = ledger->phases[slot].kind;
        bool seen = false;
        for (u64 kind_index = 0; kind_index < kind_count; kind_index += 1)
        {
            seen |= string_equal(kinds[kind_index], kind);
        }
        if (!seen)
        {
            kinds[kind_count] = kind;
            kind_count += 1;
        }
    }

    for (u64 kind_index = 0; kind_index < kind_count; kind_index += 1)
    {
        u64 phases = 0;
        u64 wall = 0;
        u64 cpu = 0;
        bool cpu_complete = true;
        for (u64 slot = 0; slot < BUSTER_PRODUCTION_PROFILE_PHASE_CAPACITY; slot += 1)
        {
            ProductionProfilePhase* phase = ledger->phases + slot;
            if (phase->recorded && string_equal(phase->kind, kinds[kind_index]))
            {
                phases += 1;
                wall += phase->duration_us;
                cpu_complete &= phase->resources.cpu_status == PROCESS_RESOURCE_OBSERVED;
                cpu += phase->resources.user_cpu_us + phase->resources.system_cpu_us;
            }
        }
        u64 share_tenths = elapsed ? (wall * 1000 + elapsed / 2) / elapsed : 0;
        String8 wall_text = production_profile_seconds_text(arena, wall);
        String8 cpu_text = S8("-");
        if (cpu_complete)
        {
            cpu_text = production_profile_seconds_text(arena, cpu);
        }
        string8_list_push(arena, &lines, string_format(arena, S8("| {S8} | {u64} | {S8} | {u64}.{u64}% | {S8} |\n"),
            kinds[kind_index], phases, wall_text, share_tenths / 10, share_tenths % 10, cpu_text));
    }

    string8_list_push(arena, &lines, S8(
        "\n### Phases\n\n"
        "| # | Phase | Kind | Variant | Start s | Wall s | Outcome | CPU s | Peak RSS MiB |\n"
        "| ---: | --- | --- | --- | ---: | ---: | --- | ---: | ---: |\n"));
    for (u64 slot = 0; slot < BUSTER_PRODUCTION_PROFILE_PHASE_CAPACITY; slot += 1)
    {
        ProductionProfilePhase* phase = ledger->phases + slot;
        if (!phase->recorded)
        {
            continue;
        }
        String8 variant = S8("-");
        String8 cpu_text = S8("-");
        String8 memory_text = S8("-");
        if (phase->variant.length)
        {
            variant = phase->variant;
        }
        if (phase->resources.cpu_status == PROCESS_RESOURCE_OBSERVED)
        {
            cpu_text = production_profile_seconds_text(arena, phase->resources.user_cpu_us + phase->resources.system_cpu_us);
        }
        if (phase->resources.memory_status == PROCESS_RESOURCE_OBSERVED)
        {
            memory_text = string_format(arena, S8("{u64}"), phase->resources.peak_memory_bytes >> 20);
        }
        String8 start_text = production_profile_seconds_text(arena, phase->start_us);
        String8 wall_text = production_profile_seconds_text(arena, phase->duration_us);
        string8_list_push(arena, &lines, string_format(arena, S8("| {u64} | `{S8}` | {S8} | {S8} | {S8} | {S8} | {S8} | {S8} | {S8} |\n"),
            slot + 1, phase->label, phase->kind, variant, start_text, wall_text,
            production_profile_phase_outcome(phase), cpu_text, memory_text));
    }
    return string_join_arena(arena, string8_list_to_slice(arena, lines), false);
}

BUSTER_GLOBAL_LOCAL bool production_profile_ledger_flush(Arena* arena, ProductionProfileLedger* ledger)
{
    bool result = production_profile_write(ledger->tsv_path, production_profile_ledger_tsv(arena, ledger)) &&
                  production_profile_write(ledger->markdown_path, production_profile_ledger_markdown(arena, ledger));
    if (!result)
    {
        fprintf(stderr, "error: could not write the production-profile phase ledger\n");
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool production_profile_ledger_record(Arena* arena, ProductionProfileLedger* ledger, u64 slot, ProductionProfilePhase phase)
{
    if (ledger->mutex)
    {
        os_mutex_lock(ledger->mutex);
    }
    bool result = slot < BUSTER_PRODUCTION_PROFILE_PHASE_CAPACITY && !ledger->phases[slot].recorded;
    if (!result)
    {
        fprintf(stderr, "error: production-profile phase slot is out of range or already recorded\n");
    }
    else
    {
        phase.recorded = true;
        if (ledger->arena)
        {
            phase.label = string_duplicate_arena(ledger->arena, phase.label, false);
            phase.kind = string_duplicate_arena(ledger->arena, phase.kind, false);
            phase.variant = string_duplicate_arena(ledger->arena, phase.variant, false);
        }
        ledger->phases[slot] = phase;
        ledger->count += 1;
        string_print(S8("PRODUCTION_PROFILE_PHASE {u64}/{u64} end slot={u64} label={S8} outcome={S8} wall_s={S8} elapsed_s={S8}\n"),
            ledger->count, ledger->expected, slot + 1, phase.label, production_profile_phase_outcome(&phase),
            production_profile_seconds_text(arena, phase.duration_us),
            production_profile_seconds_text(arena, phase.start_us + phase.duration_us));
        result = !ledger->tsv_path.length || production_profile_ledger_flush(arena, ledger);
    }
    if (ledger->mutex)
    {
        os_mutex_unlock(ledger->mutex);
    }
    return result;
}

// Stops admitting tasks and asks in-flight children to terminate. The spawn
// mutex orders this against a lane that is between its admission check and
// attaching the control state to a fresh child.
BUSTER_GLOBAL_LOCAL void production_profile_cancel(ProductionProfileContext* context)
{
    if (context->spawn_mutex)
    {
        os_mutex_lock(context->spawn_mutex);
    }
    process_control_atomic_set_if_zero(&context->cancellation_signal, SIGTERM);
    if (context->spawn_mutex)
    {
        os_mutex_unlock(context->spawn_mutex);
    }
}

// Lanes never exceed the --jobs worker budget, the memory admission, the
// task count, or one in single-threaded drivers (the TCC bootstrap).
BUSTER_GLOBAL_LOCAL u64 production_profile_build_lanes(u64 requested, u64 jobs, u64 memory_mib, u64 task_count)
{
    u64 result = BUSTER_MIN(BUSTER_MIN(requested, jobs), BUSTER_MIN(memory_mib / BUSTER_PRODUCTION_PROFILE_LANE_MEMORY_MIB, task_count));
#if BUSTER_SINGLE_THREADED
    result = 1;
#endif
    return BUSTER_MAX(result, 1);
}

// With a context ledger, records this command in phase.slot; its outcome
// includes evidence-write failures. arena belongs to the calling lane.
BUSTER_GLOBAL_LOCAL ProductionProfileCommandResult production_profile_command(
    ProductionProfileContext* context, Arena* arena, SliceString8 arguments,
    SliceString8 environment_keys, SliceString8 environment_values,
    ProductionProfilePhaseSpec phase_spec, u64 timeout_seconds, bool capture)
{
    ProductionProfileCommandResult result = {0};
    ProductionProfileLedger* ledger = context->ledger;
    String8 evidence = context->evidence;
    String8 label = phase_spec.label;
    if (evidence.length)
    {
        String8 argv_path = path_join(arena, evidence, string_format(arena, S8("{S8}.argv.txt"), label));
        if (!production_profile_write(argv_path, production_profile_argv_text(arena, arguments)))
        {
            fprintf(stderr, "error: could not write production-profile command evidence\n");
            return result;
        }
    }

    bool use_process_environment = !environment_keys.length && !environment_values.length;
    SliceString8 child_environment_keys = environment_keys;
    SliceString8 child_environment_values = environment_values;
    if (!use_process_environment &&
        !production_profile_environment_merge(
            arena,
            program_state->input.environment_keys, program_state->input.environment_values,
            environment_keys, environment_values,
            &child_environment_keys, &child_environment_values))
    {
        fprintf(stderr, "error: invalid production-profile environment override\n");
        return result;
    }

    if (ledger)
    {
        string_print(S8("PRODUCTION_PROFILE_PHASE start slot={u64} label={S8}\n"), phase_spec.slot + 1, label);
    }
    command_print(arguments);
    u64 start_us = os_now_microseconds();
    u64 capture_mask = capture ? (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR) : 0;
    ProcessGroupControlState control = {
        .cancellation_signal = &context->cancellation_signal,
        .cancellation_escalated = &context->cancellation_escalated,
        .admission_mutex = context->spawn_mutex,
    };
    ProcessSpawnResult spawn = {0};
    if (context->spawn_mutex)
    {
        os_mutex_lock(context->spawn_mutex);
    }
    bool admitted = !process_control_atomic_load(&context->cancellation_signal);
    if (admitted)
    {
        spawn = os_process_spawn(arguments, child_environment_keys, child_environment_values,
            (ProcessSpawnOptions){.capture = capture_mask, .use_process_environment = use_process_environment,
                                  .search_path = 1, .new_process_group = 1, .observe_resources = ledger != 0});
        if (spawn.handle)
        {
            spawn.process_group_control = &control;
        }
    }
    if (context->spawn_mutex)
    {
        os_mutex_unlock(context->spawn_mutex);
    }
    if (spawn.handle)
    {
        result.wait = os_process_wait_deadline(arena, spawn, timeout_seconds ? timeout_seconds * 1000000ull : 0);
    }
    else
    {
        result.wait.result = admitted ? PROCESS_RESULT_NOT_EXISTENT : PROCESS_RESULT_FAILED;
    }

    result.output = (String8){
        .pointer = (char8*)result.wait.streams[STANDARD_STREAM_OUTPUT].pointer,
        .length = result.wait.streams[STANDARD_STREAM_OUTPUT].length,
    };
    result.error = (String8){
        .pointer = (char8*)result.wait.streams[STANDARD_STREAM_ERROR].pointer,
        .length = result.wait.streams[STANDARD_STREAM_ERROR].length,
    };
    result.success = result.wait.result == PROCESS_RESULT_SUCCESS && result.wait.platform_status == 0 && !result.wait.timed_out;
    u64 duration_us = os_now_microseconds() - start_us;
    ProcessResourceUsage resources = result.wait.resources;
    // Another lane's failure, not this child's, ended it.
    bool cancelled = !result.success && process_control_atomic_load(&context->cancellation_signal) != 0;

    if (evidence.length)
    {
        String8 status_path = path_join(arena, evidence, string_format(arena, S8("{S8}.status.txt"), label));
        String8 status = string_format(arena, S8("result={u32}\nplatform_status={u32}\ntimed_out={u32}\nsuccess={u32}\n"
            "spawn_failure={u32}\nspawn_error={u32}\nduration_us={u64}\n"
            "cpu_status={u32}\nuser_cpu_us={u64}\nsystem_cpu_us={u64}\nmemory_status={u32}\npeak_memory_bytes={u64}\n"),
            (u32)result.wait.result, result.wait.platform_status, (u32)result.wait.timed_out, (u32)result.success,
            (u32)spawn.failure, spawn.error.v, duration_us,
            (u32)resources.cpu_status, resources.user_cpu_us, resources.system_cpu_us,
            (u32)resources.memory_status, resources.peak_memory_bytes);
        bool wrote = production_profile_write(status_path, status);
        if (capture)
        {
            wrote &= production_profile_write(path_join(arena, evidence, string_format(arena, S8("{S8}.stdout.log"), label)), result.output);
            wrote &= production_profile_write(path_join(arena, evidence, string_format(arena, S8("{S8}.stderr.log"), label)), result.error);
        }
        if (!wrote)
        {
            fprintf(stderr, "error: could not write production-profile process evidence\n");
            result.success = false;
        }
    }

    if (ledger && admitted)
    {
        ProductionProfilePhase phase = {
            .label = label,
            .kind = phase_spec.kind,
            .variant = phase_spec.variant,
            .start_us = start_us >= ledger->origin_us ? start_us - ledger->origin_us : 0,
            .duration_us = duration_us,
            .resources = resources,
            .success = result.success,
            .timed_out = result.wait.timed_out != 0,
            .cancelled = cancelled,
        };
        result.success &= production_profile_ledger_record(arena, ledger, phase_spec.slot, phase);
    }

    if (!result.success && capture && !cancelled)
    {
        if (result.output.length)
        {
            os_file_write(os_get_standard_stream(STANDARD_STREAM_OUTPUT), BUSTER_SLICE_TO_BYTE_SLICE(result.output));
        }
        if (result.error.length)
        {
            os_file_write(os_get_standard_stream(STANDARD_STREAM_ERROR), BUSTER_SLICE_TO_BYTE_SLICE(result.error));
        }
    }
    return result;
}

// Pre-evidence queries: unrecorded, with no evidence files.
BUSTER_GLOBAL_LOCAL ProductionProfileCommandResult production_profile_capture(ProductionProfileContext* context, SliceString8 arguments)
{
    BUSTER_CHECK(!context->ledger && !context->evidence.length);
    return production_profile_command(context, context->arena, arguments, (SliceString8){0}, (SliceString8){0},
        (ProductionProfilePhaseSpec){0}, 120, true);
}

BUSTER_GLOBAL_LOCAL bool production_profile_path_is_child(String8 parent, String8 child)
{
    bool result = parent.length && child.length > parent.length && !memcmp(parent.pointer, child.pointer, (size_t)parent.length);
    if (result)
    {
        char8 separator = child.pointer[parent.length];
        result = separator == '/' || separator == '\\';
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool production_profile_path_components_safe(String8 path)
{
    u64 first = 0;
    for (u64 one_past_last = 0; one_past_last <= path.length; one_past_last += 1)
    {
        bool separator = one_past_last == path.length || path.pointer[one_past_last] == '/' || path.pointer[one_past_last] == '\\';
        if (!separator)
        {
            continue;
        }
        String8 component = {.pointer = path.pointer + first, .length = one_past_last - first};
        if (string_equal(component, S8(".")) || string_equal(component, S8("..")))
        {
            return false;
        }
        first = one_past_last + 1;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL String8 production_profile_resolve_tool(Arena* arena, String8 requested, String8 description)
{
    String8 resolved = executable_resolve_in_path(arena, requested);
    if (!resolved.length)
    {
        fprintf(stderr, "error: could not resolve %.*s executable '%.*s'\n",
            string8_printf_length(description, UINT64_MAX), description.pointer,
            string8_printf_length(requested, UINT64_MAX), requested.pointer);
    }
    return resolved;
}

BUSTER_GLOBAL_LOCAL bool production_profile_debug_sections(String8 sections)
{
    String8 forbidden[] = {
        S8(".debug_"),
        S8(".zdebug_"),
        S8("__debug_"),
        S8("__DWARF"),
        S8(".gnu_debuglink"),
        S8(".gnu_debugaltlink"),
    };
    bool result = false;
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(forbidden); index += 1)
    {
        if (production_profile_contains(sections, forbidden[index]))
        {
            result = true;
            break;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void production_profile_usage(void)
{
    string_print(S8(
        "usage: production_profile [options]\n"
        "  --output PATH                 fresh evidence/build root (default build/production-profile)\n"
        "  --clang PATH                  Clang executable (default clang)\n"
        "  --llvm-profdata PATH          profile merger (default llvm-profdata)\n"
        "  --llvm-readobj PATH           section inspector (default llvm-readobj)\n"
        "  --benchmark-profile PROFILE   smoke, ci or full (default smoke)\n"
        "  --jobs N                      worker budget for compiler builds and tests (default 2)\n"
        "  --build-lanes N               concurrent tree builds sharing --jobs (default 1)\n"
        "  --pairs N                     benchmark pairs per comparison (default 8)\n"
        "  --warmups N                   benchmark warmups (default 1)\n"
        "  --timeout N                   child deadline in seconds (default 3600)\n"
        "  --no-benchmark                build/train/validate without the comparison matrix\n"
        "  --clean                       replace an existing output inside this checkout\n"));
}

BUSTER_GLOBAL_LOCAL bool production_profile_options(Arena* arena, SliceString8 arguments, ProductionProfileOptions* options)
{
    *options = (ProductionProfileOptions){
        .output = S8("build/production-profile"),
        .clang = S8("clang"),
        .llvm_profdata = S8("llvm-profdata"),
        .llvm_readobj = S8("llvm-readobj"),
        .benchmark_profile = S8("smoke"),
        .jobs = 2,
        .build_lanes = 1,
        .pairs = 8,
        .warmups = 1,
        .timeout_seconds = BUSTER_PRODUCTION_PROFILE_TIMEOUT_SECONDS_DEFAULT,
        .benchmark = true,
    };

    bool result = true;
    for (u64 index = 0; result && index < arguments.length; index += 1)
    {
        String8 argument = arguments.pointer[index];
        String8* string_value = 0;
        u64* integer_value = 0;
        if (string_equal(argument, S8("--output")))
        {
            string_value = &options->output;
        }
        else if (string_equal(argument, S8("--clang")))
        {
            string_value = &options->clang;
        }
        else if (string_equal(argument, S8("--llvm-profdata")))
        {
            string_value = &options->llvm_profdata;
        }
        else if (string_equal(argument, S8("--llvm-readobj")))
        {
            string_value = &options->llvm_readobj;
        }
        else if (string_equal(argument, S8("--benchmark-profile")))
        {
            string_value = &options->benchmark_profile;
        }
        else if (string_equal(argument, S8("--jobs")))
        {
            integer_value = &options->jobs;
        }
        else if (string_equal(argument, S8("--build-lanes")))
        {
            integer_value = &options->build_lanes;
        }
        else if (string_equal(argument, S8("--pairs")))
        {
            integer_value = &options->pairs;
        }
        else if (string_equal(argument, S8("--warmups")))
        {
            integer_value = &options->warmups;
        }
        else if (string_equal(argument, S8("--timeout")))
        {
            integer_value = &options->timeout_seconds;
        }
        else if (string_equal(argument, S8("--clean")))
        {
            options->clean = true;
        }
        else if (string_equal(argument, S8("--no-benchmark")))
        {
            options->benchmark = false;
        }
        else if (string_equal(argument, S8("--help")) || string_equal(argument, S8("-h")))
        {
            production_profile_usage();
            return false;
        }
        else
        {
            fprintf(stderr, "error: unknown production_profile argument '%.*s'\n",
                string8_printf_length(argument, UINT64_MAX), argument.pointer);
            result = false;
        }

        if (result && (string_value || integer_value))
        {
            index += 1;
            if (index == arguments.length)
            {
                fprintf(stderr, "error: option requires a value\n");
                result = false;
            }
            else if (string_value)
            {
                *string_value = arguments.pointer[index];
                result = string_value->length != 0;
            }
            else
            {
                result = production_profile_u64(arguments.pointer[index], integer_value);
                if (!result)
                {
                    fprintf(stderr, "error: expected an unsigned integer\n");
                }
            }
        }
    }

    if (result)
    {
        bool profile_valid = string_equal(options->benchmark_profile, S8("smoke")) ||
                             string_equal(options->benchmark_profile, S8("ci")) ||
                             string_equal(options->benchmark_profile, S8("full"));
        result = options->jobs && options->pairs && options->timeout_seconds && profile_valid &&
                 options->build_lanes && options->build_lanes <= options->jobs;
        if (!result)
        {
            fprintf(stderr, "error: jobs, pairs and timeout must be nonzero, build lanes must be 1..jobs, "
                            "and benchmark profile must be smoke, ci or full\n");
        }
    }

    BUSTER_UNUSED(arena);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 production_profile_binary(Arena* arena, String8 build_directory)
{
#if BUSTER_WINDOWS
    return path_join(arena, build_directory, S8("Release/ide.exe"));
#else
    return path_join(arena, build_directory, S8("Release/ide"));
#endif
}

// Lane-safe: allocates only from the calling lane's arena. Concurrent lanes
// capture child output into evidence so their logs do not interleave.
BUSTER_GLOBAL_LOCAL bool production_profile_build_variant(ProductionProfileContext* context, Arena* arena, ProductionProfileVariant variant)
{
    ProductionProfileTraining* training = variant.training;
    BUSTER_CHECK((variant.generate || variant.use) == (training != 0));
    BUSTER_CHECK(!training || training->lto == variant.lto);
    String8 directory = path_join(arena, context->output, variant.name);
    String8 raw = variant.generate ? path_join(arena, training->directory, S8("raw")) : S8("");
    String8 profile = variant.use ? training->profile : S8("");
    String8 manifest = variant.use ? training->manifest : S8("");
    String8 fingerprint = variant.use ? training->fingerprint : S8("");
    String8 debug = string_format(arena, S8("-DBUSTER_DEBUG_INFO={S8}"), variant.debug_info ? S8("ON") : S8("OFF"));
    String8 production = string_format(arena, S8("-DBUSTER_PRODUCTION_PROFILE={S8}"), variant.debug_info ? S8("OFF") : S8("ON"));
    String8 compiler = string_format(arena, S8("-DCMAKE_C_COMPILER:FILEPATH={S8}"), context->clang);
    String8 pgo_generate = string_format(arena, S8("-DBUSTER_PGO_GENERATE:PATH={S8}"), raw);
    String8 pgo_use = string_format(arena, S8("-DBUSTER_PGO_USE:FILEPATH={S8}"), profile);
    String8 pgo_manifest = string_format(arena, S8("-DBUSTER_PGO_MANIFEST:FILEPATH={S8}"), manifest);
    String8 pgo_fingerprint = string_format(arena, S8("-DBUSTER_PGO_FINGERPRINT:STRING={S8}"), fingerprint);

    String8 generate[40];
    u64 count = 0;
    generate[count++] = context->driver;
    generate[count++] = S8("generate");
    generate[count++] = S8("--build-directory");
    generate[count++] = directory;
    generate[count++] = S8("--cc");
    generate[count++] = S8("clang");
    generate[count++] = S8("--linker");
    generate[count++] = S8("LLD");
    generate[count++] = S8("--no-ci");
    generate[count++] = S8("--include-tests");
    generate[count++] = S8("--no-sanitize");
    generate[count++] = S8("--no-fuzz");
    generate[count++] = S8("--no-time-trace");
    generate[count++] = S8("--no-instrument");
    generate[count++] = variant.lto ? S8("--lto") : S8("--no-lto");
    generate[count++] = S8("--");
    generate[count++] = compiler;
    generate[count++] = debug;
    generate[count++] = S8("-DBUSTER_FRAME_POINTERS=ON");
    generate[count++] = production;
    generate[count++] = pgo_generate;
    generate[count++] = pgo_use;
    generate[count++] = pgo_manifest;
    generate[count++] = pgo_fingerprint;

    bool capture = context->build_lanes > 1;
    ProductionProfilePhaseSpec configure_phase = {
        .label = string_format(arena, S8("generate-{S8}"), variant.name), .kind = S8("configure"), .variant = variant.name, .slot = variant.slot,
    };
    ProductionProfileCommandResult generated = production_profile_command(
        context, arena, (SliceString8){.pointer = generate, .length = count}, (SliceString8){0}, (SliceString8){0},
        configure_phase, context->options.timeout_seconds, capture);
    if (!generated.success)
    {
        return false;
    }

    String8 jobs = string_format(arena, S8("-j{u64}"), context->build_jobs);
    String8 build[] = {
        context->driver, S8("build"), S8("--build-directory"), directory, S8("--config"), S8("Release"),
        S8("-t"), S8("ide"), S8("--"), jobs,
    };
    ProductionProfilePhaseSpec build_phase = {
        .label = string_format(arena, S8("build-{S8}"), variant.name), .kind = S8("build"), .variant = variant.name, .slot = variant.slot + 1,
    };
    ProductionProfileCommandResult built = production_profile_command(
        context, arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(build), (SliceString8){0}, (SliceString8){0},
        build_phase, context->options.timeout_seconds, capture);
    return built.success && path_exists(arena, production_profile_binary(arena, directory));
}

BUSTER_GLOBAL_LOCAL bool production_profile_train(ProductionProfileContext* context, Arena* arena, ProductionProfileTraining* profile_training)
{
    String8 instrumented = production_profile_binary(arena, path_join(arena, context->output, profile_training->instrumented));
    String8 raw = path_join(arena, profile_training->directory, S8("raw"));
    String8 training = path_join(arena, path_join(arena, context->output, S8("training")), profile_training->label);
    make_directory_recursive(arena, raw);
    String8 profile_pattern = path_join(arena, raw, S8("%4m.profraw"));
    String8 timeout = string_format(arena, S8("{u64}"), context->options.timeout_seconds);
    String8 revision_id = string_format(arena, S8("{S8}:training"), context->revision);
    String8 command[] = {
        context->driver, S8("bench_throughput"), S8("run"),
        S8("--baseline"), instrumented, S8("--candidate"), instrumented,
        S8("--output"), training, S8("--profile"), S8("ci"), S8("--mode"), S8("all"),
        S8("--pairs"), S8("1"), S8("--warmups"), S8("1"), S8("--timeout"), timeout,
        S8("--cpu"), S8("auto"), S8("--no-guard"), S8("--require-identical-output"),
        S8("--baseline-id"), revision_id, S8("--candidate-id"), revision_id,
    };
    String8 keys[] = {S8("LLVM_PROFILE_FILE")};
    String8 values[] = {profile_pattern};
    ProductionProfilePhaseSpec phase = {
        .label = string_format(arena, S8("train-{S8}"), profile_training->label), .kind = S8("train"),
        .variant = profile_training->instrumented, .slot = profile_training->slot,
    };
    ProductionProfileCommandResult trained = production_profile_command(
        context, arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command),
        (SliceString8)BUSTER_ARRAY_TO_SLICE(keys), (SliceString8)BUSTER_ARRAY_TO_SLICE(values),
        phase, context->options.timeout_seconds, context->build_lanes > 1);
    return trained.success;
}

BUSTER_GLOBAL_LOCAL int production_profile_path_compare(const void* left_pointer, const void* right_pointer)
{
    const String8* left = left_pointer;
    const String8* right = right_pointer;
    u64 count = BUSTER_MIN(left->length, right->length);
    int result = count ? memcmp(left->pointer, right->pointer, (size_t)count) : 0;
    return result ? result : (left->length > right->length) - (left->length < right->length);
}

BUSTER_GLOBAL_LOCAL bool production_profile_merge(ProductionProfileContext* context, Arena* arena, ProductionProfileTraining* training)
{
#if BUSTER_LINUX || BUSTER_APPLE
    String8 raw = path_join(arena, training->directory, S8("raw"));
    String8 raw_z = string_duplicate_arena(arena, raw, true);
    DIR* directory = opendir((const char*)raw_z.pointer);
    if (!directory)
    {
        fprintf(stderr, "error: could not open the raw production-profile directory\n");
        return false;
    }

    u64 file_count = 0;
    errno = 0;
    for (struct dirent* entry = readdir(directory); entry; entry = readdir(directory))
    {
        String8 name = string_from_pointer((char8*)entry->d_name);
        file_count += string_ends_with_sequence(name, S8(".profraw"));
    }
    bool valid = errno == 0 && file_count != 0;
    rewinddir(directory);

    String8* files = valid ? arena_allocate(arena, String8, file_count) : 0;
    u64 file_index = 0;
    errno = 0;
    for (struct dirent* entry = valid ? readdir(directory) : 0; entry; entry = readdir(directory))
    {
        String8 name = string_from_pointer((char8*)entry->d_name);
        if (string_ends_with_sequence(name, S8(".profraw")))
        {
            if (file_index == file_count)
            {
                valid = false;
                break;
            }
            files[file_index++] = path_join(arena, raw, name);
        }
    }
    valid &= errno == 0 && file_index == file_count;
    closedir(directory);
    if (!valid)
    {
        fprintf(stderr, "error: could not enumerate raw production profiles\n");
        return false;
    }

    qsort(files, (size_t)file_count, sizeof(files[0]), production_profile_path_compare);
    String8 output_argument = string_format(arena, S8("-output={S8}"), training->profile);
    String8* command = arena_allocate(arena, String8, file_count + 3);
    command[0] = context->llvm_profdata;
    command[1] = S8("merge");
    command[2] = output_argument;
    for (u64 index = 0; index < file_count; index += 1)
    {
        command[index + 3] = files[index];
    }
    ProductionProfilePhaseSpec phase = {
        .label = string_format(arena, S8("merge-profile-{S8}"), training->label), .kind = S8("merge"),
        .variant = training->instrumented, .slot = training->slot + 1,
    };
    ProductionProfileCommandResult merged = production_profile_command(
        context, arena, (SliceString8){.pointer = command, .length = file_count + 3}, (SliceString8){0}, (SliceString8){0},
        phase, context->options.timeout_seconds, true);
    return merged.success && path_exists(arena, training->profile);
#else
    BUSTER_UNUSED(context);
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(training);
    return false;
#endif
}

BUSTER_GLOBAL_LOCAL bool production_profile_validate_binary(ProductionProfileContext* context, String8 binary)
{
    Arena* arena = context->arena;
    String8 command[] = {context->llvm_readobj, S8("--sections"), binary};
    ProductionProfilePhaseSpec inspect_phase = {
        .label = S8("inspect-production-sections"), .kind = S8("inspect"), .variant = S8("pgo-lto"), .slot = BUSTER_PRODUCTION_PROFILE_SLOT_INSPECT,
    };
    ProductionProfileCommandResult inspected = production_profile_command(
        context, arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command), (SliceString8){0}, (SliceString8){0},
        inspect_phase, 300, true);
    if (!inspected.success)
    {
        return false;
    }
    if (production_profile_debug_sections(inspected.output))
    {
        fprintf(stderr, "error: production compiler contains a debug section\n");
        return false;
    }

    String8 final_directory = path_join(arena, context->output, S8("pgo-lto"));
    String8 jobs = string_format(arena, S8("-j{u64}"), context->options.jobs);
    String8 command_test[] = {
        context->driver, S8("build"), S8("--build-directory"), final_directory, S8("--config"), S8("Release"),
        S8("-t"), S8("test_all"), S8("--"), jobs,
    };
    ProductionProfilePhaseSpec test_phase = {
        .label = S8("test-production"), .kind = S8("test"), .variant = S8("pgo-lto"), .slot = BUSTER_PRODUCTION_PROFILE_SLOT_TEST,
    };
    ProductionProfileCommandResult tested = production_profile_command(
        context, arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command_test), (SliceString8){0}, (SliceString8){0},
        test_phase, context->options.timeout_seconds, false);
    return tested.success;
}

// Serial and uncontended: only runs after every build lane has joined.
BUSTER_GLOBAL_LOCAL bool production_profile_benchmark_one(
    ProductionProfileContext* context, String8 release, String8 candidate, String8 name, u64 slot)
{
    Arena* arena = context->arena;
    String8 directory = path_join(arena, path_join(arena, context->output, S8("benchmarks")), name);
    String8 pairs = string_format(arena, S8("{u64}"), context->options.pairs);
    String8 warmups = string_format(arena, S8("{u64}"), context->options.warmups);
    String8 timeout = string_format(arena, S8("{u64}"), context->options.timeout_seconds);
    String8 baseline_id = string_format(arena, S8("{S8}:release"), context->revision);
    String8 candidate_id = string_format(arena, S8("{S8}:{S8}"), context->revision, name);
    String8 run[] = {
        context->driver, S8("bench_throughput"), S8("run"),
        S8("--baseline"), release, S8("--candidate"), candidate, S8("--output"), directory,
        S8("--profile"), context->options.benchmark_profile, S8("--mode"), S8("all"),
        S8("--pairs"), pairs, S8("--warmups"), warmups, S8("--timeout"), timeout,
        S8("--cpu"), S8("auto"), S8("--no-guard"), S8("--require-identical-output"),
        S8("--baseline-id"), baseline_id, S8("--candidate-id"), candidate_id,
    };
    ProductionProfilePhaseSpec run_phase = {
        .label = string_format(arena, S8("benchmark-{S8}"), name), .kind = S8("benchmark"), .variant = name, .slot = slot,
    };
    ProductionProfileCommandResult measured = production_profile_command(
        context, arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(run), (SliceString8){0}, (SliceString8){0},
        run_phase, context->options.timeout_seconds, false);
    if (!measured.success)
    {
        return false;
    }

    String8 compare[] = {context->driver, S8("bench_throughput"), S8("compare"), S8("--output"), directory};
    ProductionProfilePhaseSpec compare_phase = {
        .label = string_format(arena, S8("compare-{S8}"), name), .kind = S8("compare"), .variant = name, .slot = slot + 1,
    };
    return production_profile_command(
        context, arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(compare), (SliceString8){0}, (SliceString8){0},
        compare_phase, context->options.timeout_seconds, false).success;
}

// The shared identity/workload contract plus the one policy line that differs
// per training. build_lto is part of the fingerprint so a profile can never be
// mistaken for the other mode's, and it is repeated in the manifest for CMake.
BUSTER_GLOBAL_LOCAL String8 production_profile_training_contract(Arena* arena, String8 shared, bool lto)
{
    return string_format(arena, S8("{S8}build_lto={S8}\n"), shared, lto ? S8("ON") : S8("OFF"));
}

BUSTER_GLOBAL_LOCAL bool production_profile_seal(ProductionProfileContext* context, Arena* arena, ProductionProfileTraining* training)
{
    String8 profile_sha = production_profile_sha256_file(arena, training->profile);
    bool result = profile_sha.length == 64;
    if (!result)
    {
        string_print(S8("error: merged {S8} profile is missing or empty\n"), training->label);
    }
    else
    {
        String8 manifest = string_format(arena, S8(
            BUSTER_PRODUCTION_PROFILE_MANIFEST_VERSION "\n"
            "fingerprint={S8}\n"
            "profile_sha256={S8}\n"
            "source_revision={S8}\n"
            "source_tree={S8}\n"
            "clang_sha256={S8}\n"
            "llvm_profdata_sha256={S8}\n"
            "training_contract_sha256={S8}\n"
            "training_profile=ci\n"
            "training_mode=all\n"
            "training_pairs=1\n"
            "training_warmups=1\n"
            "build_lto={S8}\n"),
            training->fingerprint, profile_sha, context->revision, context->tree, context->clang_sha, context->profdata_sha,
            training->fingerprint, training->lto ? S8("ON") : S8("OFF"));
        result = production_profile_write(training->manifest, manifest);
        if (!result)
        {
            string_print(S8("error: could not write the {S8} profile manifest\n"), training->label);
        }
    }
    return result;
}

// A training step trains, merges and seals; its PGO-use build is the next
// step of the same task, so it can never start before the matching seal.
BUSTER_GLOBAL_LOCAL bool production_profile_task_run(ProductionProfileContext* context, Arena* arena, ProductionProfileTask* task)
{
    bool result = true;
    for (u64 index = 0; result && index < task->step_count; index += 1)
    {
        ProductionProfileStep* step = task->steps + index;
        switch (step->kind)
        {
            case PRODUCTION_PROFILE_STEP_BUILD:
            {
                result = production_profile_build_variant(context, arena, step->variant);
            } break;
            case PRODUCTION_PROFILE_STEP_TRAIN:
            {
                if (context->training_mutex)
                {
                    os_mutex_lock(context->training_mutex);
                }
                result = production_profile_train(context, arena, step->training);
                if (context->training_mutex)
                {
                    os_mutex_unlock(context->training_mutex);
                }
                result = result && production_profile_merge(context, arena, step->training) &&
                         production_profile_seal(context, arena, step->training);
            } break;
            case PRODUCTION_PROFILE_STEP_COMMAND:
            {
                result = production_profile_command(context, arena, step->command, (SliceString8){0}, (SliceString8){0},
                    step->phase, context->options.timeout_seconds, true).success;
            } break;
        }
    }
    return result;
}

// One lane of the build gang: admits the next task in list order until the
// list is exhausted or any lane has failed. Each lane owns its arena.
BUSTER_GLOBAL_LOCAL void production_profile_task_lane(void* argument)
{
    ProductionProfileWork* work = argument;
    ProductionProfileContext* context = work->context;
    Arena* arena = arena_create((ArenaCreation){0});
    bool admitting = true;
    while (admitting)
    {
        u64 index = work->task_count;
        if (!process_control_atomic_load(&context->cancellation_signal))
        {
            index = atomic_u64_increment(&work->next);
        }
        admitting = index < work->task_count;
        if (admitting && !production_profile_task_run(context, arena, work->tasks + index))
        {
            atomic_u64_increment(&work->failed);
            production_profile_cancel(context);
        }
    }
    BUSTER_CHECK(arena_destroy(arena, 1));
}

// Runs every task on context->build_lanes lanes and reports whether all of
// them passed. A one-lane run uses the same lane path serially.
BUSTER_GLOBAL_LOCAL bool production_profile_tasks_run(ProductionProfileContext* context, ProductionProfileTask* tasks, u64 task_count)
{
    ProductionProfileWork work = {.context = context, .tasks = tasks, .task_count = task_count};
    lane_run(context->build_lanes, &production_profile_task_lane, &work);
    return !work.failed && !process_control_atomic_load(&context->cancellation_signal);
}

BUSTER_GLOBAL_LOCAL ProcessResult production_profile_main(Arena* arena, SliceString8 arguments, String8 driver)
{
#if !(BUSTER_LINUX || BUSTER_APPLE)
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(arguments);
    BUSTER_UNUSED(driver);
    string_print(S8("error: production_profile currently requires Linux or macOS\n"));
    return PROCESS_RESULT_FAILED;
#else
    ProductionProfileOptions options = {0};
    if (!production_profile_options(arena, arguments, &options))
    {
        if (!arguments.length || !string_equal(arguments.pointer[0], S8("--help")))
        {
            production_profile_usage();
        }
        return PROCESS_RESULT_FAILED;
    }

    ProductionProfileContext context = {.arena = arena, .options = options};
    make_directory_recursive(arena, S8("build"));
    context.repository = os_path_absolute(arena, S8("."), true);
    context.driver = os_path_absolute(arena, driver, true);
    context.output = os_path_absolute_lexical(arena, options.output, true);
    String8 build_root = os_path_absolute(arena, S8("build"), true);
    if (!context.repository.length || !context.driver.length || !context.output.length || !build_root.length ||
        !production_profile_path_components_safe(context.output) ||
        !production_profile_path_is_child(build_root, context.output))
    {
        string_print(S8("error: production_profile output must be a dot-component-free child of the checkout build directory\n"));
        return PROCESS_RESULT_FAILED;
    }

    String8 git = production_profile_resolve_tool(arena, S8("git"), S8("git"));
    context.clang = production_profile_resolve_tool(arena, options.clang, S8("Clang"));
    context.llvm_profdata = production_profile_resolve_tool(arena, options.llvm_profdata, S8("llvm-profdata"));
    context.llvm_readobj = production_profile_resolve_tool(arena, options.llvm_readobj, S8("llvm-readobj"));
    if (!git.length || !context.clang.length || !context.llvm_profdata.length || !context.llvm_readobj.length)
    {
        return PROCESS_RESULT_FAILED;
    }

    String8 status_command[] = {
        git, S8("status"), S8("--porcelain=v1"), S8("--untracked-files=all"), S8("--"), S8("."),
        S8(":(exclude)build"), S8(":(exclude)build/**"),
    };
    ProductionProfileCommandResult status = production_profile_capture(
        &context, (SliceString8)BUSTER_ARRAY_TO_SLICE(status_command));
    if (!status.success || production_profile_trim(status.output).length)
    {
        if (status.output.length)
        {
            os_file_write(os_get_standard_stream(STANDARD_STREAM_ERROR), BUSTER_SLICE_TO_BYTE_SLICE(status.output));
        }
        string_print(S8("error: production_profile requires a clean source tree outside build/\n"));
        return PROCESS_RESULT_FAILED;
    }
    String8 revision_command[] = {git, S8("rev-parse"), S8("--verify"), S8("HEAD")};
    String8 tree_command[] = {git, S8("rev-parse"), S8("--verify"), S8("HEAD^{tree}")};
    ProductionProfileCommandResult revision = production_profile_capture(&context, (SliceString8)BUSTER_ARRAY_TO_SLICE(revision_command));
    ProductionProfileCommandResult tree = production_profile_capture(&context, (SliceString8)BUSTER_ARRAY_TO_SLICE(tree_command));
    context.revision = production_profile_trim(revision.output);
    context.tree = production_profile_trim(tree.output);
    if (!revision.success || !tree.success || context.revision.length != 40 || context.tree.length != 40)
    {
        string_print(S8("error: could not identify the source revision and tree\n"));
        return PROCESS_RESULT_FAILED;
    }

    if (path_exists(arena, context.output))
    {
        if (!options.clean)
        {
            string_print(S8("error: production_profile output already exists; choose a fresh path or pass --clean\n"));
            return PROCESS_RESULT_FAILED;
        }
        if (!os_directory_delete(context.output))
        {
            string_print(S8("error: could not remove the previous production_profile output\n"));
            return PROCESS_RESULT_FAILED;
        }
    }
    make_directory_recursive(arena, context.output);
    context.evidence = path_join(arena, context.output, S8("evidence"));
    make_directory_recursive(arena, context.evidence);

    // Host facts are the visible machine, not effective cgroup/runner limits
    // (#2758 owns those); the workload knobs explain the worker budget.
    u64 memory_mib = os_get_physical_memory_size() >> 20;
    context.build_lanes = production_profile_build_lanes(options.build_lanes, options.jobs, memory_mib, BUSTER_PRODUCTION_PROFILE_TASK_COUNT);
    context.build_jobs = BUSTER_MAX(options.jobs / context.build_lanes, 1);
    if (context.build_lanes != options.build_lanes)
    {
        string_print(S8("note: admitted {u64} of {u64} requested build lanes\n"), context.build_lanes, options.build_lanes);
    }
    ProductionProfileLedger ledger = {
        .tsv_path = path_join(arena, context.evidence, S8("phases.tsv")),
        .markdown_path = path_join(arena, context.evidence, S8("phases.md")),
        .origin_us = os_now_microseconds(),
        .expected = production_profile_phase_expected(options.benchmark),
        .arena = arena,
    };
    if (context.build_lanes > 1)
    {
        context.spawn_mutex = os_mutex_create();
        context.training_mutex = os_mutex_create();
        ledger.mutex = os_mutex_create();
    }
    String8 run_identity = string_format(arena, S8(
        "source_revision={S8}\n"
        "source_tree={S8}\n"
        "jobs={u64}\n"
        "build_lanes_requested={u64}\n"
        "build_lanes={u64}\n"
        "build_jobs_per_lane={u64}\n"
        "lane_memory_mib={u64}\n"
        "benchmark={S8}\n"
        "benchmark_profile={S8}\n"
        "pairs={u64}\n"
        "warmups={u64}\n"
        "host_logical_threads={u32}\n"
        "host_physical_memory_mib={u64}\n"),
        context.revision, context.tree, options.jobs, options.build_lanes, context.build_lanes, context.build_jobs,
        BUSTER_PRODUCTION_PROFILE_LANE_MEMORY_MIB, options.benchmark ? S8("on") : S8("off"),
        options.benchmark_profile, options.pairs, options.warmups,
        os_get_logical_thread_count(), memory_mib);
    ledger.identity = run_identity;
    context.ledger = &ledger;
    if (!production_profile_ledger_flush(arena, &ledger))
    {
        return PROCESS_RESULT_FAILED;
    }

    String8 clang_version_command[] = {context.clang, S8("--version")};
    String8 clang_target_command[] = {context.clang, S8("-dumpmachine")};
    String8 profdata_version_command[] = {context.llvm_profdata, S8("--version")};
    ProductionProfilePhaseSpec clang_version_phase = {
        .label = S8("clang-version"), .kind = S8("toolchain"), .slot = BUSTER_PRODUCTION_PROFILE_SLOT_TOOLCHAIN,
    };
    ProductionProfilePhaseSpec clang_target_phase = {
        .label = S8("clang-target"), .kind = S8("toolchain"), .slot = BUSTER_PRODUCTION_PROFILE_SLOT_TOOLCHAIN + 1,
    };
    ProductionProfilePhaseSpec profdata_version_phase = {
        .label = S8("llvm-profdata-version"), .kind = S8("toolchain"), .slot = BUSTER_PRODUCTION_PROFILE_SLOT_TOOLCHAIN + 2,
    };
    ProductionProfileCommandResult clang_version = production_profile_command(
        &context, arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(clang_version_command), (SliceString8){0}, (SliceString8){0},
        clang_version_phase, 120, true);
    ProductionProfileCommandResult clang_target = production_profile_command(
        &context, arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(clang_target_command), (SliceString8){0}, (SliceString8){0},
        clang_target_phase, 120, true);
    ProductionProfileCommandResult profdata_version = production_profile_command(
        &context, arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(profdata_version_command), (SliceString8){0}, (SliceString8){0},
        profdata_version_phase, 120, true);

    String8 clang_sha = production_profile_sha256_file(arena, context.clang);
    String8 profdata_sha = production_profile_sha256_file(arena, context.llvm_profdata);
    context.clang_sha = clang_sha;
    context.profdata_sha = profdata_sha;
    String8 clang_identity_sha = production_profile_sha256_text(arena, clang_version.output);
    String8 clang_target_sha = production_profile_sha256_text(arena, production_profile_trim(clang_target.output));
    String8 profdata_identity_sha = production_profile_sha256_text(arena, profdata_version.output);
    if (!clang_version.success || !clang_target.success || !profdata_version.success ||
        clang_sha.length != 64 || profdata_sha.length != 64)
    {
        string_print(S8("error: could not seal the LLVM toolchain identity\n"));
        return PROCESS_RESULT_FAILED;
    }
    ledger.identity = string_format(arena, S8("{S8}clang_sha256={S8}\nclang_target={S8}\nllvm_profdata_sha256={S8}\n"),
        run_identity, clang_sha, production_profile_trim(clang_target.output), profdata_sha);

    String8 shared_contract = string_format(arena, S8(
        BUSTER_PRODUCTION_PROFILE_CONTRACT_VERSION "\n"
        "source_revision={S8}\n"
        "source_tree={S8}\n"
        "clang_sha256={S8}\n"
        "clang_identity_sha256={S8}\n"
        "clang_target_sha256={S8}\n"
        "llvm_profdata_sha256={S8}\n"
        "llvm_profdata_identity_sha256={S8}\n"
        "training_profile=ci\n"
        "training_mode=all\n"
        "training_pairs=1\n"
        "training_warmups=1\n"
        "training_artifact=object\n"
        "build_debug_info=OFF\n"
        "build_frame_pointers=ON\n"),
        context.revision, context.tree, clang_sha, clang_identity_sha, clang_target_sha,
        profdata_sha, profdata_identity_sha);

    // The production profile keeps the profile/ root; the non-LTO profile only
    // seals the pgo comparison tree.
    ProductionProfileTraining trainings[] = {
        {.instrumented = S8("instrumented"), .directory = path_join(arena, context.output, S8("profile-no-lto")), .label = S8("no-lto"),
         .slot = BUSTER_PRODUCTION_PROFILE_SLOT_TRAIN_NO_LTO},
        {.instrumented = S8("instrumented-lto"), .directory = path_join(arena, context.output, S8("profile")), .label = S8("lto"),
         .slot = BUSTER_PRODUCTION_PROFILE_SLOT_TRAIN_LTO, .lto = true},
    };
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(trainings); index += 1)
    {
        ProductionProfileTraining* training = trainings + index;
        BUSTER_CHECK(training->lto == (index != 0));
        String8 contract = production_profile_training_contract(arena, shared_contract, training->lto);
        training->fingerprint = production_profile_sha256_text(arena, contract);
        training->profile = path_join(arena, training->directory, S8("merged.profdata"));
        training->manifest = path_join(arena, training->directory, S8("manifest.txt"));
        make_directory_recursive(arena, training->directory);
        if (!production_profile_write(path_join(arena, training->directory, S8("training-contract.txt")), contract))
        {
            string_print(S8("error: could not write the training contract\n"));
            return PROCESS_RESULT_FAILED;
        }
    }
    ProductionProfileTraining* training_no_lto = trainings + 0;
    ProductionProfileTraining* training_lto = trainings + 1;

    // Longest task first (measured on the #2790 ledgers): the ThinLTO chain,
    // then the non-LTO chain, then the plain trees. Each chain's PGO-use build
    // follows its own seal; nothing else depends on another task.
    ProductionProfileTask tasks[] = {
        {.steps = {
            {.kind = PRODUCTION_PROFILE_STEP_BUILD, .variant = {.name = training_lto->instrumented, .training = training_lto,
                .slot = BUSTER_PRODUCTION_PROFILE_SLOT_INSTRUMENTED_LTO, .lto = true, .generate = true}},
            {.kind = PRODUCTION_PROFILE_STEP_TRAIN, .training = training_lto},
            {.kind = PRODUCTION_PROFILE_STEP_BUILD, .variant = {.name = S8("pgo-lto"), .training = training_lto,
                .slot = BUSTER_PRODUCTION_PROFILE_SLOT_PGO_LTO, .lto = true, .use = true}},
        }, .step_count = 3},
        {.steps = {
            {.kind = PRODUCTION_PROFILE_STEP_BUILD, .variant = {.name = training_no_lto->instrumented, .training = training_no_lto,
                .slot = BUSTER_PRODUCTION_PROFILE_SLOT_INSTRUMENTED, .generate = true}},
            {.kind = PRODUCTION_PROFILE_STEP_TRAIN, .training = training_no_lto},
            {.kind = PRODUCTION_PROFILE_STEP_BUILD, .variant = {.name = S8("pgo"), .training = training_no_lto,
                .slot = BUSTER_PRODUCTION_PROFILE_SLOT_PGO, .use = true}},
        }, .step_count = 3},
        {.steps = {{.kind = PRODUCTION_PROFILE_STEP_BUILD, .variant = {.name = S8("release"),
            .slot = BUSTER_PRODUCTION_PROFILE_SLOT_RELEASE, .debug_info = true}}}, .step_count = 1},
        {.steps = {{.kind = PRODUCTION_PROFILE_STEP_BUILD, .variant = {.name = S8("lto"),
            .slot = BUSTER_PRODUCTION_PROFILE_SLOT_LTO, .lto = true}}}, .step_count = 1},
        {.steps = {{.kind = PRODUCTION_PROFILE_STEP_BUILD, .variant = {.name = S8("g0"),
            .slot = BUSTER_PRODUCTION_PROFILE_SLOT_G0}}}, .step_count = 1},
    };
    BUSTER_CHECK(BUSTER_ARRAY_LENGTH(tasks) == BUSTER_PRODUCTION_PROFILE_TASK_COUNT);
    if (!production_profile_tasks_run(&context, tasks, BUSTER_ARRAY_LENGTH(tasks)))
    {
        string_print(S8("error: production-profile build lanes failed; see evidence/phases.md\n"));
        return PROCESS_RESULT_FAILED;
    }

    String8 release = production_profile_binary(arena, path_join(arena, context.output, S8("release")));
    String8 g0 = production_profile_binary(arena, path_join(arena, context.output, S8("g0")));
    String8 lto = production_profile_binary(arena, path_join(arena, context.output, S8("lto")));
    String8 pgo = production_profile_binary(arena, path_join(arena, context.output, S8("pgo")));
    String8 pgo_lto = production_profile_binary(arena, path_join(arena, context.output, S8("pgo-lto")));
    if (!production_profile_validate_binary(&context, pgo_lto))
    {
        return PROCESS_RESULT_FAILED;
    }

    if (options.benchmark)
    {
        struct
        {
            String8 name;
            String8 binary;
        } comparisons[] = {
            {S8("g0"), g0},
            {S8("lto"), lto},
            {S8("pgo"), pgo},
            {S8("pgo-lto"), pgo_lto},
        };
        for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(comparisons); index += 1)
        {
            if (!production_profile_benchmark_one(&context, release, comparisons[index].binary, comparisons[index].name,
                    BUSTER_PRODUCTION_PROFILE_SLOT_BENCHMARK + index * 2))
            {
                return PROCESS_RESULT_FAILED;
            }
        }
    }

    String8 final_sha = production_profile_sha256_file(arena, pgo_lto);
    String8 summary = string_format(arena, S8(
        "status=passed\n"
        "source_revision={S8}\n"
        "source_tree={S8}\n"
        "fingerprint={S8}\n"
        "profile={S8}\n"
        "profile_sha256={S8}\n"
        "manifest={S8}\n"
        "comparison_profile_no_lto={S8}\n"
        "comparison_manifest_no_lto={S8}\n"
        "production_compiler={S8}\n"
        "production_compiler_sha256={S8}\n"
        "debug_sections=absent\n"
        "correctness=test_all\n"
        "deterministic_outputs=throughput-require-identical-output\n"
        "benchmarks={S8}\n"
        "phase_ledger={S8}\n"),
        context.revision, context.tree, training_lto->fingerprint, training_lto->profile,
        production_profile_sha256_file(arena, training_lto->profile), training_lto->manifest,
        training_no_lto->profile, training_no_lto->manifest, pgo_lto, final_sha, options.benchmark ? S8("complete") : S8("skipped"),
        ledger.tsv_path);
    // Every expected phase must have run; a skipped phase is never success.
    if (ledger.count != ledger.expected)
    {
        string_print(S8("error: production-profile recorded {u64} of {u64} expected phases\n"), ledger.count, ledger.expected);
        return PROCESS_RESULT_FAILED;
    }
    if (!production_profile_write(path_join(arena, context.output, S8("summary.txt")), summary))
    {
        string_print(S8("error: could not write the production-profile summary\n"));
        return PROCESS_RESULT_FAILED;
    }
    ledger.complete = true;
    if (!production_profile_ledger_flush(arena, &ledger))
    {
        return PROCESS_RESULT_FAILED;
    }

    string_print(S8("PRODUCTION_PROFILE result=pass compiler={S8} manifest={S8} summary={S8}\n"),
        pgo_lto, training_lto->manifest, path_join(arena, context.output, S8("summary.txt")));
    return PROCESS_RESULT_SUCCESS;
#endif
}

BUSTER_GLOBAL_LOCAL ProductionProfileTask production_profile_command_task(SliceString8 command, String8 label, u64 slot)
{
    ProductionProfileTask result = {.step_count = 1};
    result.steps[0] = (ProductionProfileStep){
        .kind = PRODUCTION_PROFILE_STEP_COMMAND,
        .command = command,
        .phase = {.label = label, .kind = S8("command"), .slot = slot},
    };
    return result;
}

// Drives the real lane scheduler with short POSIX commands: all tasks land in
// their slots; a failure stops admission; and, with two lanes, it terminates
// the other lane's in-flight child promptly instead of waiting it out.
BUSTER_GLOBAL_LOCAL u64 production_profile_scheduler_self_test(Arena* arena)
{
    u64 failures = 0;
#if BUSTER_LINUX || BUSTER_APPLE
    String8 pass_argv[] = {S8("true")};
    String8 fail_argv[] = {S8("false")};
    String8 sleep_argv[] = {S8("sleep"), S8("30")};
    String8 late_fail_argv[] = {S8("sh"), S8("-c"), S8("sleep 1; exit 3")};
    SliceString8 pass_command = BUSTER_ARRAY_TO_SLICE(pass_argv);
    SliceString8 fail_command = BUSTER_ARRAY_TO_SLICE(fail_argv);
    SliceString8 sleep_command = BUSTER_ARRAY_TO_SLICE(sleep_argv);
    SliceString8 late_fail_command = BUSTER_ARRAY_TO_SLICE(late_fail_argv);

    for (u64 scenario = 0; scenario < 3; scenario += 1)
    {
        bool cancellation = scenario == 2;
        if (cancellation && BUSTER_SINGLE_THREADED)
        {
            continue;
        }
        ProductionProfileLedger ledger = {.expected = 3, .origin_us = os_now_microseconds()};
        ProductionProfileContext context = {
            .arena = arena,
            .options = {.timeout_seconds = 60},
            .ledger = &ledger,
            .build_lanes = scenario == 1 ? 1 : 2,
        };
        context.build_lanes = production_profile_build_lanes(context.build_lanes, context.build_lanes, UINT64_MAX >> 20, 3);
        if (context.build_lanes > 1)
        {
            context.spawn_mutex = os_mutex_create();
            ledger.mutex = os_mutex_create();
        }
        ProductionProfileTask tasks[3];
        if (scenario == 0)
        {
            tasks[0] = production_profile_command_task(pass_command, S8("pass-c"), 2);
            tasks[1] = production_profile_command_task(pass_command, S8("pass-a"), 0);
            tasks[2] = production_profile_command_task(pass_command, S8("pass-b"), 1);
        }
        else if (scenario == 1)
        {
            tasks[0] = production_profile_command_task(fail_command, S8("fail"), 0);
            tasks[1] = production_profile_command_task(pass_command, S8("not-admitted"), 1);
            tasks[2] = production_profile_command_task(pass_command, S8("not-admitted-either"), 2);
        }
        else
        {
            tasks[0] = production_profile_command_task(sleep_command, S8("in-flight"), 0);
            tasks[1] = production_profile_command_task(late_fail_command, S8("late-fail"), 1);
            tasks[2] = production_profile_command_task(pass_command, S8("not-admitted"), 2);
        }
        u64 start_us = os_now_microseconds();
        bool passed = production_profile_tasks_run(&context, tasks, BUSTER_ARRAY_LENGTH(tasks));
        u64 elapsed_us = os_now_microseconds() - start_us;
        ProductionProfilePhase* phases = ledger.phases;
        if (scenario == 0)
        {
            failures += !passed || ledger.count != 3;
            failures += !phases[0].success || !phases[1].success || !phases[2].success;
            failures += !string_equal(phases[0].label, S8("pass-a")) || !string_equal(phases[2].label, S8("pass-c"));
        }
        else if (scenario == 1)
        {
            failures += passed || ledger.count != 1;
            failures += !phases[0].recorded || phases[0].success || phases[0].cancelled;
            failures += phases[1].recorded || phases[2].recorded;
        }
        else
        {
            failures += passed || ledger.count != 2;
            failures += !phases[1].recorded || phases[1].success || phases[1].cancelled;
            failures += !phases[0].recorded || phases[0].success || !phases[0].cancelled;
            failures += phases[2].recorded;
            failures += elapsed_us >= 15000000;
        }
        if (context.spawn_mutex)
        {
            os_mutex_destroy(context.spawn_mutex);
            os_mutex_destroy(ledger.mutex);
        }
        if (failures)
        {
            string_print(S8("error: production-profile scheduler self-test scenario {u64} failed\n"), scenario);
        }
    }
#else
    BUSTER_UNUSED(arena);
#endif
    return failures;
}

BUSTER_GLOBAL_LOCAL ProcessResult production_profile_self_test(Arena* arena)
{
    u64 failures = 0;
    u64 value = 0;
    failures += !production_profile_u64(S8("0"), &value) || value != 0;
    failures += !production_profile_u64(S8("18446744073709551615"), &value) || value != UINT64_MAX;
    failures += production_profile_u64(S8("18446744073709551616"), &value);
    failures += production_profile_u64(S8("-1"), &value);
    failures += production_profile_u64((String8){0}, &value);
    failures += !production_profile_debug_sections(S8("Name: .debug_info\n"));
    failures += !production_profile_debug_sections(S8("Segment: __DWARF\nName: __debug_line\n"));
    failures += production_profile_debug_sections(S8("Name: .text\nName: .symtab\nName: .llvm_addrsig\n"));
    String8 contract = S8(BUSTER_PRODUCTION_PROFILE_CONTRACT_VERSION "\nsource_revision=abc\n");
    String8 first = production_profile_sha256_text(arena, contract);
    String8 second = production_profile_sha256_text(arena, contract);
    String8 third = production_profile_sha256_text(arena, S8(BUSTER_PRODUCTION_PROFILE_CONTRACT_VERSION "\nsource_revision=abd\n"));
    failures += first.length != 64 || !string_equal(first, second) || string_equal(first, third);
    String8 contract_lto = production_profile_training_contract(arena, contract, true);
    String8 contract_no_lto = production_profile_training_contract(arena, contract, false);
    failures += !string_ends_with_sequence(contract_lto, S8("\nbuild_lto=ON\n"));
    failures += !string_ends_with_sequence(contract_no_lto, S8("\nbuild_lto=OFF\n"));
    failures += string_equal(production_profile_sha256_text(arena, contract_lto), production_profile_sha256_text(arena, contract_no_lto));
    failures += !production_profile_path_is_child(S8("/checkout"), S8("/checkout/build/profile"));
    failures += production_profile_path_is_child(S8("/checkout"), S8("/checkout-other"));
    failures += !production_profile_path_components_safe(S8("/checkout/build/profile"));
    failures += production_profile_path_components_safe(S8("/checkout/build/../src"));
    failures += production_profile_path_components_safe(S8("/checkout/build/./profile"));

    failures += production_profile_environment_key_equal(S8("PATH"), S8("HOME"));
#if BUSTER_WINDOWS
    failures += !production_profile_environment_key_equal(S8("Path"), S8("PATH"));
#else
    failures += production_profile_environment_key_equal(S8("Path"), S8("PATH"));
#endif

    String8 inherited_keys[] = {S8("PATH"), S8("LLVM_PROFILE_FILE"), S8("HOME")};
    String8 inherited_values[] = {S8("/bin"), S8("old.profraw"), S8("/home/runner")};
    String8 override_keys[] = {S8("LLVM_PROFILE_FILE"), S8("EMPTY")};
    String8 override_values[] = {S8("new-%p.profraw"), (String8){0}};
    SliceString8 merged_keys = {0};
    SliceString8 merged_values = {0};
    bool merged = production_profile_environment_merge(
        arena,
        (SliceString8)BUSTER_ARRAY_TO_SLICE(inherited_keys),
        (SliceString8)BUSTER_ARRAY_TO_SLICE(inherited_values),
        (SliceString8)BUSTER_ARRAY_TO_SLICE(override_keys),
        (SliceString8)BUSTER_ARRAY_TO_SLICE(override_values),
        &merged_keys, &merged_values);
    failures += !merged || merged_keys.length != 4 || merged_values.length != 4;
    if (merged && merged_keys.length == 4 && merged_values.length == 4)
    {
        failures += !string_equal(merged_keys.pointer[0], S8("PATH")) || !string_equal(merged_values.pointer[0], S8("/bin"));
        failures += !string_equal(merged_keys.pointer[1], S8("HOME")) || !string_equal(merged_values.pointer[1], S8("/home/runner"));
        failures += !string_equal(merged_keys.pointer[2], S8("LLVM_PROFILE_FILE")) || !string_equal(merged_values.pointer[2], S8("new-%p.profraw"));
        failures += !string_equal(merged_keys.pointer[3], S8("EMPTY")) || merged_values.pointer[3].length != 0;
    }

    ProductionProfileOptions options = {0};
    String8 valid_arguments[] = {
        S8("--output"), S8("build/pgo"), S8("--benchmark-profile"), S8("ci"),
        S8("--jobs"), S8("3"), S8("--pairs"), S8("4"), S8("--warmups"), S8("0"),
        S8("--timeout"), S8("90"), S8("--clean"), S8("--no-benchmark"),
    };
    failures += !production_profile_options(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(valid_arguments), &options);
    failures += !string_equal(options.output, S8("build/pgo")) || !string_equal(options.benchmark_profile, S8("ci")) ||
                options.jobs != 3 || options.pairs != 4 || options.warmups != 0 || options.timeout_seconds != 90 ||
                !options.clean || options.benchmark;
    String8 invalid_arguments[] = {S8("--pairs"), S8("0")};
    failures += production_profile_options(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_arguments), &options);
    String8 lanes_arguments[] = {S8("--jobs"), S8("3"), S8("--build-lanes"), S8("2")};
    failures += !production_profile_options(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(lanes_arguments), &options) ||
                options.jobs != 3 || options.build_lanes != 2;
    String8 lanes_over_budget[] = {S8("--jobs"), S8("2"), S8("--build-lanes"), S8("3")};
    failures += production_profile_options(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(lanes_over_budget), &options);
    String8 lanes_zero[] = {S8("--build-lanes"), S8("0")};
    failures += production_profile_options(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(lanes_zero), &options);

    failures += production_profile_phase_expected(true) != 31 || production_profile_phase_expected(false) != 23;
    failures += production_profile_phase_expected(true) > BUSTER_PRODUCTION_PROFILE_PHASE_CAPACITY;
    failures += !string_equal(production_profile_seconds_text(arena, 1599999), S8("1.5"));
    failures += !string_equal(production_profile_seconds_text(arena, 0), S8("0.0"));

    // Phases land in their slots whatever the completion order, a failure or
    // deadline stays visible, and an unfinished ledger reports itself
    // incomplete. An empty tsv_path keeps the self-test from writing files.
    ProductionProfileLedger ledger = {.identity = S8("source_revision=abc\n"), .expected = 4};
    ProductionProfilePhase observed = {
        .label = S8("build-release"), .kind = S8("build"), .variant = S8("release"), .start_us = 0, .duration_us = 3000000,
        .resources = {.user_cpu_us = 4000000, .system_cpu_us = 1000000, .peak_memory_bytes = 3ull << 20,
                      .cpu_status = PROCESS_RESOURCE_OBSERVED, .memory_status = PROCESS_RESOURCE_OBSERVED},
        .success = true,
    };
    ProductionProfilePhase failed = {
        .label = S8("build-g0"), .kind = S8("build"), .variant = S8("g0"), .start_us = 3000000, .duration_us = 1000000,
    };
    ProductionProfilePhase timed_out = {
        .label = S8("train-lto"), .kind = S8("train"), .start_us = 4000000, .duration_us = 4000000, .timed_out = true,
    };
    failures += !production_profile_ledger_record(arena, &ledger, 3, failed);
    failures += !production_profile_ledger_record(arena, &ledger, 1, timed_out);
    failures += !production_profile_ledger_record(arena, &ledger, 0, observed);
    failures += production_profile_ledger_record(arena, &ledger, 0, observed);
    failures += production_profile_ledger_record(arena, &ledger, BUSTER_PRODUCTION_PROFILE_PHASE_CAPACITY, observed);
    failures += ledger.count != 3 || production_profile_ledger_elapsed(&ledger) != 8000000;
    String8 tsv = production_profile_ledger_tsv(arena, &ledger);
    String8 tsv_first = S8("\n1\tbuild-release\tbuild\trelease\t0\t3000000\tpassed\t4000000\t1000000\t3145728\n");
    String8 tsv_second = S8("\n2\ttrain-lto\ttrain\t-\t4000000\t4000000\ttimed_out\t-\t-\t-\n");
    String8 tsv_fourth = S8("\n4\tbuild-g0\tbuild\tg0\t3000000\t1000000\tfailed\t-\t-\t-\n");
    failures += !string_starts_with_sequence(tsv, S8(BUSTER_PRODUCTION_PROFILE_LEDGER_VERSION "\tcomplete=0\tphases=3\texpected=4\n"));
    failures += !production_profile_contains(tsv, tsv_first) || !production_profile_contains(tsv, tsv_second) ||
                !production_profile_contains(tsv, tsv_fourth);
    failures += !(string_first_sequence(tsv, tsv_first) < string_first_sequence(tsv, tsv_second) &&
                  string_first_sequence(tsv, tsv_second) < string_first_sequence(tsv, tsv_fourth));
    String8 markdown = production_profile_ledger_markdown(arena, &ledger);
    failures += !production_profile_contains(markdown, S8("Result: **incomplete** (3 of 4 phases recorded, 8.0 s elapsed)."));
    failures += !production_profile_contains(markdown, S8("source_revision=abc\n```"));
    failures += !production_profile_contains(markdown, S8("| build | 2 | 4.0 | 50.0% | - |\n| train | 1 | 4.0 | 50.0% | - |\n"));
    failures += !production_profile_contains(markdown, S8("| 1 | `build-release` | build | release | 0.0 | 3.0 | passed | 5.0 | 3 |\n"));
    failures += !production_profile_contains(markdown, S8("| 2 | `train-lto` | train | - | 4.0 | 4.0 | timed_out | - | - |\n"));
    ledger.complete = true;
    failures += !production_profile_contains(production_profile_ledger_markdown(arena, &ledger), S8("Result: **complete**"));
    ProductionProfileLedger owned = {.expected = 1, .arena = arena};
    Arena* lane_arena = arena_create((ArenaCreation){0});
    ProductionProfilePhase lane_phase = {.label = string_format(lane_arena, S8("lane-label-{u64}"), 7ull), .kind = S8("build"), .success = true};
    failures += !production_profile_ledger_record(arena, &owned, 0, lane_phase);
    BUSTER_CHECK(arena_destroy(lane_arena, 1));
    failures += !production_profile_contains(production_profile_ledger_tsv(arena, &owned), S8("\n1\tlane-label-7\tbuild\t"));
    ProductionProfilePhase cancelled = {.cancelled = true};
    failures += !string_equal(production_profile_phase_outcome(&cancelled), S8("cancelled"));

    // Lane admission never exceeds the worker budget, the 4 GiB-per-lane
    // memory bound or the task count, and never drops below one lane.
    failures += production_profile_build_lanes(2, 2, 16094, 5) != (BUSTER_SINGLE_THREADED ? 1 : 2);
    failures += production_profile_build_lanes(8, 8, 65536, 5) != (BUSTER_SINGLE_THREADED ? 1 : 5);
    failures += production_profile_build_lanes(4, 4, 6000, 5) != 1;
    failures += production_profile_build_lanes(2, 2, 0, 5) != 1;
    failures += production_profile_build_lanes(1, 4, 65536, 5) != 1;

    failures += production_profile_scheduler_self_test(arena);

    string_print(S8("PRODUCTION_PROFILE_SELF_TEST failures={u64} result={S8}\n"), failures, failures ? S8("fail") : S8("pass"));
    return failures ? PROCESS_RESULT_FAILED : PROCESS_RESULT_SUCCESS;
}
