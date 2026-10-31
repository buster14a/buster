// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The conversion on lanes (#87): with the vector path and a resolution on more than one lane, foreign_link_build_mode
// runs its costly passes on that many lanes, and the rest on lane 0 between syncs, as the exact path runs all of them
// on the calling thread. On a link of synth's shape the passes took 25 to 31 ms on one thread (on the 7940HS): the
// global names' symbols 10 to 13 (each name's winner, its round and its name's bytes are three reads that miss),
// link_check 7 to 8, the relocations 5 to 6, the rest under 3.
//   - The names (foreign_link_names_lanes): each lane decides its range of the names (foreign_link_decide_name) and
//     counts what it keeps, its common symbols' atoms and its names' bytes; lane 0 places the lanes' ranges in name
//     order and pushes the names' bytes once; then each lane adds its names' atoms, names and symbols through a view of
//     the link (foreign_link_view): the columns, with each table's count and capacity those of the lane's range, and a
//     slice of the names' bytes as its names' arena. The ids and every column come out as the exact path's.
//   - The relocations (foreign_link_relocations_lanes): each object's fixups are counted first (its relocations but
//     R_X86_64_NONE's, in the sections that have atoms), and placed in object order; then each lane imports its
//     objects' relocations through a view whose fixups are the object's range. The vector reader needs room for every
//     entry of a section in the wide addends too, so each object's range of them is its entries' count: the side
//     table has gaps, which nothing reads (a wide fixup holds its addend's index), where the exact path's is dense.
//   - link_check_lanes, link.c's.
//   - The debug sections' bytes and relocations (foreign_debug.c), after lane 0's plan, each lane taking items.
// A view's counts must end where they were placed to end; one that doesn't fails the conversion (which no input the
// exact path takes can make) rather than leave a gap among the fixups. Each lane's first failure is the smallest name or
// object of its range, and the lanes' smallest is the one reported, as the exact path reports its first.
//
// foreign_link.c includes this file before foreign_link_build_mode.

// A view of `link`: its columns, with one table's count and capacity set by the caller.
static Link foreign_link_view(const Link* link)
{
    return *link;
}

// What the conversion's lanes share: the steps' state (lane 0's, between syncs), and the passes'.
typedef struct ForeignLinkLanes ForeignLinkLanes;
struct ForeignLinkLanes
{
    ForeignLinkBuild* build;
    ForeignLinkName* decisions;
    // Each lane's range's counts and first failure, then its bases.
    uint64_t kept[BUSTER_LANE_MAX_COUNT];
    uint64_t commons[BUSTER_LANE_MAX_COUNT];
    uint64_t name_bytes[BUSTER_LANE_MAX_COUNT];
    uint32_t failed_at[BUSTER_LANE_MAX_COUNT];
    ForeignLinkError failure[BUSTER_LANE_MAX_COUNT];
    ImportResult import_failure[BUSTER_LANE_MAX_COUNT];
    uint8_t* bytes;
    uint64_t name_base[BUSTER_LANE_MAX_COUNT];
    uint64_t symbol_base[BUSTER_LANE_MAX_COUNT];
    uint64_t atom_base[BUSTER_LANE_MAX_COUNT];
    uint64_t byte_base[BUSTER_LANE_MAX_COUNT];
    // Each object's fixups and relocation entries, then their bases (object_count + 1 each).
    uint64_t* fixups;
    uint64_t* entries;
    // For each symbol, the least id of the objects whose references give it a type while it is undefined.
    uint32_t* first_typed;
    // The debug sections' plan, when the output keeps them (foreign_debug.c).
    ForeignDebugPlan debug_plan;
    bool debug;
};

// Whether every lane goes on: lane 0 decides from the conversion's result and every lane gets its answer. Only lane 0
// reads the result: it writes it in the steps it takes alone, and another lane's read before the broadcast's barrier
// would race with those writes, though the broadcast only keeps lane 0's answer.
static bool foreign_link_lanes_proceed(ForeignLinkLanes* state)
{
    const bool proceed = lane_index() == 0 && state->build->result.error == FOREIGN_LINK_ERROR_NONE;
    return lane_broadcast(proceed, 0) != 0;
}

// The global names' symbols on lanes (see the file's comment). Every lane calls it; lane 0 sets build->result.
static void foreign_link_names_lanes(ForeignLinkLanes* state)
{
    const uint32_t lane = lane_index();
    const uint32_t lanes = lane_count();
    ForeignLinkBuild* build = state->build;
    ForeignLink* output = build->output;
    const Foreign* foreign = build->foreign;
    const ForeignNames* names = &foreign->names;
    const Range64 range = lane_range(names->count);
    uint64_t kept = 0;
    uint64_t commons = 0;
    uint64_t bytes = 0;
    uint32_t failed_at = UINT32_MAX;
    ForeignLinkError failure = FOREIGN_LINK_ERROR_NONE;
    for (uint64_t at = range.begin; failed_at == UINT32_MAX && at < range.end; at += 1)
    {
        const uint32_t name = (uint32_t)names->order[at];
        const ForeignLinkName decision = foreign_link_decide_name(output, foreign, build->required, &build->bounds, build->kind, build->defer_undefined, (uint32_t)name);
        state->decisions[name] = decision;
        kept += decision.kept ? 1 : 0;
        commons += decision.kept && decision.common_class != 0 ? 1 : 0;
        bytes += decision.kept ? (uint64_t)names->length[name] + 1 : 0;
        failed_at = decision.error != FOREIGN_LINK_ERROR_NONE ? (uint32_t)at : failed_at;
        failure = decision.error;
    }
    state->kept[lane] = kept;
    state->commons[lane] = commons;
    state->name_bytes[lane] = bytes;
    state->failed_at[lane] = failed_at;
    state->failure[lane] = failure;
    lane_sync();
    if (lane == 0)
    {
        // The first failure in name order, else each lane's place: its names, symbols and common atoms after the lanes
        // before it, and its names' bytes in one push.
        uint32_t first = UINT32_MAX;
        uint64_t total_bytes = 0;
        uint64_t name_at = output->link.names.count;
        uint64_t symbol_at = output->link.symbols.count;
        uint64_t atom_at = output->link.atoms.count;
        for (uint32_t other = 0; other < lanes; other += 1)
        {
            if (state->failed_at[other] < first)
            {
                first = state->failed_at[other];
                build->result.error = state->failure[other];
                const uint32_t name = (uint32_t)names->order[first];
                build->result.name = name + 1;
                build->result.object = state->decisions[name].object;
            }
            state->name_base[other] = name_at;
            state->symbol_base[other] = symbol_at;
            state->atom_base[other] = atom_at;
            state->byte_base[other] = total_bytes;
            name_at += state->kept[other];
            symbol_at += state->kept[other];
            atom_at += state->commons[other];
            total_bytes += state->name_bytes[other];
        }
        const bool fits = name_at <= output->link.names.capacity && symbol_at <= output->link.symbols.capacity && atom_at <= output->link.atoms.capacity;
        state->bytes = first == UINT32_MAX && fits ? arena_push(output->link.name_arena, total_bytes + 1, 1) : 0;
        build->result.error = first == UINT32_MAX && state->bytes == 0 ? FOREIGN_LINK_ERROR_LINK : build->result.error;
    }
    if (foreign_link_lanes_proceed(state))
    {
        Arena slice = {
            .base = state->bytes + state->byte_base[lane],
            .length = 0,
            .capacity = state->name_bytes[lane],
        };
        Link view = foreign_link_view(&output->link);
        view.name_arena = &slice;
        view.names.count = (uint32_t)state->name_base[lane];
        view.names.capacity = (uint32_t)(state->name_base[lane] + state->kept[lane]);
        view.symbols.count = (uint32_t)state->symbol_base[lane];
        view.symbols.capacity = (uint32_t)(state->symbol_base[lane] + state->kept[lane]);
        view.atoms.count = (uint32_t)state->atom_base[lane];
        view.atoms.capacity = (uint32_t)(state->atom_base[lane] + state->commons[lane]);
        failed_at = UINT32_MAX;
        for (uint64_t at = range.begin; at < range.end; at += 1)
        {
            const uint32_t name = (uint32_t)names->order[at];
            const ForeignLinkName* decision = &state->decisions[name];
            const bool added = !decision->kept || foreign_link_add_name(&view, output, foreign, (uint32_t)name, decision) == FOREIGN_LINK_ERROR_NONE;
            failed_at = !added && failed_at == UINT32_MAX ? (uint32_t)at : failed_at;
        }
        const bool placed = view.names.count == view.names.capacity && view.symbols.count == view.symbols.capacity && view.atoms.count == view.atoms.capacity;
        state->failed_at[lane] = failed_at == UINT32_MAX && !placed ? (uint32_t)range.begin : failed_at;
    }
    lane_sync();
    if (lane == 0 && build->result.error == FOREIGN_LINK_ERROR_NONE)
    {
        uint32_t first = UINT32_MAX;
        for (uint32_t other = 0; other < lanes; other += 1)
        {
            first = state->failed_at[other] < first ? state->failed_at[other] : first;
        }
        if (first != UINT32_MAX)
        {
            build->result.error = FOREIGN_LINK_ERROR_LINK;
            const uint32_t name = (uint32_t)names->order[first];
            build->result.name = name + 1;
            build->result.object = state->decisions[name].object;
        }
        else
        {
            output->link.names.count = (uint32_t)(state->name_base[lanes - 1] + state->kept[lanes - 1]);
            output->link.symbols.count = (uint32_t)(state->symbol_base[lanes - 1] + state->kept[lanes - 1]);
            output->link.atoms.count = (uint32_t)(state->atom_base[lanes - 1] + state->commons[lanes - 1]);
        }
    }
}

// The objects' maps of their global symbols on lanes, each lane a range of the objects; then, on lane 0, each undefined
// symbol that a reference gives a type gets the type of the least object's, as the exact path's id order gives it.
static void foreign_link_maps_lanes(ForeignLinkLanes* state)
{
    ForeignLinkBuild* build = state->build;
    ForeignLink* output = build->output;
    const Foreign* foreign = build->foreign;
    const Range64 symbols = lane_range(output->link.symbols.count);
    for (uint64_t symbol = symbols.begin; symbol < symbols.end; symbol += 1)
    {
        state->first_typed[symbol] = UINT32_MAX;
    }
    lane_sync();
    const Range64 range = lane_range(foreign->object_count > 1 ? foreign->object_count - 1 : 0);
    for (uint64_t at = range.begin; at < range.end; at += 1)
    {
        foreign_link_map_object(output, foreign, (uint32_t)at + 1, state->first_typed);
    }
    lane_sync();
    for (uint64_t symbol = symbols.begin; symbol < symbols.end; symbol += 1)
    {
        const uint32_t object_id = state->first_typed[symbol];
        if (object_id != UINT32_MAX)
        {
            // The type the least object's reference gives it: its record for the symbol's name.
            const ForeignObject* input = &foreign->objects[object_id];
            const ForeignRound* round = foreign_symbol_round(foreign, input->global_begin);
            uint8_t type = 0;
            for (uint32_t i = 0; type == 0 && i < input->global_count; i += 1)
            {
                const uint32_t at = input->global_begin + i - round->first_symbol;
                const bool same = output->names[round->symbols.name_id[at]] == symbol;
                type = same ? (uint8_t)ELF64_ST_TYPE(round->symbols.info[at]) : 0;
            }
            output->link.symbols.flags[symbol] |= type;
        }
    }
    lane_sync();
}

// An object's fixups and relocation entries, as foreign_link_relocations imports them: its relocation sections' with an
// atom (but the bound ones, which foreign_link_bounds_relocations imports), every entry but R_X86_64_NONE's a fixup.
static void foreign_link_count_relocations(const ForeignLink* output, const Foreign* foreign, const ForeignLinkBounds* bounds, uint32_t id, uint64_t* fixups,
                                           uint64_t* entries)
{
    const ForeignObject* input = &foreign->objects[id];
    const ImportObject* object = &output->objects[id];
    uint64_t counted = 0;
    uint64_t total = 0;
    for (uint32_t i = 0; input->library == 0 && i < input->relocation_count; i += 1)
    {
        const Elf64_Shdr header = import_section_header(&input->elf, input->relocations[i]);
        const bool bound = foreign_link_bound_section(bounds, id, header.sh_info);
        const AtomId atom = !bound && header.sh_info < object->section_count ? object->section_atoms[header.sh_info] : 0;
        const String8 bytes = atom != 0 ? foreign_link_relocation_bytes(output, input, id, &header) : (String8){ 0 };
        const uint64_t count = bytes.length / sizeof(Elf64_Rela);
        for (uint64_t r = 0; r < count; r += 1)
        {
            Elf64_Rela relocation;
            memcpy(&relocation, bytes.pointer + r * sizeof(Elf64_Rela), sizeof(relocation));
            counted += ELF64_R_TYPE(relocation.r_info) != R_X86_64_NONE ? 1 : 0;
        }
        total += count;
    }
    *fixups = counted;
    *entries = total;
}

// The relocations on lanes (see the file's comment). Every lane calls it; lane 0 sets build->result.
static void foreign_link_relocations_lanes(ForeignLinkLanes* state)
{
    const uint32_t lane = lane_index();
    ForeignLinkBuild* build = state->build;
    ForeignLink* output = build->output;
    const Foreign* foreign = build->foreign;
    const uint32_t objects = foreign->object_count;
    const Range64 range = lane_range(objects > 1 ? objects - 1 : 0);
    for (uint64_t at = range.begin; at < range.end; at += 1)
    {
        const uint32_t id = (uint32_t)at + 1;
        foreign_link_count_relocations(output, foreign, &build->bounds, id, &state->fixups[id], &state->entries[id]);
    }
    lane_sync();
    if (lane == 0)
    {
        // Each object's fixups after the objects before it, from the link's count, and its wide addends' room likewise.
        uint64_t fixup_at = output->link.fixups.count;
        uint64_t wide_at = output->link.fixups.wide_count;
        for (uint32_t id = 1; id < objects; id += 1)
        {
            const uint64_t fixups = state->fixups[id];
            const uint64_t entries = state->entries[id];
            state->fixups[id] = fixup_at;
            state->entries[id] = wide_at;
            fixup_at += fixups;
            wide_at += entries;
        }
        state->fixups[objects] = fixup_at;
        state->entries[objects] = wide_at;
        build->result.error = fixup_at > output->link.fixups.capacity || wide_at > output->link.fixups.wide_capacity ? FOREIGN_LINK_ERROR_CAPACITY : FOREIGN_LINK_ERROR_NONE;
    }
    if (foreign_link_lanes_proceed(state))
    {
        ForeignLink view_output = *output;
        Link* view = &view_output.link;
        uint32_t failed_at = UINT32_MAX;
        ImportResult failure = { 0 };
        Scratch scratch = scratch_begin(0);
        for (uint64_t at = range.begin; failed_at == UINT32_MAX && at < range.end; at += 1)
        {
            const uint32_t id = (uint32_t)at + 1;
            view->fixups.count = (uint32_t)state->fixups[id];
            view->fixups.capacity = (uint32_t)state->fixups[id + 1];
            view->fixups.wide_count = (uint32_t)state->entries[id];
            view->fixups.wide_capacity = (uint32_t)state->entries[id + 1];
            view->error = LINK_ERROR_NONE;
            const ImportResult imported = foreign->objects[id].library == 0 ? foreign_link_relocations(&view_output, foreign, &build->bounds, id, foreign->path, scratch.arena) :
                                                                              (ImportResult){ 0 };
            const bool placed = imported.error != IMPORT_ERROR_NONE || view->fixups.count == view->fixups.capacity;
            if (imported.error != IMPORT_ERROR_NONE || !placed)
            {
                failed_at = id;
                failure = imported.error != IMPORT_ERROR_NONE ? imported : import_fail(IMPORT_ERROR_LINK, 0);
            }
        }
        scratch_end(scratch);
        state->failed_at[lane] = failed_at;
        state->import_failure[lane] = failure;
    }
    lane_sync();
    if (lane == 0 && build->result.error == FOREIGN_LINK_ERROR_NONE)
    {
        uint32_t first = UINT32_MAX;
        for (uint32_t other = 0; other < lane_count(); other += 1)
        {
            if (state->failed_at[other] < first)
            {
                first = state->failed_at[other];
                build->result.import = state->import_failure[other];
            }
        }
        if (first != UINT32_MAX)
        {
            build->result.error = FOREIGN_LINK_ERROR_IMPORT;
            build->result.object = first;
        }
        else
        {
            output->link.fixups.count = (uint32_t)state->fixups[objects];
            output->link.fixups.wide_count = (uint32_t)state->entries[objects];
        }
    }
}

// Every lane of the conversion's group: lane 0 takes the steps, and every lane the passes between them, each pass only
// once every step before it has gone right.
static void foreign_link_lane(void* argument)
{
    ForeignLinkLanes* state = argument;
    ForeignLinkBuild* build = state->build;
    const uint32_t lane = lane_index();
    if (lane == 0)
    {
        foreign_link_build_begin(build);
        // The passes' shared columns, in the scratch of the steps.
        const Foreign* foreign = build->foreign;
        state->decisions = build->result.error == FOREIGN_LINK_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(build->scratch.arena, ForeignLinkName, foreign->names.count + 1) : 0;
        state->fixups = build->result.error == FOREIGN_LINK_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(build->scratch.arena, uint64_t, (uint64_t)foreign->object_count + 1) : 0;
        state->entries = build->result.error == FOREIGN_LINK_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(build->scratch.arena, uint64_t, (uint64_t)foreign->object_count + 1) : 0;
        state->first_typed = build->result.error == FOREIGN_LINK_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(build->scratch.arena, uint32_t, (uint64_t)build->output->link.symbols.capacity + 1) : 0;
        const bool allocated = state->decisions != 0 && state->fixups != 0 && state->entries != 0 && state->first_typed != 0;
        build->result.error = build->result.error == FOREIGN_LINK_ERROR_NONE && !allocated ? FOREIGN_LINK_ERROR_MEMORY : build->result.error;
        build->ends[FOREIGN_LINK_PHASE_SECTIONS] = foreign_now();
    }
    if (foreign_link_lanes_proceed(state))
    {
        foreign_link_names_lanes(state);
    }
    if (lane == 0)
    {
        build->ends[FOREIGN_LINK_PHASE_NAMES] = foreign_now();
    }
    if (foreign_link_lanes_proceed(state))
    {
        foreign_link_maps_lanes(state);
    }
    if (lane == 0)
    {
        build->ends[FOREIGN_LINK_PHASE_MAPS] = foreign_now();
        foreign_link_build_middle(build);
        build->ends[FOREIGN_LINK_PHASE_UNWIND] = foreign_now();
    }
    if (foreign_link_lanes_proceed(state))
    {
        foreign_link_relocations_lanes(state);
    }
    if (lane == 0)
    {
        build->ends[FOREIGN_LINK_PHASE_RELOCATIONS] = foreign_now();
        foreign_link_build_end(build);
        build->ends[FOREIGN_LINK_PHASE_COPIES] = foreign_now();
    }
    if (foreign_link_lanes_proceed(state))
    {
        const LinkError checked = link_check_lanes(&build->output->link);
        if (lane == 0 && checked != LINK_ERROR_NONE)
        {
            build->result.error = FOREIGN_LINK_ERROR_LINK;
        }
    }
    if (lane == 0)
    {
        build->ends[FOREIGN_LINK_PHASE_CHECK] = foreign_now();
        foreign_link_build_finish(build);
        build->ends[FOREIGN_LINK_PHASE_EXPORTS] = foreign_now();
        // The debug sections' plan, and their passes on every lane (foreign_debug.c), unless the output leaves them out.
        state->debug = build->result.error == FOREIGN_LINK_ERROR_NONE && !build->options.strip_debug;
        if (state->debug)
        {
            build->output->debug = (ExecutableDebugSections){ 0 };
            build->result = foreign_debug_plan(&state->debug_plan, build->output, build->foreign, build->order, build->arena);
        }
    }
    if (foreign_link_lanes_proceed(state) && state->debug)
    {
        foreign_debug_contribute_lanes(&state->debug_plan, build->foreign);
    }
    // Every lane's items done before lane 0 reads their results.
    lane_sync();
    if (lane == 0 && state->debug && build->result.error == FOREIGN_LINK_ERROR_NONE)
    {
        build->result = foreign_debug_first_failure(state->debug_plan.contribution_results, state->debug_plan.contribution_count);
    }
    if (foreign_link_lanes_proceed(state) && state->debug)
    {
        foreign_debug_relocate_lanes(&state->debug_plan, build->output, build->foreign);
    }
    lane_sync();
    if (lane == 0)
    {
        if (state->debug && build->result.error == FOREIGN_LINK_ERROR_NONE)
        {
            build->result = foreign_debug_first_failure(state->debug_plan.relocation_results, state->debug_plan.relocation_count);
        }
        if (state->debug && build->result.error == FOREIGN_LINK_ERROR_NONE)
        {
            foreign_debug_finish(&state->debug_plan, build->output);
        }
        scratch_end(build->scratch);
        build->ends[FOREIGN_LINK_PHASE_DEBUG] = foreign_now();
    }
}

// The conversion on `lanes` lanes (see the file's comment), into build->result: in link-foreign's flow (#905), a round
// of it, which every lane runs.
static void foreign_link_build_lanes(ForeignLinkBuild* build, uint32_t lanes)
{
    ForeignLinkLanes state;
    memset(&state, 0, sizeof(state));
    state.build = build;
    LaneFlow* flow = build->foreign->flow;
    if (flow != 0)
    {
        lane_flow_round_begin(flow, FOREIGN_FLOW_CONVERT, &state, lanes);
        foreign_link_lane(&state);
        lane_flow_round_end(flow);
    }
    else if (!lane_run(lanes, foreign_link_lane, &state, 0))
    {
        build->result = (ForeignLinkResult){ .error = FOREIGN_LINK_ERROR_MEMORY };
    }
}
