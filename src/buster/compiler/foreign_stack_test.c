// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Selected ET_REL inputs with an explicit executable-stack request must not reach the NX-only writer. Original
// assembly fixtures check direct inputs, regular/thin archive selection and losing COMDAT code on both resolver
// paths and 1/2/3/8 lanes. Grouped/excluded markers test our conservative refusal policy, not another linker's rule.
// These are producer diagnostics and section-import checks; no executable, GC or external linker oracle is used.

static const ForeignLinkTestSource foreign_stack_test_sources[] = {
    {
        .name = "fstack_start",
        .text =
            ".text\n.globl _start\n.type _start,@function\n_start:\nmov $42,%edi\nmov $60,%eax\nsyscall\n"
            ".size _start,.-_start\n.section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .name = "fstack_missing",
        .text =
            ".text\n.globl _start\n.type _start,@function\n_start:\nmov $42,%edi\nmov $60,%eax\nsyscall\n"
            ".size _start,.-_start\n",
    },
    {
        .name = "fstack_call",
        .text =
            ".text\n.globl _start\n.type _start,@function\n_start:\ncall fstack_pick\nmov %eax,%edi\nmov $60,%eax\nsyscall\n"
            ".size _start,.-_start\n.section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .name = "fstack_request",
        .text =
            ".text\n.globl fstack_pick\n.type fstack_pick,@function\nfstack_pick:\nmov $42,%eax\nret\n"
            ".size fstack_pick,.-fstack_pick\n.section .note.GNU-stack,\"x\",@progbits\n",
    },
    {
        .name = "fstack_nonexec",
        .text =
            ".text\n.globl fstack_pick\n.type fstack_pick,@function\nfstack_pick:\nmov $42,%eax\nret\n"
            ".size fstack_pick,.-fstack_pick\n.section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .name = "fstack_first",
        .text =
            ".section .text.fstack_chosen,\"axG\",@progbits,fstack_chosen,comdat\n"
            ".weak fstack_chosen\n.type fstack_chosen,@function\nfstack_chosen:\nmov $42,%eax\nret\n.size fstack_chosen,.-fstack_chosen\n"
            ".section .note.GNU-stack,\"\",@progbits\n",
    },
    {
        .name = "fstack_loser",
        .text =
            ".section .text.fstack_chosen,\"axG\",@progbits,fstack_chosen,comdat\n"
            ".weak fstack_chosen\n.type fstack_chosen,@function\nfstack_chosen:\nmov $99,%eax\nret\n.size fstack_chosen,.-fstack_chosen\n"
            ".section .note.GNU-stack,\"x\",@progbits\n",
    },
    {
        .name = "fstack_grouped",
        .text =
            ".section .text.fstack_chosen,\"axG\",@progbits,fstack_chosen,comdat\n"
            ".weak fstack_chosen\n.type fstack_chosen,@function\nfstack_chosen:\nmov $99,%eax\nret\n.size fstack_chosen,.-fstack_chosen\n"
            ".section .note.GNU-stack,\"xG\",@progbits,fstack_chosen,comdat\n",
    },
    {
        .name = "fstack_excluded",
        .text =
            ".text\n.globl fstack_pick\n.type fstack_pick,@function\nfstack_pick:\nmov $42,%eax\nret\n"
            ".size fstack_pick,.-fstack_pick\n.section .note.GNU-stack,\"xe\",@progbits\n",
    },
    {
        .name = "fstack_retry_before",
        .text =
            ".text\n.globl fstack_retry\n.type fstack_retry,@function\nfstack_retry:\nret\n.size fstack_retry,.-fstack_retry\n"
            ".section .init_array.65536,\"aw\",@init_array\n.balign 8\n.quad 0\n"
            ".section .note.GNU-stack,\"x\",@progbits\n",
    },
    {
        .name = "fstack_retry_after",
        .text =
            ".text\n.globl fstack_retry\n.type fstack_retry,@function\nfstack_retry:\nret\n.size fstack_retry,.-fstack_retry\n"
            ".section .note.GNU-stack,\"x\",@progbits\n"
            ".section .init_array.65536,\"aw\",@init_array\n.balign 8\n.quad 0\n",
    },
};

typedef struct ForeignStackTestCase ForeignStackTestCase;
struct ForeignStackTestCase
{
    const char* name;
    const char* arguments[5];
    const char* request;
    uint32_t objects;
    bool archive;
    bool thin;
    bool discarded;
    bool grouped;
    bool excluded;
    bool dynamic;
};

static const ForeignStackTestCase foreign_stack_test_cases[] = {
    {
        .name = "missing note",
        .arguments = {
            "fstack_missing.o",
        },
        .objects = 2,
    },
    {
        .name = "non-executable note",
        .arguments = {
            "fstack_start.o",
        },
        .objects = 2,
    },
    {
        .name = "non-executable provider",
        .arguments = {
            "fstack_call.o",
            "fstack_nonexec.o",
        },
        .objects = 3,
    },
    {
        .name = "referenced direct request",
        .arguments = {
            "fstack_call.o",
            "fstack_request.o",
        },
        .request = "fstack_request.o",
        .objects = 3,
        .dynamic = true,
    },
    {
        .name = "unreferenced direct request",
        .arguments = {
            "fstack_start.o",
            "fstack_request.o",
        },
        .request = "fstack_request.o",
        .objects = 3,
    },
    {
        .name = "extracted regular member",
        .arguments = {
            "fstack_call.o",
            "fstack_request.a",
        },
        .request = "fstack_request.a",
        .objects = 3,
        .archive = true,
    },
    {
        .name = "unextracted regular member",
        .arguments = {
            "fstack_start.o",
            "fstack_request.a",
        },
        .objects = 2,
    },
    {
        .name = "forced regular member",
        .arguments = {
            "fstack_start.o",
            "--undefined=fstack_pick",
            "fstack_request.a",
        },
        .request = "fstack_request.a",
        .objects = 3,
        .archive = true,
    },
    {
        .name = "whole regular archive",
        .arguments = {
            "fstack_start.o",
            "--whole-archive",
            "fstack_request.a",
            "--no-whole-archive",
        },
        .request = "fstack_request.a",
        .objects = 3,
        .archive = true,
    },
    {
        .name = "extracted thin member",
        .arguments = {
            "fstack_call.o",
            "fstack_request_thin.a",
        },
        .request = "fstack_request_thin.a",
        .objects = 3,
        .archive = true,
        .thin = true,
    },
    {
        .name = "unextracted thin member",
        .arguments = {
            "fstack_start.o",
            "fstack_request_thin.a",
        },
        .objects = 2,
    },
    {
        .name = "forced thin member",
        .arguments = {
            "fstack_start.o",
            "--undefined=fstack_pick",
            "fstack_request_thin.a",
        },
        .request = "fstack_request_thin.a",
        .objects = 3,
        .archive = true,
        .thin = true,
    },
    {
        .name = "whole thin archive",
        .arguments = {
            "fstack_start.o",
            "--whole-archive",
            "fstack_request_thin.a",
            "--no-whole-archive",
        },
        .request = "fstack_request_thin.a",
        .objects = 3,
        .archive = true,
        .thin = true,
    },
    {
        .name = "request survives losing COMDAT code",
        .arguments = {
            "fstack_start.o",
            "fstack_first.o",
            "fstack_loser.o",
        },
        .request = "fstack_loser.o",
        .objects = 4,
        .discarded = true,
    },
    {
        .name = "conservative grouped-marker refusal",
        .arguments = {
            "fstack_start.o",
            "fstack_first.o",
            "fstack_grouped.o",
        },
        .request = "fstack_grouped.o",
        .objects = 4,
        .discarded = true,
        .grouped = true,
    },
    {
        .name = "conservative excluded-marker refusal",
        .arguments = {
            "fstack_start.o",
            "fstack_excluded.o",
        },
        .request = "fstack_excluded.o",
        .objects = 3,
        .excluded = true,
    },
};

static bool foreign_stack_test_compile(const char* directory, ForeignTestTally* tally)
{
    bool result = true;
    ForeignTestPath source;
    ForeignTestPath object;
    ForeignTestPath output;
    ForeignTestPath errors;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(foreign_stack_test_sources); i += 1)
    {
        const ForeignLinkTestSource* fixture = &foreign_stack_test_sources[i];
        snprintf(source, sizeof(source), "%s/%s.s", directory, fixture->name);
        snprintf(object, sizeof(object), "%s/%s.o", directory, fixture->name);
        snprintf(output, sizeof(output), "%s/%s_compile.out", directory, fixture->name);
        snprintf(errors, sizeof(errors), "%s/%s_compile.err", directory, fixture->name);
        char* arguments[] = {
            (char*)"cc",
            (char*)"-c",
            source,
            (char*)"-o",
            object,
            0,
        };
        const bool made = foreign_test_write(source, fixture->text, strlen(fixture->text)) && object_test_run(arguments, output, errors) == 0;
        foreign_test_expect(tally, made, "compile original stack fixture", fixture->name);
        result = made && result;
    }
    return result;
}

static uint32_t foreign_stack_test_section(const ForeignObject* object, const char* wanted)
{
    uint32_t result = 0;
    for (uint32_t section = 1; result == 0 && section < object->elf.section_count; section += 1)
    {
        const Elf64_Shdr header = import_section_header(&object->elf, section);
        String8 name = { 0 };
        if (import_section_name(&object->elf, &header, &name) && import_equal(name, wanted))
        {
            result = section;
        }
    }
    return result;
}

static void foreign_stack_test_case(ForeignTestTally* tally, const char* directory, const ForeignStackTestCase* fixture, Arena* arena)
{
    const uint64_t saved = arena->length;
    char* arguments[BUSTER_ARRAY_LENGTH(fixture->arguments)];
    uint32_t count = 0;
    bool ready = true;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixture->arguments) && fixture->arguments[i] != 0; i += 1)
    {
        const char* argument = fixture->arguments[i];
        arguments[count] = argument[0] == '-' ? (char*)argument : foreign_test_path(directory, argument, arena);
        ready = arguments[count] != 0 && ready;
        count += 1;
    }
    const char* requested = fixture->request != 0 ? foreign_test_path(directory, fixture->request, arena) : 0;
    ready = ready && (fixture->request == 0 || requested != 0);
    ForeignCommand command = { 0 };
    ready = ready && foreign_test_command(&command, arguments, count, arena);
    command.partition_records = BUSTER_FOREIGN_TEST_PARTITION_RECORDS;
    foreign_test_expect(tally, ready, "prepare stack command", fixture->name);
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(foreign_test_lanes); l += 1)
        {
            const uint64_t iteration = arena->length;
            Foreign foreign;
            foreign_run(&foreign, &command, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, foreign_test_lanes[l], 0);
            char detail[256];
            snprintf(detail, sizeof(detail), "%s, %s on %u lanes", fixture->name, path == 0 ? "exact" : "vector", foreign_test_lanes[l]);
            foreign_test_expect(tally, foreign.failure.error == FOREIGN_ERROR_NONE, "resolve stack fixture", detail);
            if (foreign.failure.error == FOREIGN_ERROR_NONE)
            {
                foreign_test_expect(tally, foreign.object_count == fixture->objects, "select exactly the expected objects", detail);
                uint32_t requests = 0;
                uint32_t request_object = 0;
                uint32_t request_section = 0;
                for (uint32_t id = 1; id < foreign.object_count; id += 1)
                {
                    const ForeignObject* object = &foreign.objects[id];
                    const uint32_t section = foreign_stack_test_section(object, ".note.GNU-stack");
                    const Elf64_Shdr header = import_section_header(&object->elf, section);
                    for (uint32_t a = 0; a < count; a += 1)
                    {
                        const char* argument = fixture->arguments[a];
                        const bool missing = strcmp(argument, "fstack_missing.o") == 0;
                        const bool clear = strcmp(argument, "fstack_start.o") == 0 || strcmp(argument, "fstack_call.o") == 0 || strcmp(argument, "fstack_nonexec.o") == 0;
                        if ((missing || clear) && import_equal(foreign.inputs[object->input].path, arguments[a]))
                        {
                            foreign_test_expect(tally, missing ? section == 0 : section != 0 && (header.sh_flags & SHF_EXECINSTR) == 0,
                                                "assembler preserves missing versus non-executable note", detail);
                        }
                    }
                    if (section != 0 && (header.sh_flags & SHF_EXECINSTR) != 0)
                    {
                        requests += 1;
                        request_object = id;
                        request_section = section;
                    }
                }
                foreign_test_expect(tally, requests == (fixture->request != 0 ? 1u : 0u), "inspect selected stack requests independently", detail);
                Arena names = arena_reserve(1ull << 24);
                ForeignLink converted = { 0 };
                const ForeignLinkResult built = foreign_link_build(&converted, &foreign, arena, &names);
                if (fixture->request != 0)
                {
                    foreign_test_expect(tally, built.error == FOREIGN_LINK_ERROR_IMPORT && built.import.error == IMPORT_ERROR_UNSUPPORTED,
                                        "refuse the selected executable-stack request", detail);
                    if (request_object != 0)
                    {
                        const ForeignObject* object = &foreign.objects[request_object];
                        const ForeignInput* input = &foreign.inputs[object->input];
                        const Elf64_Shdr header = import_section_header(&object->elf, request_section);
                        foreign_test_expect(tally, import_equal(input->path, requested) && (object->member != 0) == fixture->archive &&
                                            input->kind == (fixture->archive ? FOREIGN_KIND_ARCHIVE : FOREIGN_KIND_OBJECT),
                                            "identify the requesting input and member", detail);
                        foreign_test_expect(tally, built.object == request_object && built.name == 0 && built.import.section == request_section &&
                                            built.import.offset == object->elf.section_offset + (uint64_t)request_section * sizeof(Elf64_Shdr) &&
                                            built.import.type == header.sh_type && built.import.symbol == 0 && built.import.relocation == 0,
                                            "report the original object, section, header offset and type", detail);
                        foreign_test_expect(tally, converted.objects != 0 && converted.objects[request_object].executable_stack_section == request_section,
                                            "retain the original request section in import metadata", detail);
                        foreign_test_expect(tally, ((header.sh_flags & SHF_GROUP) != 0) == fixture->grouped &&
                                            ((header.sh_flags & SHF_EXCLUDE) != 0) == fixture->excluded,
                                            "fixture has the intended marker flags", detail);
                        if (fixture->archive && input->kind == FOREIGN_KIND_ARCHIVE)
                        {
                            const ForeignArchive* archive = &foreign.archives[input->item];
                            foreign_test_expect(tally, archive->thin == fixture->thin && object->member <= archive->member_count,
                                                "identify regular or thin selected member", detail);
                            if (object->member != 0 && object->member <= archive->member_count)
                            {
                                const char* member = fixture->thin ? foreign_test_path(directory, "fstack_request.o", arena) : "fstack_request.o";
                                foreign_test_expect(tally, member != 0 && import_equal(archive->members[object->member - 1].name, member),
                                                    "report the requesting archive member", detail);
                            }
                        }
                        if (fixture->discarded)
                        {
                            const uint32_t code = foreign_stack_test_section(object, ".text.fstack_chosen");
                            foreign_test_expect(tally, code != 0 && object->discarded != 0 && object->discarded[code] != 0,
                                                "requesting object's COMDAT code lost selection", detail);
                            foreign_test_expect(tally, object->discarded != 0 && (object->discarded[request_section] != 0) == fixture->grouped,
                                                "distinguish ordinary and grouped stack markers", detail);
                        }
                    }
                    if (fixture->dynamic)
                    {
                        const String8 interpreter = {
                            .pointer = "/foreign-stack-test-loader",
                            .length = sizeof("/foreign-stack-test-loader") - 1,
                        };
                        ForeignLink dynamic = { 0 };
                        const ForeignLinkResult pie = foreign_link_build_dynamic(&dynamic, &foreign, arena, &names, interpreter);
                        foreign_test_expect(tally, pie.error == built.error && pie.object == built.object && pie.name == built.name &&
                                            pie.import.error == built.import.error && pie.import.section == built.import.section &&
                                            pie.import.symbol == built.import.symbol && pie.import.relocation == built.import.relocation &&
                                            pie.import.type == built.import.type && pie.import.offset == built.import.offset,
                                            "dynamic handoff refuses with the identical diagnostic", detail);
                    }
                }
                else
                {
                    foreign_test_expect(tally, built.error == FOREIGN_LINK_ERROR_NONE && link_check(&converted.link) == LINK_ERROR_NONE,
                                        "accept inputs without a selected explicit stack request", detail);
                    for (uint32_t id = 1; converted.objects != 0 && id < foreign.object_count; id += 1)
                    {
                        foreign_test_expect(tally, converted.objects[id].executable_stack_section == 0, "do not invent a stack request", detail);
                    }
                }
                arena_release(&names);
            }
            foreign_release(&foreign);
            arena_pop_to(arena, iteration);
        }
    }
    arena_pop_to(arena, saved);
}

// A priority outside the importer's range is detected after the vector batch records the stack marker. Scalar
// retry must clear that speculative metadata, then retain only requests preceding the failing section in order.
static void foreign_stack_test_retry(ForeignTestTally* tally, const char* directory, bool before, Arena* arena)
{
    const uint64_t saved = arena->length;
    char* arguments[] = {
        foreign_test_path(directory, before ? "fstack_retry_before.o" : "fstack_retry_after.o", arena),
    };
    ForeignCommand command = { 0 };
    const bool ready = arguments[0] != 0 && foreign_test_command(&command, arguments, BUSTER_ARRAY_LENGTH(arguments), arena);
    foreign_test_expect(tally, ready, "prepare section retry fixture", 0);
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(foreign_test_lanes); l += 1)
        {
            const uint64_t iteration = arena->length;
            Foreign foreign;
            foreign_run(&foreign, &command, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, foreign_test_lanes[l], 0);
            char detail[128];
            snprintf(detail, sizeof(detail), "invalid priority %s stack, %s on %u lanes", before ? "before" : "after",
                     path == 0 ? "exact" : "vector", foreign_test_lanes[l]);
            const bool resolved = foreign.failure.error == FOREIGN_ERROR_NONE && foreign.object_count == 2;
            foreign_test_expect(tally, resolved, "resolve section retry fixture", detail);
            if (resolved)
            {
                const ForeignObject* input = &foreign.objects[1];
                const uint32_t priority = foreign_stack_test_section(input, ".init_array.65536");
                const uint32_t stack = foreign_stack_test_section(input, ".note.GNU-stack");
                foreign_test_expect(tally, priority != 0 && stack != 0 && (priority < stack) == before &&
                                    priority / BUSTER_IMPORT_BATCH == stack / BUSTER_IMPORT_BATCH,
                                    "place the invalid priority and request in one vector batch", detail);
                ImportObject objects[2] = { 0 };
                ForeignLink converted = {
                    .objects = objects,
                };
                LinkCapacity capacity = { 0 };
                Arena names = arena_reserve(1ull << 24);
                const bool allocated = foreign_link_capacity(&foreign, 0, 0, &capacity) && link_initialize(&converted.link, arena, &names, capacity);
                foreign_test_expect(tally, allocated, "allocate direct section handoff", detail);
                if (allocated)
                {
                    // A reused output must be initialized even on the exact path.
                    objects[1].executable_stack_section = UINT32_MAX;
                    const ImportResult imported = foreign_link_sections(&converted, input, 1, foreign.path, arena);
                    foreign_test_expect(tally, imported.error == IMPORT_ERROR_UNSUPPORTED && imported.section == priority &&
                                        imported.offset == input->elf.section_offset + (uint64_t)priority * sizeof(Elf64_Shdr) &&
                                        imported.type == SHT_INIT_ARRAY && imported.symbol == 0 && imported.relocation == 0,
                                        "scalar retry reports the earlier import failure precisely", detail);
                    foreign_test_expect(tally, objects[1].executable_stack_section == (before ? 0 : stack),
                                        "scalar retry keeps only previously visited stack metadata", detail);
                }
                arena_release(&names);
            }
            foreign_release(&foreign);
            arena_pop_to(arena, iteration);
        }
    }
    arena_pop_to(arena, saved);
}

static bool foreign_stack_test(const char* directory, Arena* arena)
{
    const uint64_t saved = arena->length;
    ForeignTestTally tally = {
        .check = "foreign executable stack refusal",
    };
    if (foreign_stack_test_compile(directory, &tally))
    {
        const char* members[] = {
            "fstack_request.o",
        };
        char* archive = foreign_test_archive(directory, "fstack_request.a", members, BUSTER_ARRAY_LENGTH(members), false, arena);
        char* thin = foreign_test_archive(directory, "fstack_request_thin.a", members, BUSTER_ARRAY_LENGTH(members), true, arena);
        foreign_test_expect(&tally, archive != 0 && thin != 0, "prepare regular and thin stack archives", 0);
        if (archive != 0 && thin != 0)
        {
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(foreign_stack_test_cases); i += 1)
            {
                foreign_stack_test_case(&tally, directory, &foreign_stack_test_cases[i], arena);
            }
            foreign_stack_test_retry(&tally, directory, true, arena);
            foreign_stack_test_retry(&tally, directory, false, arena);
        }
    }
    arena_pop_to(arena, saved);
    return foreign_test_report(&tally);
}
