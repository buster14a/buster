// Controlled application's quiescent subset of #1926's probe at b6bc250d.
// Ownership: two stable slots own object arenas, JitPrograms and typed leases.
// Entry: main. Map: probe_prepare/publish/pin/drop/reclaim, probe_rebuild,
// probe_interactive. No application dispatch enters compiler internals.
// All operations and the entire dynamic extent of a module call are serial.

#include <buster/lib/base.h>
#include <buster/lib/arena.h>
#include <buster/lib/compiler/jit/jit.h>
#include <buster/lib/string.h>
#include <buster/lib/system_headers.h>
#include <buster/lib/target.h>
#include "pilot_contract.h"
#include <stdio.h>

#if !BUSTER_LINUX || !BUSTER_CPU_ARCH_X86_64
#error This controlled hot-reload application supports Linux x86-64 only.
#endif

BUSTER_GLOBAL_LOCAL ProgramState probe_program_state;
BUSTER_V_IMPL ProgramState* program_state = &probe_program_state;
BUSTER_V_IMPL OsState os_state;


#define PROBE_PATH_CAPACITY 4096
#define PROBE_OBJECT_LIMIT BUSTER_MB(1)
#define PROBE_ARENA_LIMIT BUSTER_MB(16)
#define PROBE_COMPILE_TIMEOUT_US UINT64_C(60000000)

typedef enum ProbeState
{
    PROBE_EMPTY, PROBE_PREPARING, PROBE_READY, PROBE_ACTIVE,
    PROBE_RETIRED, PROBE_REJECTED, PROBE_RELEASE_PENDING, PROBE_RELEASED,
} ProbeState;

typedef enum ProbeError
{
    PROBE_OK, PROBE_IO, PROBE_OBJECT, PROBE_ABI, PROBE_MODULE_STATE,
    PROBE_ENTRY_KIND, PROBE_JIT, PROBE_BUSY, PROBE_BAD_TRANSITION,
    PROBE_RELEASE_FAILED, PROBE_COMPILE,
} ProbeError;

typedef struct ProbeVersion ProbeVersion;
struct ProbeVersion
{
    Arena* arena;
    ObjectFile object;
    JitProgram program;
    void* entry;
    ProbeState state;
    u32 pins;
    u32 generation;
};

typedef struct ProbeLease ProbeLease;
struct ProbeLease
{
    ProbeVersion* version;
    PilotStateFunction* function;
};

typedef struct ProbeHost ProbeHost;
struct ProbeHost
{
    ProbeVersion* active;
    ProbeVersion* import_candidate;
    PilotState state;
    u32 next_generation;
    u32 checks;
    u32 failures;
    u32 live_maps;
    u32 peak_maps;
    u64 live_bytes;
    bool reject_arena_release_once;
    bool test_import_safe_point;
    bool compile_blocked;
};

BUSTER_GLOBAL_LOCAL ProbeHost* probe_import_host;
BUSTER_GLOBAL_LOCAL ProbeError probe_publish(ProbeHost* host, ProbeVersion* candidate);
BUSTER_GLOBAL_LOCAL ProbeError probe_reclaim(ProbeHost* host, ProbeVersion* version);

BUSTER_GLOBAL_LOCAL void probe_check(ProbeHost* host, bool valid, char const* name)
{
    host->checks += 1;
    host->failures += !valid;
    printf("CHECK %s %s\n", valid ? "PASS" : "FAIL", name);
}

BUSTER_GLOBAL_LOCAL unsigned long long probe_host_delta(unsigned long long value)
{
    if (probe_import_host->test_import_safe_point)
    {
        // The module's return address is live even though execution is in host
        // code. Both replacement and reclamation must refuse this safe point.
        probe_check(probe_import_host, probe_publish(probe_import_host, probe_import_host->import_candidate) == PROBE_BUSY,
                    "host_import_cannot_publish_while_module_return_is_live");
        probe_check(probe_import_host, probe_reclaim(probe_import_host, probe_import_host->active) == PROBE_BAD_TRANSITION,
                    "host_import_cannot_reclaim_active_return_address");
    }
    return value;
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
            if (size > 0 && (u64)size <= PROBE_OBJECT_LIMIT && fseek(file, 0, SEEK_SET) == 0)
            {
                bytes->pointer = arena_allocate(version->arena, u8, (u64)size);
                bytes->length = (u64)size;
                if (fread(bytes->pointer, 1, (size_t)size, file) == (size_t)size && fgetc(file) == EOF && !ferror(file))
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

BUSTER_GLOBAL_LOCAL ProbeError probe_policy(ProbeVersion* version)
{
    ProbeError result = PROBE_ABI;
    u64 const expected[PILOT_DESCRIPTOR_WORDS] = {PILOT_DESCRIPTOR_VALUES};
    ObjectFile const* object = &version->object;
    u32 descriptors = 0;
    u32 entries = 0;
    for (u32 index = 0; index < object->symbol_count; index += 1)
    {
        ObjectSymbol const* symbol = object->symbols + index;
        if (string_equal(symbol->name, S8("pilot_descriptor")))
        {
            descriptors += 1;
            if (symbol->kind == OBJECT_SYMBOL_DATA && symbol->section < object->section_count)
            {
                ObjectSection const* section = object->sections + symbol->section;
                if (section->kind == OBJECT_SECTION_READ_ONLY_DATA && symbol->size == sizeof(expected) &&
                    symbol->value <= section->data.length && sizeof(expected) <= section->data.length - symbol->value &&
                    !memcmp(section->data.pointer + symbol->value, expected, sizeof(expected)))
                {
                    result = PROBE_OK;
                    // A descriptor is pointer-free data. Never accept a value
                    // that later relocation could turn into another contract.
                    for (u32 r = 0; r < object->relocation_count; r += 1)
                    {
                        ObjectRelocation const* relocation = object->relocations + r;
                        u64 width = object_relocation_kind_width(relocation->kind);
                        if (relocation->section == symbol->section && relocation->offset < symbol->value + sizeof(expected) &&
                            (relocation->offset >= symbol->value || width > symbol->value - relocation->offset))
                        {
                            result = PROBE_ABI;
                        }
                    }
                }
            }
        }
        if (string_equal(symbol->name, S8("pilot_step")))
        {
            entries += 1;
        }
    }
    if (descriptors != 1)
    {
        result = PROBE_ABI;
    }
    if (result == PROBE_OK)
    {
        result = entries == 1 ? PROBE_OK : PROBE_ENTRY_KIND;
        for (u32 index = 0; index < object->symbol_count; index += 1)
        {
            ObjectSymbol const* symbol = object->symbols + index;
            if (string_equal(symbol->name, S8("pilot_step")) &&
                (symbol->kind != OBJECT_SYMBOL_FUNCTION || symbol->section >= object->section_count ||
                 object->sections[symbol->section].kind != OBJECT_SECTION_TEXT || !symbol->size))
            {
                result = PROBE_ENTRY_KIND;
            }
        }
        for (u32 index = 0; index < object->section_count; index += 1)
        {
            ObjectSection const* section = object->sections + index;
            u64 size = BUSTER_MAX(section->data.length, section->virtual_size);
            if (size && (section->kind == OBJECT_SECTION_DATA || section->kind == OBJECT_SECTION_ZERO ||
                         section->kind == OBJECT_SECTION_INIT_ARRAY || section->kind == OBJECT_SECTION_FINI_ARRAY))
            {
                result = PROBE_MODULE_STATE;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProbeError probe_prepare(ProbeHost* host, ProbeVersion* version, char const* path)
{
    ProbeError result;
    if (version->state != PROBE_EMPTY && version->state != PROBE_RELEASED)
    {
        result = PROBE_BAD_TRANSITION;
    }
    else
    {
        *version = (ProbeVersion){.state = PROBE_PREPARING, .generation = ++host->next_generation};
        version->arena = arena_create((ArenaCreation){.reserved_size = PROBE_ARENA_LIMIT, .flags = {.no_pool = true}});
        ByteSlice bytes = {0};
        result = probe_read(version, path, &bytes);
        if (result == PROBE_OK)
        {
            version->object = object_read(version->arena, bytes, target_native);
            result = version->object.error == OBJECT_ERROR_NONE ? probe_policy(version) : PROBE_OBJECT;
        }
        if (result == PROBE_OK)
        {
            JitHostBinding binding = {.name = S8("pilot_host_delta"), .kind = OBJECT_SYMBOL_FUNCTION};
            PilotImportFunction* imported = probe_host_delta;
            BUSTER_CT_CHECK(sizeof(binding.address) == sizeof(imported));
            memcpy(&binding.address, &imported, sizeof(imported));
            version->program = jit_link_object(&version->object, (JitOptions){.bindings = &binding, .binding_count = 1});
            result = version->program.error == JIT_ERROR_NONE ? PROBE_OK : PROBE_JIT;
            if (version->program.allocation_base)
            {
                host->live_maps += 1;
                host->peak_maps = BUSTER_MAX(host->peak_maps, host->live_maps);
                host->live_bytes += version->program.allocation_size;
            }
            if (result == PROBE_OK)
            {
                version->entry = jit_program_symbol(&version->program, S8("pilot_step"));
                result = version->entry ? PROBE_OK : PROBE_JIT;
            }
        }
        version->state = result == PROBE_OK ? PROBE_READY : PROBE_REJECTED;
        printf("CANDIDATE generation=%u result=%u object_error=%u jit_error=%u\n",
               version->generation, (unsigned)result, (unsigned)version->object.error, (unsigned)version->program.error);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProbeError probe_publish(ProbeHost* host, ProbeVersion* candidate)
{
    ProbeError result;
    if (candidate->state != PROBE_READY)
    {
        result = PROBE_BAD_TRANSITION;
    }
    else if (host->active && host->active->pins)
    {
        result = PROBE_BUSY;
    }
    else
    {
        if (host->active)
        {
            host->active->state = PROBE_RETIRED;
        }
        candidate->state = PROBE_ACTIVE;
        host->active = candidate;
        result = PROBE_OK;
    }
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
        memcpy(&result.function, &result.version->entry, sizeof(result.function));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProbeError probe_drop(ProbeLease* lease)
{
    ProbeError result;
    if (lease->version && lease->version->pins)
    {
        ProbeVersion* version = lease->version;
        *lease = (ProbeLease){0};
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
        if (version->program.allocation_base)
        {
            host->live_maps -= 1;
            host->live_bytes -= version->program.allocation_size;
        }
        jit_program_release(&version->program);
        version->state = PROBE_RELEASE_PENDING;
        bool released;
        if (host->reject_arena_release_once)
        {
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
            version->object = (ObjectFile){0};
            version->state = PROBE_RELEASED;
        }
        result = released ? PROBE_OK : PROBE_RELEASE_FAILED;
    }
    return result;
}

// Clang function UBSan probes metadata before an indirect target; Buster JIT
// entry points carry no such host-compiler prefix. Keep every other sanitizer.
#if BUSTER_COMPILER_CLANG
__attribute__((no_sanitize("function")))
#endif
BUSTER_GLOBAL_LOCAL unsigned long long probe_call(ProbeLease const* lease, PilotState* state)
{
    return lease->function(state);
}

BUSTER_GLOBAL_LOCAL bool probe_step(ProbeHost* host)
{
    ProbeLease lease = probe_pin(host);
    bool result = lease.function != 0;
    if (result)
    {
        unsigned long long observed = probe_call(&lease, &host->state);
        printf("COUNTER generation=%u total=%llu calls=%llu result=%llu\n",
               lease.version->generation, host->state.total, host->state.calls, observed);
        result = observed == host->state.total;
        result = probe_drop(&lease) == PROBE_OK && result;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProbeError probe_compile(ProbeHost* host, char const* compiler, char const* source, char const* output)
{
    // Remove only our private output; a failed compiler must not load stale code.
    ProbeError result = PROBE_COMPILE;
    if (!host->compile_blocked && (unlink(output) == 0 || errno == ENOENT))
    {
        TemporalArena scratch = scratch_begin(0, 0);
        String8 arguments[] = {string_from_pointer_length((char8 const*)compiler, strlen(compiler)), S8("cc"), S8("-g0"), S8("-fverify-codegen"),
            S8("-fregister-allocator=mir-stack"), S8("-fno-machine-fallback"), S8("-target"),
            S8("x86_64-unknown-linux"), S8("-c"), string_from_pointer_length((char8 const*)source, strlen(source)), S8("-o"),
            string_from_pointer_length((char8 const*)output, strlen(output))};
        ProcessSpawnResult spawn = os_process_spawn((SliceString8){.pointer = arguments, .length = BUSTER_ARRAY_LENGTH(arguments)},
            (SliceString8){0}, (SliceString8){0}, (ProcessSpawnOptions){
                .capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR), .new_process_group = true});
        if (spawn.handle)
        {
            ProcessWaitResult wait = os_process_wait_deadline(scratch.arena, spawn, PROBE_COMPILE_TIMEOUT_US);
            host->compile_blocked = wait.process_tree_cleanup_failed || wait.process_group_reservation_retained ||
                wait.process_group_ownership_lost;
            bool completed = wait.result == PROCESS_RESULT_SUCCESS && !wait.timed_out && !wait.capture_failed &&
                !wait.capture_limit_exceeded && !host->compile_blocked;
            if (completed)
            {
                result = PROBE_OK;
            }
            else
            {
                printf("COMPILE_FAILED status=%u timeout=%u capture_failed=%u cleanup_failed=%u\n",
                    wait.platform_status, wait.timed_out, wait.capture_failed, wait.process_tree_cleanup_failed);
                for (u32 index = STANDARD_STREAM_OUTPUT; index <= STANDARD_STREAM_ERROR; index += 1)
                {
                    ByteSlice bytes = wait.streams[index];
                    if (bytes.length)
                    {
                        fwrite(bytes.pointer, 1, (size_t)bytes.length, stderr);
                    }
                }
            }
        }
        else
        {
            printf("COMPILE_SPAWN_FAILED stage=%u error=%llu\n", (unsigned)spawn.failure, (unsigned long long)spawn.error.v);
        }
        scratch_end(scratch);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProbeError probe_replace(ProbeHost* host, ProbeVersion* versions, char const* object_path)
{
    ProbeVersion* candidate = host->active == versions ? versions + 1 : versions;
    ProbeError result = probe_reclaim(host, candidate);
    if (result == PROBE_OK)
    {
        result = probe_prepare(host, candidate, object_path);
        if (result == PROBE_OK)
        {
            ProbeVersion* old = host->active;
            result = probe_publish(host, candidate);
            if (result == PROBE_OK && old)
            {
                result = probe_reclaim(host, old);
            }
        }
        if (candidate != host->active)
        {
            ProbeError cleanup = probe_reclaim(host, candidate);
            if (cleanup != PROBE_OK)
            {
                result = cleanup;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProbeError probe_rebuild(ProbeHost* host, ProbeVersion* versions, char const* compiler,
                                             char const* source, char const* output)
{
    ProbeError result;
    if (host->active && host->active->pins)
    {
        result = PROBE_BUSY;
    }
    else
    {
        result = probe_compile(host, compiler, source, output);
        if (result == PROBE_OK)
        {
            result = probe_replace(host, versions, output);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProbeError probe_shutdown(ProbeHost* host, ProbeVersion* versions)
{
    ProbeError result;
    if (host->active && host->active->pins)
    {
        result = PROBE_BUSY;
    }
    else
    {
        if (host->active)
        {
            host->active->state = PROBE_RETIRED;
            host->active = 0;
        }
        ProbeError first = probe_reclaim(host, versions);
        ProbeError second = probe_reclaim(host, versions + 1);
        result = first != PROBE_OK ? first : second;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool probe_interactive(ProbeHost* host, char const* compiler, char const* source, char const* output, FILE* input)
{
    ProbeVersion versions[2] = {0};
    bool result = probe_rebuild(host, versions, compiler, source, output) == PROBE_OK;
    if (result)
    {
        printf("Commands: s = step, r = rebuild and reload saved source, q = quit\n");
        char command[16];
        bool done = false;
        while (!done && fgets(command, sizeof(command), input))
        {
            if (!strchr(command, '\n') && !feof(input))
            {
                int byte;
                do { byte = fgetc(input); } while (byte != '\n' && byte != EOF);
                printf("Expected s, r, or q\n");
            }
            else if (!strcmp(command, "s\n") || !strcmp(command, "s"))
            {
                result = probe_step(host) && result;
            }
            else if (!strcmp(command, "r\n") || !strcmp(command, "r"))
            {
                ProbeError reload = probe_rebuild(host, versions, compiler, source, output);
                printf("RELOAD result=%u total=%llu calls=%llu\n", (unsigned)reload, host->state.total, host->state.calls);
            }
            else if (!strcmp(command, "q\n") || !strcmp(command, "q"))
            {
                done = true;
            }
            else
            {
                printf("Expected s, r, or q\n");
            }
        }
    }
    result = probe_shutdown(host, versions) == PROBE_OK && result;
    return result;
}

#include "lifecycle_test.h"

int main(int argc, char** argv)
{
    int result;
    if (argc != 3 || strlen(argv[1]) >= PROBE_PATH_CAPACITY || strlen(argv[2]) >= PROBE_PATH_CAPACITY)
    {
        fprintf(stderr, "usage: %s --self-test /absolute/path/ide\n       %s /absolute/path/ide source.c\n", argv[0], argv[0]);
        result = 2;
    }
    else
    {
        char directory[] = "/tmp/buster-hot-reload-XXXXXX";
        char* owned = mkdtemp(directory);
        if (!owned)
        {
            result = 1;
        }
        else
        {
            cpu_detect_model();
            ThreadContext* context = thread_context_allocate();
            thread_context_select(context);
            ProbeHost host = {0};
            probe_import_host = &host;
            char output[PROBE_PATH_CAPACITY];
            snprintf(output, sizeof(output), "%s/candidate.o", owned);
            bool self_test = !strcmp(argv[1], "--self-test");
            bool success = self_test ? probe_lifecycle_test(&host, argv[2], owned, output) :
                probe_interactive(&host, argv[1], argv[2], output, stdin);
            if (!host.compile_blocked)
            {
                unlink(output);
                success = rmdir(owned) == 0 && success;
            }
            else
            {
                printf("COMPILER_ADMISSION_STOPPED workspace_retained=%s\n", owned);
                success = false;
            }
            printf("HOT_RELOAD_RESULT checks=%u failures=%u live_maps=%u live_bytes=%llu peak_maps=%u cleanup=%s\n",
                host.checks, host.failures, host.live_maps, (unsigned long long)host.live_bytes, host.peak_maps,
                success ? "ok" : "failed");
            probe_import_host = 0;
            thread_context_release(context);
            arena_pool_release_thread();
            result = success && !host.failures && !host.live_maps && !host.live_bytes ? 0 : 1;
        }
    }
    return result;
}
