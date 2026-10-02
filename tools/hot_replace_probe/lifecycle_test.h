// Direct lifecycle tests for the controlled host; included only by host.c.
// Sources are first-party inline fixtures. Expected counter values are literal
// independent arithmetic. Linux /proc maps checks observe actual protections
// and unmapping, rather than treating the host's map counters as an oracle.

BUSTER_GLOBAL_LOCAL bool probe_test_write(char const* path, char const* source)
{
    FILE* file = fopen(path, "wb");
    bool result = false;
    if (file)
    {
        size_t length = strlen(source);
        result = fwrite(source, 1, length, file) == length;
        result = fclose(file) == 0 && result;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool probe_test_mapping(void* base, u64 size, bool present, bool executable)
{
    FILE* file = fopen("/proc/self/maps", "r");
    bool result = false;
    if (file)
    {
        u64 address = (u64)(uintptr_t)base;
        bool overlap = false;
        bool exact_permissions = false;
        char line[1024];
        while (fgets(line, sizeof(line), file))
        {
            unsigned long long begin;
            unsigned long long end;
            char permissions[5];
            if (sscanf(line, "%llx-%llx %4s", &begin, &end, permissions) == 3 &&
                begin < address + size && end > address)
            {
                overlap = true;
                if (begin <= address && end >= address + size && permissions[0] == 'r' &&
                    permissions[1] == '-' && permissions[2] == (executable ? 'x' : '-'))
                {
                    exact_permissions = true;
                }
            }
        }
        result = !ferror(file) && (present ? overlap && exact_permissions : !overlap);
        result = fclose(file) == 0 && result;
    }
    return result;
}

#define PROBE_TEST_DESCRIPTOR "unsigned long long const pilot_descriptor[7] = {PILOT_DESCRIPTOR_VALUES};\n"
#define PROBE_TEST_SOURCE_HEAD "#include \"pilot_contract.h\"\n"
#define PROBE_TEST_STEP_ONE "unsigned long long pilot_step(PilotState* s) { s->total += pilot_host_delta(1); s->calls += 1; return s->total; }\n"
#define PROBE_TEST_STEP_TWO "unsigned long long pilot_step(PilotState* s) { s->total += pilot_host_delta(2); s->calls += 1; return s->total; }\n"

BUSTER_GLOBAL_LOCAL bool probe_test_source(char const* path, char const* body)
{
    // The generated file lives in the private workspace. The contract remains
    // the authoritative repository header; compilation runs from repo root.
    FILE* file = fopen(path, "wb");
    bool result = false;
    if (file)
    {
        char const* include = "#include \"";
        char cwd[PROBE_PATH_CAPACITY];
        char const* suffix = "/tools/hot_replace_probe/pilot_contract.h\"\n";
        result = getcwd(cwd, sizeof(cwd)) != 0;
        if (result)
        {
            result = fputs(include, file) >= 0 && fputs(cwd, file) >= 0 &&
                fputs(suffix, file) >= 0 && fputs(body, file) >= 0;
        }
        result = fclose(file) == 0 && result;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool probe_lifecycle_test(ProbeHost* host, char const* compiler, char const* owned, char const* output)
{
    char source[PROBE_PATH_CAPACITY];
    snprintf(source, sizeof(source), "%s/module.c", owned);
    ProbeVersion versions[2] = {0};
    bool written = probe_test_source(source, PROBE_TEST_DESCRIPTOR PROBE_TEST_STEP_ONE);
    probe_check(host, written, "initial_source_written");
    ProbeError first = written ? probe_rebuild(host, versions, compiler, source, output) : PROBE_IO;
    probe_check(host, first == PROBE_OK, "buster_compile_load_publish_v1");
    if (first == PROBE_OK)
    {
        probe_check(host, probe_step(host) && host->state.total == 1 && host->state.calls == 1, "v1_counter_oracle");
        probe_check(host, probe_test_mapping(host->active->entry, 1, true, true), "code_is_read_execute_not_write");
        for (u32 s = 0; s < host->active->object.section_count; s += 1)
        {
            if (host->active->object.sections[s].kind == OBJECT_SECTION_READ_ONLY_DATA &&
                host->active->program.section_sizes[s])
            {
                probe_check(host, probe_test_mapping(host->active->program.section_addresses[s],
                    host->active->program.section_sizes[s], true, false), "descriptor_mapping_is_read_only");
            }
        }

        ProbeVersion* old = host->active;
        written = probe_test_source(source, PROBE_TEST_DESCRIPTOR PROBE_TEST_STEP_TWO);
        probe_check(host, written && probe_compile(compiler, source, output) == PROBE_OK, "v2_buster_compiled");
        ProbeVersion* candidate = versions + 1;
        ProbeError prepared = probe_prepare(host, candidate, output);
        probe_check(host, prepared == PROBE_OK, "v2_prepared_separately");
        if (prepared == PROBE_OK)
        {
            ProbeLease held = probe_pin(host);
            probe_check(host, probe_publish(host, candidate) == PROBE_BUSY && host->active == old, "outstanding_lease_blocks_reload");
            probe_check(host, probe_shutdown(host, versions) == PROBE_BUSY && host->active == old, "outstanding_lease_blocks_shutdown");
            probe_check(host, probe_rebuild(host, versions, "/absent/compiler", source, output) == PROBE_BUSY, "busy_rebuild_does_not_spawn_compiler");
            probe_check(host, held.function && held.function(&host->state) == 2 && host->state.calls == 2, "old_lease_remains_callable");
            probe_check(host, probe_drop(&held) == PROBE_OK && !held.function && !held.version, "drop_clears_borrow_before_release");
            probe_check(host, probe_drop(&held) == PROBE_BAD_TRANSITION, "double_drop_rejected");

            host->import_candidate = candidate;
            host->test_import_safe_point = true;
            probe_check(host, probe_step(host) && host->state.total == 3 && host->state.calls == 3, "active_dynamic_extent_includes_host_import");
            host->test_import_safe_point = false;
            host->import_candidate = 0;
            void* old_base = old->program.allocation_base;
            u64 old_size = old->program.allocation_size;
            probe_check(host, probe_publish(host, candidate) == PROBE_OK, "quiescent_v2_publication");
            probe_check(host, probe_reclaim(host, old) == PROBE_OK, "old_code_then_object_owner_reclaimed");
            probe_check(host, probe_test_mapping(old_base, old_size, false, false), "old_mapping_independently_unmapped");
            probe_check(host, probe_step(host) && host->state.total == 5 && host->state.calls == 4, "v2_preserves_counter_and_changes_behavior");

            char const* bad_sources[] = {
                "this is not C;\n",
                "#undef PILOT_ABI_VERSION\n#define PILOT_ABI_VERSION 2ULL\n" PROBE_TEST_DESCRIPTOR PROBE_TEST_STEP_ONE,
                "#undef PILOT_STATE_VERSION\n#define PILOT_STATE_VERSION 2ULL\n" PROBE_TEST_DESCRIPTOR PROBE_TEST_STEP_ONE,
                "unsigned long long const pilot_descriptor[7] = {0x4255535445520002ULL,1,1,24,8,0,8};\n" PROBE_TEST_STEP_ONE,
                "unsigned long long const pilot_descriptor[7] = {0x4255535445520002ULL,1,1,16,8,8,0};\n" PROBE_TEST_STEP_ONE,
                PROBE_TEST_DESCRIPTOR "int module_global = 1;\n" PROBE_TEST_STEP_ONE,
                PROBE_TEST_DESCRIPTOR "int missing_entry(void) { return 1; }\n",
                PROBE_TEST_DESCRIPTOR "extern unsigned long long missing_import(void);\n"
                    "unsigned long long pilot_step(PilotState* s) { s->calls += 1; return missing_import(); }\n",
                PROBE_TEST_DESCRIPTOR "_Thread_local unsigned long long module_tls;\n"
                    "unsigned long long pilot_step(PilotState* s) { s->calls += 1; return ++module_tls; }\n",
            };
            ProbeError const errors[] = {PROBE_COMPILE, PROBE_ABI, PROBE_ABI, PROBE_ABI, PROBE_ABI,
                PROBE_MODULE_STATE, PROBE_ENTRY_KIND, PROBE_JIT, PROBE_JIT};
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(bad_sources); index += 1)
            {
                ProbeVersion* incumbent = host->active;
                PilotState before = host->state;
                written = probe_test_source(source, bad_sources[index]);
                ProbeError rejected = written ? probe_rebuild(host, versions, compiler, source, output) : PROBE_IO;
                probe_check(host, rejected == errors[index], "bad_reload_rejected_with_expected_class");
                probe_check(host, host->active == incumbent && host->state.total == before.total &&
                    host->state.calls == before.calls && host->live_maps == 1, "failure_preserves_selector_state_and_resource_bound");
                probe_check(host, probe_step(host) && host->state.total == before.total + 2 &&
                    host->state.calls == before.calls + 1, "failure_preserves_actual_old_execution");
            }
            probe_check(host, probe_test_write(output, "invalid object"), "invalid_object_written");
            probe_check(host, probe_replace(host, versions, output) == PROBE_OBJECT && host->live_maps == 1, "malformed_object_preserves_incumbent");
            unlink(output);
            probe_check(host, probe_replace(host, versions, output) == PROBE_IO, "absent_object_rejected");
            written = probe_test_source(source, PROBE_TEST_DESCRIPTOR PROBE_TEST_STEP_ONE);
            probe_check(host, written && probe_rebuild(host, versions, "/absent/compiler", source, output) == PROBE_COMPILE,
                "missing_compiler_preserves_incumbent");
            for (u32 index = 0; index < 12; index += 1)
            {
                PilotState before = host->state;
                unsigned long long delta = (index & 1) ? 2 : 1;
                written = probe_test_source(source, (index & 1) ?
                    PROBE_TEST_DESCRIPTOR PROBE_TEST_STEP_TWO : PROBE_TEST_DESCRIPTOR PROBE_TEST_STEP_ONE);
                probe_check(host, written && probe_rebuild(host, versions, compiler, source, output) == PROBE_OK &&
                    host->live_maps == 1 && host->peak_maps <= 2, "repeated_edit_rebuild_reload_bounded_to_two_slots");
                probe_check(host, probe_step(host) && host->state.total == before.total + delta &&
                    host->state.calls == before.calls + 1, "repeated_reload_independent_counter_oracle");
            }
            ProbeVersion* inactive = host->active == versions ? versions + 1 : versions;
            prepared = probe_prepare(host, inactive, output);
            probe_check(host, prepared == PROBE_OK, "cleanup_failure_candidate_prepared");
            if (prepared == PROBE_OK)
            {
                Arena* retained = inactive->arena;
                host->reject_arena_release_once = true;
                probe_check(host, probe_reclaim(host, inactive) == PROBE_RELEASE_FAILED &&
                    inactive->arena == retained && inactive->state == PROBE_RELEASE_PENDING,
                    "arena_release_failure_retains_owner");
                probe_check(host, probe_prepare(host, inactive, output) == PROBE_BAD_TRANSITION &&
                    probe_publish(host, inactive) == PROBE_BAD_TRANSITION, "release_pending_slot_cannot_be_reused_or_published");
                probe_check(host, probe_reclaim(host, inactive) == PROBE_OK, "arena_release_retry");
            }
        }
    }
    probe_check(host, probe_shutdown(host, versions) == PROBE_OK && !host->live_maps && !host->live_bytes, "shutdown_releases_both_slots");
    probe_check(host, probe_shutdown(host, versions) == PROBE_OK, "shutdown_is_idempotent");
    probe_check(host, unlink(source) == 0, "private_source_removed");
    return !host->failures;
}
