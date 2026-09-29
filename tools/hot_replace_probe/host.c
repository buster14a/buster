// Isolated controlled-host diagnostic. Ownership: this file owns version slots,
// executable mappings, object arenas, and every borrowed typed entry address.
// Entry: main; map: probe_prepare -> probe_publish -> probe_pin/probe_drop ->
// probe_reclaim. All operations are serial on this host's one OS thread.
// No Buster compiler dispatch, production behavior, or lane policy is changed.
// A lease must not be copied; raw entry addresses may not escape its lifetime.

#include <buster/lib/base.h>
#include <buster/lib/arena.h>
#include <buster/lib/compiler/jit/jit.h>
#include <buster/lib/string.h>
#include <buster/lib/system_headers.h>
#include <buster/lib/target.h>
#include "pilot_contract.h"
#include <stdio.h>
#include <time.h>

#if !BUSTER_LINUX || !BUSTER_CPU_ARCH_X86_64
#error The diagnostic is restricted to a controlled Linux x86-64 host.
#endif

BUSTER_GLOBAL_LOCAL ProgramState probe_program_state;
BUSTER_V_IMPL ProgramState* program_state = &probe_program_state;
BUSTER_V_IMPL OsState os_state;

typedef enum ProbeState
{
    PROBE_EMPTY,
    PROBE_PREPARING,
    PROBE_READY,
    PROBE_ACTIVE,
    PROBE_RETIRED,
    PROBE_REJECTED,
    PROBE_RELEASE_PENDING,
    PROBE_RELEASED,
} ProbeState;

typedef enum ProbeError
{
    PROBE_OK,
    PROBE_IO,
    PROBE_OBJECT,
    PROBE_ABI,
    PROBE_MODULE_STATE,
    PROBE_ENTRY_KIND,
    PROBE_JIT,
    PROBE_BUSY,
    PROBE_BAD_TRANSITION,
    PROBE_RELEASE_FAILED,
} ProbeError;

typedef struct ProbeVersion ProbeVersion;
struct ProbeVersion
{
    Arena* arena;
    ObjectFile object;
    JitProgram program;
    void* entry;
    ProbeState state;
    ProbeError error;
    u32 pins;
    u32 generation;
    bool host_state;
};

typedef struct ProbeLease ProbeLease;
struct ProbeLease
{
    ProbeVersion* version;
    PilotFunction* function;
    PilotStateFunction* state_function;
};

typedef struct ProbeHost ProbeHost;
struct ProbeHost
{
    ProbeVersion* active;
    u64 candidate_count;
    u64 mapped_count;
    u64 mapped_bytes;
    u64 metadata_bytes;
    u64 section_count;
    u64 symbol_count;
    u64 relocation_count;
    u64 publication_count;
    u64 reclamation_count;
    u64 calls;
    u64 read_ns;
    u64 parse_policy_ns;
    u64 jit_lookup_ns;
    u64 publication_ns;
    u64 reclamation_ns;
    u64 call_ns;
    u32 next_generation;
    u32 checks;
    u32 failures;
    bool timings;
    bool reject_arena_release_once;
    bool bind_fixture_import;
};

BUSTER_GLOBAL_LOCAL int probe_host_import(int value)
{
    return value + 17;
}

BUSTER_GLOBAL_LOCAL u64 probe_stamp(ProbeHost const* host)
{
    u64 result = 0;
    if (host->timings)
    {
        struct timespec stamp;
        if (clock_gettime(CLOCK_MONOTONIC, &stamp) == 0)
        {
            result = (u64)stamp.tv_sec * UINT64_C(1000000000) + (u64)stamp.tv_nsec;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void probe_check(ProbeHost* host, bool valid, char const* name)
{
    host->checks += 1;
    host->failures += !valid;
    printf("CHECK %s %s\n", valid ? "PASS" : "FAIL", name);
}

BUSTER_GLOBAL_LOCAL ProbeError probe_read(ProbeVersion* version, char const* path, ByteSlice* bytes)
{
    ProbeError result = PROBE_IO;
    FILE* file = fopen(path, "rb");
    if (file)
    {
        if (fseek(file, 0, SEEK_END) == 0)
        {
            long size = ftell(file);
            // This bounded fixture reader deliberately excludes arbitrary modules.
            if (size > 0 && size <= (long)BUSTER_MB(1) && fseek(file, 0, SEEK_SET) == 0)
            {
                bytes->pointer = arena_allocate(version->arena, u8, (u64)size);
                bytes->length = (u64)size;
                if (fread(bytes->pointer, 1, (size_t)size, file) == (size_t)size)
                {
                    result = PROBE_OK;
                }
            }
        }
        if (fclose(file) != 0)
        {
            result = PROBE_IO;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProbeError probe_policy(ProbeHost* host, ProbeVersion* version)
{
    ProbeError result = PROBE_ABI;
    u64 expected = version->host_state ? PILOT_ABI_HOST_STATE : PILOT_ABI_STATELESS;
    String8 entry_name = version->host_state ? S8("pilot_step") : S8("pilot");
    ObjectFile* object = &version->object;
    host->section_count += object->section_count;
    host->symbol_count += object->symbol_count;
    host->relocation_count += object->relocation_count;
    for (u32 index = 0; index < object->symbol_count; index += 1)
    {
        ObjectSymbol const* symbol = object->symbols + index;
        if (string_equal(symbol->name, S8("pilot_abi")) && symbol->kind == OBJECT_SYMBOL_DATA &&
            symbol->section != OBJECT_SECTION_UNDEFINED && symbol->section < object->section_count)
        {
            ObjectSection const* section = object->sections + symbol->section;
            if (section->kind == OBJECT_SECTION_READ_ONLY_DATA && symbol->size == sizeof(expected) &&
                symbol->value <= section->data.length && sizeof(expected) <= section->data.length - symbol->value)
            {
                u64 observed;
                memcpy(&observed, section->data.pointer + symbol->value, sizeof(observed));
                if (observed == expected)
                {
                    result = PROBE_OK;
                }
            }
        }
    }
    if (result == PROBE_OK)
    {
        for (u32 index = 0; index < object->section_count; index += 1)
        {
            ObjectSection const* section = object->sections + index;
            u64 size = BUSTER_MAX(section->data.length, section->virtual_size);
            // TLS deliberately reaches the native JIT's explicit rejection.
            // Only host-owned explicit context state is admitted by this pilot.
            if (size && (section->kind == OBJECT_SECTION_DATA || section->kind == OBJECT_SECTION_ZERO ||
                         section->kind == OBJECT_SECTION_INIT_ARRAY || section->kind == OBJECT_SECTION_FINI_ARRAY))
            {
                result = PROBE_MODULE_STATE;
            }
            if (size && section->kind != OBJECT_SECTION_TEXT && section->kind != OBJECT_SECTION_READ_ONLY_DATA &&
                section->kind != OBJECT_SECTION_DATA && section->kind != OBJECT_SECTION_ZERO &&
                section->kind != OBJECT_SECTION_THREAD_LOCAL_DATA && section->kind != OBJECT_SECTION_THREAD_LOCAL_ZERO)
            {
                host->metadata_bytes += size;
            }
        }
    }
    if (result == PROBE_OK)
    {
        for (u32 index = 0; index < object->symbol_count; index += 1)
        {
            ObjectSymbol const* symbol = object->symbols + index;
            if (string_equal(symbol->name, entry_name) &&
                (symbol->kind != OBJECT_SYMBOL_FUNCTION || symbol->section == OBJECT_SECTION_UNDEFINED ||
                 symbol->section >= object->section_count || object->sections[symbol->section].kind != OBJECT_SECTION_TEXT))
            {
                result = PROBE_ENTRY_KIND;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProbeError probe_prepare(ProbeHost* host, ProbeVersion* version, char const* path, bool host_state)
{
    ProbeError result;
    if (version->state != PROBE_EMPTY && version->state != PROBE_RELEASED)
    {
        result = PROBE_BAD_TRANSITION;
    }
    else
    {
        memset(version, 0, sizeof(*version));
        version->state = PROBE_PREPARING;
        version->host_state = host_state;
        version->generation = ++host->next_generation;
        host->candidate_count += 1;
        version->arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(16), .flags = {.no_pool = true}});
        ByteSlice bytes = {0};
        u64 started = probe_stamp(host);
        result = probe_read(version, path, &bytes);
        host->read_ns += probe_stamp(host) - started;
        if (result == PROBE_OK)
        {
            started = probe_stamp(host);
            version->object = object_read(version->arena, bytes, target_native);
            result = version->object.error == OBJECT_ERROR_NONE ? probe_policy(host, version) : PROBE_OBJECT;
            host->parse_policy_ns += probe_stamp(host) - started;
        }
        if (result == PROBE_OK)
        {
            started = probe_stamp(host);
            JitHostBinding binding = {.name = S8("unavailable_host_function"), .kind = OBJECT_SYMBOL_FUNCTION};
            PilotFunction* imported = probe_host_import;
            BUSTER_CT_CHECK(sizeof(binding.address) == sizeof(imported));
            memcpy(&binding.address, &imported, sizeof(imported));
            JitOptions options = {0};
            if (host->bind_fixture_import)
            {
                options.bindings = &binding;
                options.binding_count = 1;
            }
            // The binding array is borrowed only for this call. Its name and
            // permanent host function survive every execution of the module.
            version->program = jit_link_object(&version->object, options);
            result = version->program.error == JIT_ERROR_NONE ? PROBE_OK : PROBE_JIT;
            if (result == PROBE_OK)
            {
                version->entry = jit_program_symbol(&version->program, host_state ? S8("pilot_step") : S8("pilot"));
                result = version->entry ? PROBE_OK : PROBE_JIT;
            }
            host->jit_lookup_ns += probe_stamp(host) - started;
            if (version->program.allocation_base)
            {
                host->mapped_count += 1;
                host->mapped_bytes += version->program.allocation_size + version->program.auxiliary_allocation_size;
            }
        }
        version->error = result;
        version->state = result == PROBE_OK ? PROBE_READY : PROBE_REJECTED;
        printf("CANDIDATE generation=%u result=%u object_error=%u jit_error=%u sections=%u symbols=%u relocations=%u\n",
               version->generation, (unsigned int)result, (unsigned int)version->object.error,
               (unsigned int)version->program.error, version->object.section_count,
               version->object.symbol_count, version->object.relocation_count);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProbeError probe_publish(ProbeHost* host, ProbeVersion* candidate, bool versioned)
{
    ProbeError result;
    u64 started = probe_stamp(host);
    if (candidate->state != PROBE_READY)
    {
        result = PROBE_BAD_TRANSITION;
    }
    else if (host->active && candidate->host_state != host->active->host_state)
    {
        result = PROBE_ABI;
    }
    else if (host->active && host->active->pins && !versioned)
    {
        result = PROBE_BUSY;
    }
    else
    {
        ProbeVersion* old = host->active;
        candidate->state = PROBE_ACTIVE;
        if (old)
        {
            old->state = PROBE_RETIRED;
        }
        // Exact serial publication point. No atomic/concurrent protocol is claimed.
        host->active = candidate;
        host->publication_count += 1;
        result = PROBE_OK;
    }
    host->publication_ns += probe_stamp(host) - started;
    return result;
}

BUSTER_GLOBAL_LOCAL ProbeLease probe_pin(ProbeHost* host)
{
    ProbeLease result = {0};
    if (host->active && host->active->state == PROBE_ACTIVE && host->active->entry)
    {
        result.version = host->active;
        result.version->pins += 1;
        BUSTER_CT_CHECK(sizeof(result.function) == sizeof(result.version->entry));
        BUSTER_CT_CHECK(sizeof(result.state_function) == sizeof(result.version->entry));
        if (result.version->host_state)
        {
            memcpy(&result.state_function, &result.version->entry, sizeof(result.state_function));
        }
        else
        {
            memcpy(&result.function, &result.version->entry, sizeof(result.function));
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProbeError probe_drop(ProbeLease* lease)
{
    ProbeError result;
    if (lease->version && lease->version->pins)
    {
        ProbeVersion* version = lease->version;
        // Eliminate this host's escaped callable before dropping its lifetime pin.
        memset(lease, 0, sizeof(*lease));
        version->pins -= 1;
        result = PROBE_OK;
    }
    else
    {
        result = PROBE_BAD_TRANSITION;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProbeError probe_reclaim(ProbeHost* host, ProbeVersion* version)
{
    ProbeError result;
    u64 started = probe_stamp(host);
    if (host->active == version || version->state == PROBE_ACTIVE || version->state == PROBE_PREPARING)
    {
        result = PROBE_BAD_TRANSITION;
    }
    else if (version->pins)
    {
        result = PROBE_BUSY;
    }
    else if (version->state == PROBE_EMPTY || version->state == PROBE_RELEASED)
    {
        result = PROBE_OK;
    }
    else
    {
        version->entry = 0;
        // Exact reclamation point: this mapping has no active selector or lease.
        jit_program_release(&version->program);
        version->state = PROBE_RELEASE_PENDING;
        bool released;
        if (host->reject_arena_release_once)
        {
            // Diagnostic failure injection retains the actual arena for retry.
            host->reject_arena_release_once = false;
            released = false;
        }
        else
        {
            released = arena_destroy(version->arena, 1);
        }
        if (released)
        {
            version->arena = 0;
            memset(&version->object, 0, sizeof(version->object));
            version->state = PROBE_RELEASED;
            host->reclamation_count += 1;
        }
        result = released ? PROBE_OK : PROBE_RELEASE_FAILED;
    }
    host->reclamation_ns += probe_stamp(host) - started;
    return result;
}

BUSTER_GLOBAL_LOCAL int probe_call(ProbeHost* host, ProbeLease const* lease, int value)
{
    u64 started = probe_stamp(host);
    // The only function-pointer call is this controlled application's typed JIT
    // boundary, matching the existing JIT tests. It is not compiler dispatch.
    int result = lease->function(value);
    host->call_ns += probe_stamp(host) - started;
    host->calls += 1;
    return result;
}

BUSTER_GLOBAL_LOCAL int probe_state_call(ProbeHost* host, ProbeLease const* lease, PilotState* state, int value)
{
    u64 started = probe_stamp(host);
    int result = lease->state_function(state, value);
    host->call_ns += probe_stamp(host) - started;
    host->calls += 1;
    return result;
}

BUSTER_GLOBAL_LOCAL void probe_shutdown(ProbeHost* host, ProbeVersion* versions)
{
    if (host->active)
    {
        host->active->state = PROBE_RETIRED;
        host->active = 0;
    }
    for (u32 index = 0; index < 2; index += 1)
    {
        probe_check(host, probe_reclaim(host, versions + index) == PROBE_OK, "shutdown_reclaim");
    }
}

BUSTER_GLOBAL_LOCAL void probe_experiment(ProbeHost* host, char** paths)
{
    ProbeVersion versions[2] = {0};
    ProbeError prepared = probe_prepare(host, versions, paths[0], false);
    probe_check(host, prepared == PROBE_OK, "initial_v1_prepared");
    if (prepared == PROBE_OK)
    {
        probe_check(host, probe_publish(host, versions, false) == PROBE_OK, "initial_v1_published");
        ProbeLease initial = probe_pin(host);
        probe_check(host, initial.function && probe_call(host, &initial, 7) == 8, "initial_v1_callable");
        probe_check(host, probe_drop(&initial) == PROBE_OK, "initial_lease_dropped");

        char const* failures[] = {"abi_change_rejected", "missing_entry_rejected", "missing_import_rejected", "tls_rejected"};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(failures); index += 1)
        {
            ProbeError rejected = probe_prepare(host, versions + 1, paths[index + 2], false);
            bool expected = index == 0 ? rejected == PROBE_ABI : rejected == PROBE_JIT;
            if (index == 1)
            {
                expected = expected && versions[1].program.error == JIT_ERROR_SYMBOL_NOT_FOUND;
            }
            if (index == 3)
            {
                expected = expected && versions[1].program.error == JIT_ERROR_TLS_UNSUPPORTED;
            }
            // Native object relocation behavior determines the import diagnostic;
            // print its real JitError rather than rewriting the emitted object.
            probe_check(host, expected, failures[index]);
            probe_check(host, probe_publish(host, versions + 1, false) == PROBE_BAD_TRANSITION, "rejected_candidate_not_published");
            ProbeLease preserved = probe_pin(host);
            probe_check(host, preserved.function && probe_call(host, &preserved, 7) == 8, "failure_preserves_v1");
            probe_check(host, probe_drop(&preserved) == PROBE_OK, "failure_lease_dropped");
            probe_check(host, probe_reclaim(host, versions + 1) == PROBE_OK, "failed_candidate_released");
        }

        prepared = probe_prepare(host, versions + 1, paths[6], true);
        probe_check(host, prepared == PROBE_OK, "different_abi_candidate_prepared");
        if (prepared == PROBE_OK)
        {
            probe_check(host, probe_publish(host, versions + 1, false) == PROBE_ABI, "prepared_different_abi_publish_rejected");
            ProbeLease preserved = probe_pin(host);
            probe_check(host, preserved.function && probe_call(host, &preserved, 7) == 8, "cross_abi_failure_preserves_v1");
            probe_check(host, probe_drop(&preserved) == PROBE_OK, "cross_abi_lease_dropped");
            Arena* retained = versions[1].arena;
            host->reject_arena_release_once = true;
            probe_check(host, probe_reclaim(host, versions + 1) == PROBE_RELEASE_FAILED &&
                        versions[1].arena == retained && versions[1].state == PROBE_RELEASE_PENDING,
                        "arena_release_failure_retains_owner");
            probe_check(host, probe_prepare(host, versions + 1, paths[1], false) == PROBE_BAD_TRANSITION,
                        "release_pending_slot_not_overwritten");
            probe_check(host, probe_publish(host, versions + 1, false) == PROBE_BAD_TRANSITION,
                        "release_pending_slot_not_published");
            probe_check(host, probe_reclaim(host, versions + 1) == PROBE_OK, "arena_release_retry_succeeds");
        }

        prepared = probe_prepare(host, versions + 1, paths[1], false);
        probe_check(host, prepared == PROBE_OK, "v2_prepared");
        if (prepared == PROBE_OK)
        {
            ProbeLease borrowed = probe_pin(host);
            probe_check(host, versions[0].program.allocation_base != versions[1].program.allocation_base, "separate_old_candidate_mappings");
            probe_check(host, probe_publish(host, versions + 1, false) == PROBE_BUSY, "quiescent_publish_rejects_pin");
            probe_check(host, borrowed.function && probe_call(host, &borrowed, 7) == 8, "old_borrow_survives_busy_rejection");
            probe_check(host, probe_reclaim(host, versions) == PROBE_BAD_TRANSITION, "active_mapping_not_reclaimed");
            probe_check(host, probe_drop(&borrowed) == PROBE_OK, "quiescent_borrow_dropped");
            probe_check(host, probe_publish(host, versions + 1, false) == PROBE_OK, "quiescent_v2_published");
            probe_check(host, probe_reclaim(host, versions) == PROBE_OK, "quiescent_v1_reclaimed");
            ProbeLease current = probe_pin(host);
            probe_check(host, current.function && probe_call(host, &current, 7) == 9, "v2_callable");
            probe_check(host, probe_drop(&current) == PROBE_OK, "v2_lease_dropped");

            // Separate versioned experiment: serial logical overlap only. A pin
            // models ownership by an active invocation or an escaped entry lease;
            // it does not prove simultaneous threads or changing a live frame.
            prepared = probe_prepare(host, versions, paths[0], false);
            probe_check(host, prepared == PROBE_OK, "versioned_v1_prepared");
            if (prepared == PROBE_OK)
            {
                ProbeLease old = probe_pin(host);
                probe_check(host, probe_publish(host, versions, true) == PROBE_OK, "versioned_publish_with_old_pin");
                ProbeLease next = probe_pin(host);
                probe_check(host, old.function && probe_call(host, &old, 7) == 9, "retired_v2_lease_callable");
                probe_check(host, next.function && probe_call(host, &next, 7) == 8, "versioned_new_v1_callable");
                probe_check(host, probe_reclaim(host, versions + 1) == PROBE_BUSY, "retired_mapping_held_by_old_pin");
                probe_check(host, probe_drop(&old) == PROBE_OK, "last_old_lease_dropped");
                probe_check(host, probe_reclaim(host, versions + 1) == PROBE_OK, "versioned_old_reclaimed_after_last_pin");
                probe_check(host, probe_drop(&next) == PROBE_OK, "versioned_new_lease_dropped");
            }
        }
    }
    probe_shutdown(host, versions);

    // First stateful extension: caller-owned fixed-layout context, no migration.
    PilotState state = {0};
    prepared = probe_prepare(host, versions, paths[6], true);
    probe_check(host, prepared == PROBE_OK, "state_v1_prepared");
    if (prepared == PROBE_OK)
    {
        probe_check(host, probe_publish(host, versions, false) == PROBE_OK, "state_v1_published");
        ProbeLease first = probe_pin(host);
        probe_check(host, first.state_function && probe_state_call(host, &first, &state, 7) == 7 && state.calls == 1,
                    "host_owned_context_updated_by_v1");
        probe_check(host, probe_drop(&first) == PROBE_OK, "state_v1_lease_dropped");
        prepared = probe_prepare(host, versions + 1, paths[0], true);
        probe_check(host, prepared == PROBE_ABI, "stateless_to_stateful_abi_change_rejected");
        probe_check(host, state.total == 7 && state.calls == 1 && host->active == versions, "failed_state_candidate_preserves_host_context");
        probe_check(host, probe_reclaim(host, versions + 1) == PROBE_OK, "bad_state_candidate_released");
        prepared = probe_prepare(host, versions + 1, paths[7], true);
        probe_check(host, prepared == PROBE_OK, "state_v2_prepared");
        if (prepared == PROBE_OK)
        {
            probe_check(host, probe_publish(host, versions + 1, false) == PROBE_OK, "state_v2_published");
            probe_check(host, probe_reclaim(host, versions) == PROBE_OK, "state_v1_reclaimed");
            ProbeLease second = probe_pin(host);
            probe_check(host, second.state_function && probe_state_call(host, &second, &state, 7) == 21 && state.calls == 2,
                        "host_owned_context_retained_across_replacement");
            probe_check(host, probe_drop(&second) == PROBE_OK, "state_v2_lease_dropped");
        }
    }
    probe_shutdown(host, versions);

    // Verify the ordinary serialized-object import path with a permanent host
    // target. The ELF reader normalizes PLT32 to the JIT's PC32 representation.
    host->bind_fixture_import = true;
    prepared = probe_prepare(host, versions, paths[4], false);
    host->bind_fixture_import = false;
    probe_check(host, prepared == PROBE_OK, "explicit_host_import_prepared");
    if (prepared == PROBE_OK)
    {
        probe_check(host, probe_publish(host, versions, false) == PROBE_OK, "explicit_host_import_published");
        ProbeLease imported = probe_pin(host);
        probe_check(host, imported.function && probe_call(host, &imported, 7) == 24, "explicit_host_import_callable");
        probe_check(host, probe_drop(&imported) == PROBE_OK, "explicit_host_import_lease_dropped");
    }
    probe_shutdown(host, versions);
}

int main(int argc, char** argv)
{
    ProbeHost host = {0};
    int path_start = 1;
    if (argc > 1 && strcmp(argv[1], "--timings") == 0)
    {
        host.timings = true;
        path_start += 1;
    }
    int result;
    if (argc - path_start != 8)
    {
        fprintf(stderr, "usage: %s [--timings] v1.o v2.o bad_abi.o missing_entry.o missing_import.o tls.o state_v1.o state_v2.o\n", argv[0]);
        result = 2;
    }
    else
    {
        cpu_detect_model();
        ThreadContext* context = thread_context_allocate();
        thread_context_select(context);
        probe_experiment(&host, argv + path_start);
        printf("COUNTS checks=%u failures=%u candidates=%llu maps=%llu cumulative_mapped_bytes=%llu sections=%llu symbols=%llu relocations=%llu metadata_bytes_unregistered=%llu publications=%llu reclamations=%llu calls=%llu\n",
               host.checks, host.failures, (unsigned long long)host.candidate_count, (unsigned long long)host.mapped_count,
               (unsigned long long)host.mapped_bytes, (unsigned long long)host.section_count, (unsigned long long)host.symbol_count,
               (unsigned long long)host.relocation_count, (unsigned long long)host.metadata_bytes,
               (unsigned long long)host.publication_count, (unsigned long long)host.reclamation_count,
               (unsigned long long)host.calls);
        if (host.timings)
        {
            printf("DIAGNOSTIC_NS read=%llu object_parse_policy=%llu jit_load_finalize_lookup=%llu publish_attempts=%llu reclamation_attempts=%llu ordinary_calls=%llu compile=not_measured quiescent_wait=not_measured\n",
                   (unsigned long long)host.read_ns, (unsigned long long)host.parse_policy_ns,
                   (unsigned long long)host.jit_lookup_ns, (unsigned long long)host.publication_ns,
                   (unsigned long long)host.reclamation_ns, (unsigned long long)host.call_ns);
        }
        thread_context_release(context);
        arena_pool_release_thread();
        result = host.failures ? 1 : 0;
    }
    return result;
}
