// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original C++ COMDAT inputs exercise actual unwinding across a retained function in the losing object, including
// LSDA/personality references and cleanup destructors. The system C++ runtime is a separately linked oracle/runtime;
// no external implementation is copied. Include after foreign_dynamic_test.c and its command/output helpers.

static const char* const foreign_unwind_runtime_sources[] = {
    "extern \"C\" int drops; extern \"C\" int run_b();\n"
    "struct Guard { ~Guard() { drops += 1; } };\n"
    "template<int N> __attribute__((noinline)) int shared_throw(int n) { Guard guard; if (n) throw n + N; return N; }\n"
    "template int shared_throw<7>(int);\n"
    "extern \"C\" int run_a() { try { return shared_throw<7>(0); } catch (...) { return -100; } }\n"
    "int drops; int main() { int a = run_a(); try { run_b(); } catch (int value) { return a == 7 && value == 35 && drops == 3 ? 42 : 100; } return 101; }\n",
    "extern \"C\" int drops;\n"
    "struct Guard { ~Guard() { drops += 1; } };\n"
    "template<int N> __attribute__((noinline)) int shared_throw(int n) { Guard guard; if (n) throw n + N; return N; }\n"
    "template int shared_throw<7>(int);\n"
    "extern \"C\" int run_b() { Guard guard; return shared_throw<7>(28); }\n",
};

static void foreign_unwind_runtime_ownership(ForeignTestTally* tally, const Foreign* foreign, const ForeignLink* converted, const char* detail)
{
    uint32_t owners = 0;
    bool selected = false;
    bool dropped = false;
    bool retained = false;
    bool states = true;
    for (uint32_t id = 1; id < foreign->object_count; id += 1)
    {
        const ForeignObject* object = &foreign->objects[id];
        uint32_t run = 0;
        uint32_t shared = 0;
        for (uint32_t i = object->first_global; object->library == 0 && i < object->symbols.count; i += 1)
        {
            const Elf64_Sym symbol = import_symbol(&object->symbols, i);
            String8 name = { 0 };
            uint32_t section = 0;
            const bool defined = ELF64_ST_TYPE(symbol.st_info) == STT_FUNC && import_symbol_section(&object->symbols, i, &symbol, &section) &&
                                 section != SHN_UNDEF && section < object->elf.section_count && (section < SHN_LORESERVE || symbol.st_shndx == SHN_XINDEX);
            if (defined && import_string(object->symbols.names, symbol.st_name, &name))
            {
                run = import_equal(name, "run_b") ? section : run;
                // The explicit shared_throw<7>(int) instantiation in both original C++ inputs.
                shared = import_equal(name, "_Z12shared_throwILi7EEii") ? section : shared;
            }
        }
        if (run != 0)
        {
            owners += 1;
            const ImportObject* imported = &converted->objects[id];
            selected = shared != 0 && shared != run && object->discarded != 0 && object->discarded[shared] != 0 && object->discarded[run] == 0 &&
                       shared < imported->section_count && run < imported->section_count && imported->section_atoms[shared] == 0 && imported->section_atoms[run] != 0;
            for (const ForeignLinkUnwind* frame = converted->unwind != 0 ? converted->unwind[id] : 0; frame != 0; frame = frame->next)
            {
                for (uint32_t r = 0; r < frame->plan.record_count; r += 1)
                {
                    const ForeignUnwindRecord* record = &frame->plan.records[r];
                    if (record->kind == FOREIGN_UNWIND_FDE && record->section == shared)
                    {
                        dropped = true;
                        states = states && record->new_offset == UINT64_MAX;
                    }
                    if (record->kind == FOREIGN_UNWIND_FDE && record->section == run)
                    {
                        retained = true;
                        states = states && record->new_offset != UINT64_MAX;
                    }
                }
            }
        }
    }
    foreign_test_expect(tally, owners == 1 && selected, "losing C++ object retains run_b and discards shared_throw", detail);
    foreign_test_expect(tally, owners == 1 && dropped && states, "losing shared_throw FDE is removed", detail);
    foreign_test_expect(tally, owners == 1 && retained && states, "losing object's run_b FDE remains", detail);
}

static bool foreign_unwind_runtime_test(const char* directory, Arena* arena, bool cross_check, ForeignTestSystem* system)
{
    ForeignTestTally tally = {
        .check = "foreign C++ unwind runtime",
    };
    const uint64_t saved = arena->length;
    Runtime runtime;
    bool ready = runtime_import(&runtime) == RUNTIME_ERROR_NONE && system->found;
    ForeignTestLine* line = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignTestLine, 1);
    ready = ready && line != 0;
    foreign_test_expect(&tally, ready, "find system runtime and reserve command", 0);
    if (ready)
    {
        *line = (ForeignTestLine){ 0 };
        foreign_test_pie_begin(line, system);
        foreign_test_add(line, (char*)"--eh-frame-hdr", false, true);
    }
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(foreign_unwind_runtime_sources); i += 1)
    {
        ForeignTestPath source;
        ForeignTestPath object;
        ForeignTestPath output;
        ForeignTestPath errors;
        snprintf(source, sizeof(source), "%s/fw_runtime_%u.cpp", directory, i);
        snprintf(object, sizeof(object), "%s/fw_runtime_%u.o", directory, i);
        snprintf(output, sizeof(output), "%s/fw_runtime_%u_compile.out", directory, i);
        snprintf(errors, sizeof(errors), "%s/fw_runtime_%u_compile.err", directory, i);
        char* arguments[] = {
            (char*)"g++",
            (char*)"-c",
            (char*)"-O1",
            (char*)"-fPIC",
            (char*)"-ffunction-sections",
            (char*)"-fdata-sections",
            source,
            (char*)"-o",
            object,
            0,
        };
        const char* text = foreign_unwind_runtime_sources[i];
        ready = foreign_test_write(source, text, strlen(text)) && object_test_run(arguments, output, errors) == 0;
        foreign_test_expect(&tally, ready, "compile original C++ exception fixture", source);
        char name[64];
        snprintf(name, sizeof(name), "fw_runtime_%u.o", i);
        foreign_test_add(line, foreign_test_path(directory, name, arena), true, true);
    }
    ForeignCommand command = { 0 };
    if (ready)
    {
        foreign_test_add(line, (char*)"-lstdc++", true, true);
        foreign_test_pie_end(line, system);
        for (uint32_t i = 0; i < line->our_count; i += 1)
        {
            ready = ready && line->ours[i] != 0;
        }
        ready = ready && foreign_test_command(&command, line->ours, line->our_count, arena);
        foreign_test_expect(&tally, ready, "prepare C++ runtime command", 0);
    }
    String8 reference = { 0 };
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(foreign_test_lanes); l += 1)
        {
            const uint64_t iteration = arena->length;
            Foreign foreign;
            foreign_run(&foreign, &command, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, foreign_test_lanes[l], 0);
            Arena names = arena_reserve(1ull << 28);
            ForeignLink converted = { 0 };
            const ForeignLinkResult built = foreign_link_build_dynamic(&converted, &foreign, arena, &names, runtime.loader_path);
            char detail[256];
            snprintf(detail, sizeof(detail), "%s %u lanes: resolve %u convert %u object %u name %u import %u section %u symbol %u offset %llu",
                     path == 0 ? "exact" : "vector", foreign_test_lanes[l], (unsigned)foreign.failure.error, (unsigned)built.error,
                     built.object, built.name, (unsigned)built.import.error, built.import.section, built.import.symbol, (unsigned long long)built.import.offset);
            const bool linked = foreign.failure.error == FOREIGN_ERROR_NONE && built.error == FOREIGN_LINK_ERROR_NONE;
            foreign_test_expect(&tally, linked, "convert C++ COMDAT and exception data", detail);
            ForeignTestPath executable;
            snprintf(executable, sizeof(executable), "%s/fw_runtime_output_%u_%u", directory, path, foreign_test_lanes[l]);
            bool published = false;
            if (linked)
            {
                foreign_unwind_runtime_ownership(&tally, &foreign, &converted, detail);
                ExecutableTestWrite write = {
                    .link = &converted.link,
                    .options = {
                        .path = {
                            .pointer = executable,
                            .length = strlen(executable),
                        },
                        .entry = foreign_link_test_symbol(&foreign, &converted, "_start"),
                        .dynamic = &converted.dynamic,
                        .exact = path == 0,
                    },
                };
                published = lane_run(foreign_test_lanes[l], executable_test_lane_main, &write, 0) && write.succeeded;
                foreign_test_expect(&tally, published, "write C++ exception executable", detail);
                if (!published)
                {
                    executable_test_print_failure(detail, &write.result);
                }
                if (published)
                {
                    foreign_test_expect(&tally, foreign_dynamic_test_run(directory, executable, 100, arena), "throw crosses both objects and runs cleanup destructors", detail);
                    const String8 bytes = foreign_test_read(arena, executable);
                    const char* unwind = bytes.pointer != 0 ? executable_test_unwind(&converted.link, bytes, 0, foreign.object_count, &write.result) : "no output";
                    foreign_test_expect(&tally, unwind == 0, "independent unwind walk and range lookups", unwind);
                    if (path != 0 || l != 0)
                    {
                        foreign_test_expect(&tally, reference.pointer != 0 && bytes.pointer != 0 && reference.length == bytes.length &&
                                            memcmp(reference.pointer, bytes.pointer, bytes.length) == 0, "C++ output agrees across paths and lanes", detail);
                    }
                }
            }
            arena_release(&names);
            foreign_release(&foreign);
            arena_pop_to(arena, iteration);
            if (path == 0 && l == 0)
            {
                reference = published ? foreign_test_read(arena, executable) : (String8){ 0 };
                foreign_test_expect(&tally, reference.pointer != 0, "retain exact C++ output", 0);
            }
        }
    }
    if (ready && cross_check)
    {
        foreign_dynamic_test_oracles(&tally, directory, line, 100, arena);
    }
    runtime_release(&runtime);
    arena_pop_to(arena, saved);
    return foreign_test_report(&tally);
}
