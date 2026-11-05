// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Mixed-input commands (#1120): the unit scheduler keeps C and assembly objects in memory, then the ordinary foreign
// resolver consumes them beside files and archives in argv order. Pure-C commands retain their direct link path.

static int check_driver_assembly(const CheckDriverCommand* command, const PPOptions* preprocessing, CheckUnitsUnit* unit,
                                 CheckUnitsLane* lane, FILE* errors)
{
    PPOptions options = *preprocessing;
    // The C lane's file cache was configured for C tokenization, not assembly punctuation. The preprocessor owns
    // this unit's files instead; its intern table and Preprocessor are the lane's, never the standalone adapter's statics.
    options.files = NULL;
    options.semantic_pragmas = false;
    const bool preprocess = unit->kind == CHECK_DRIVER_INPUT_ASSEMBLY_CPP;
    const char* output = command->output != NULL ? command->output : unit->object;
    String8 source = { 0 };
    bool ready = true;
    if (preprocess)
    {
        ready = asm_preprocess_unit(unit->path, options, &lane->arena, &source, &lane->identifiers, &lane->pp,
                                    &unit->dependency, &lane->kept, command->output, errors, &unit->fatal);
    }
    else if (strcmp(unit->path, "-") == 0)
    {
        source = options.stdin_source;
        ready = options.stdin_ready && source.length <= BUSTER_ASM_MAX_SOURCE;
    }
    else
    {
        ready = asm_read_file(unit->path, &lane->arena, &source);
    }
    X86MatchIndex* index = ready ? BUSTER_ARENA_PUSH_ARRAY(&lane->arena, X86MatchIndex, 1) : NULL;
    int result = 1;
    if (!ready || index == NULL)
    {
        fprintf(errors, "%s: unable to initialize or read assembly input (maximum 64 MiB)\n", unit->path);
    }
    else
    {
        x86_match_initialize(index, &options.target);
        const AsmOptions assembly = { .vectorized = !command->scalar, .intel = command->assembly_intel };
        AsmResult assembled = asm_assemble(source, index, assembly, &lane->arena);
        if (assembled.error != NULL)
        {
            asm_diagnostic_to(errors, unit->path, source, assembled, preprocess);
        }
        else if (!command->emit_object && !command->link)
        {
            result = 0;
        }
        else
        {
            const Object object = asm_object(&assembled, foreign_string(unit->path), &lane->arena);
            const bool written = object.error == OBJECT_ERROR_NONE &&
                (command->link ? check_units_keep_object(&unit->kept, &object) :
                 output != NULL && object_write_file(output, object.bytes, object.size) == OBJECT_ERROR_NONE);
            result = written ? 0 : 1;
            if (!written)
            {
                fprintf(errors, "%s: unable to build or write ELF object\n", unit->path);
            }
        }
    }
    return result;
}

static bool check_driver_mixed_link(const CheckDriverCommand* command, const CheckUnitsUnit* units, uint32_t count, CheckProgramLanes* lanes)
{
    const CheckDriverLibraries* libraries = command->libraries;
    Runtime runtime = { 0 };
    const RuntimeError discovered = runtime_discover_inputs(&runtime, command->runtime_kind);
    ForeignDriverRun run = { .begin = foreign_driver_now(), .result = 1 };
    run.arena = arena_reserve(UINT64_C(1) << 36);
    run.names = arena_reserve(UINT64_C(1) << 34);
    const uint64_t capacity = (uint64_t)libraries->argument_count + RUNTIME_OBJECTS + BUSTER_RUNTIME_ARCHIVES + 2;
    ForeignArgument* arguments = BUSTER_ARENA_PUSH_ARRAY(&run.arena, ForeignArgument, capacity);
    const uint32_t defaults = (uint32_t)BUSTER_ARRAY_LENGTH(runtime_library_directories);
    String8* directories = BUSTER_ARENA_PUSH_ARRAY(&run.arena, String8, (uint64_t)libraries->directory_count + defaults + 1);
    String8* undefined = BUSTER_ARENA_PUSH_ARRAY(&run.arena, String8, 1);
    run.succeeded = discovered == RUNTIME_ERROR_NONE && arguments != NULL && directories != NULL && undefined != NULL && run.names.base != NULL;
    if (discovered != RUNTIME_ERROR_NONE)
    {
        fprintf(stderr, "error: can't find the C runtime inputs%s%s\n", runtime.failed_file[0] != 0 ? ": " : "", runtime.failed_file);
    }
    else if (!run.succeeded)
    {
        fprintf(stderr, "fatal error: can't reserve mixed-input linker memory\n");
    }
    uint32_t used = 0;
    if (run.succeeded)
    {
        // The same runtime selection as the direct path, without importing a snapshot: the foreign resolver owns
        // these files' symbol resolution and archive extraction. Startup precedes the user's argv-ordered inputs.
        for (uint32_t i = RUNTIME_OBJECT_START; i <= RUNTIME_OBJECT_BEGIN; i += 1)
        {
            if (runtime.object_paths[i].length != 0)
            {
                arguments[used++] = (ForeignArgument){ .name = runtime.object_paths[i] };
            }
        }
        const uint32_t user_begin = used;
        memcpy(arguments + used, libraries->arguments, (uint64_t)libraries->argument_count * sizeof(*arguments));
        used += libraries->argument_count;
        for (uint32_t i = 0; i < count; i += 1)
        {
            if (units[i].kind != CHECK_DRIVER_INPUT_LINK)
            {
                arguments[user_begin + command->inputs[i].argument].bytes = units[i].kept.object;
                run.succeeded &= units[i].kept.object.pointer != NULL;
            }
        }
        for (uint32_t i = 0; i < runtime.archive_count; i += 1)
        {
            arguments[used++] = (ForeignArgument){ .name = runtime.archives[i].path };
        }
        if (runtime.gcc_s_path.length != 0)
        {
            arguments[used++] = (ForeignArgument){ .name = runtime.gcc_s_path, .flags = BUSTER_FOREIGN_INPUT_AS_NEEDED };
        }
        if (runtime.libc_script.length != 0)
        {
            arguments[used++] = (ForeignArgument){ .name = runtime.libc_script };
        }
        for (uint32_t i = RUNTIME_OBJECT_END; i < RUNTIME_OBJECTS; i += 1)
        {
            arguments[used++] = (ForeignArgument){ .name = runtime.object_paths[i] };
        }
        memcpy(directories, libraries->directories, (uint64_t)libraries->directory_count * sizeof(*directories));
        directories[libraries->directory_count] = runtime.gcc_directory;
        for (uint32_t i = 0; i < defaults; i += 1)
        {
            directories[libraries->directory_count + 1 + i] = foreign_string(runtime_library_directories[i]);
        }
        undefined[0] = libraries->linker.entry.pointer != NULL ? libraries->linker.entry : foreign_string("_start");
        run.driver = (ForeignDriver){
            .command = {
                .arguments = arguments,
                .argument_count = used,
                .directories = directories,
                .directory_count = libraries->directory_count + defaults + 1,
                .undefined = undefined,
                .undefined_count = 1,
            },
            .output = foreign_string(command->output != NULL ? command->output : "a.out"),
            .entry = undefined[0],
            .entry_given = libraries->linker.entry.pointer != NULL,
            .linker = libraries->linker,
            .kind = command->runtime_kind,
            .path = command->scalar ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR,
        };
    }
    bool result = false;
    if (run.succeeded)
    {
        WorkerBudgetLease lease = { .control = -1, .reply = -1, .child = -1 };
        run.most = check_units_slots(lanes->budget, lanes->lanes, &lease);
        run.driver.lanes = run.most;
        run.arguments_end = foreign_driver_now();
        // Enter the nested flow on EVERY session lane, including those outside the lease. Its inner rounds narrow
        // to the lease; the others stay parked. A narrowed outer group would let the session settle widen past it.
        const uint32_t session_lanes = lane_flow_lanes(&lanes->units_flow->rounds);
        lane_flow_round_begin(&lanes->units_flow->rounds, CHECK_UNITS_ROUND_FOREIGN, &run, session_lanes);
        foreign_driver_flow(&run);
        lane_flow_round_end(&lanes->units_flow->rounds);
        check_units_slots_back(lanes->units_flow, &lease);
        result = run.result == 0;
        // foreign_driver_link released run's arenas; the unit objects and discovered paths live until now.
    }
    else
    {
        arena_release(&run.names);
        arena_release(&run.arena);
    }
    runtime_release(&runtime);
    return result;
}
