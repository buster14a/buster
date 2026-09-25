#!/usr/bin/env python3
"""Anchor-checked disposable counters for the whole-type-table leads. Never commit."""
import sys
path = sys.argv[1]
src = open(path).read()
def insert(anchor, text, after=True, count=1):
    global src
    n = src.count(anchor)
    if n != count:
        sys.exit(f"anchor count {n} != {count}: {anchor[:80]!r}")
    src = src.replace(anchor, anchor + text if after else text + anchor)
PROBE = r'''
#include <stdio.h>
#include <stdlib.h>
typedef struct CProbeCounters CProbeCounters;
struct CProbeCounters
{
    unsigned long long layout_calls, layout_offset_calls, layout_solves, layout_offset_solves, layout_pending_seeded,
        layout_pending_visits, layout_offset_pending_visits, layout_passes, layout_scratch_bytes, type_count_max,
        def_scans, def_scan_iterations, def_scan_misses, member_calls, member_direct_visits, member_promoted_calls,
        member_promoted_memset_bytes, offsetof_member_calls, offsetof_memset_bytes;
};
static CProbeCounters c_probe;
__attribute__((destructor)) static void c_probe_print(void)
{
    if (getenv("BUSTER_PROBE"))
    {
        CProbeCounters* p = &c_probe;
        fprintf(stderr, "PROBE layout_calls=%llu layout_offset_calls=%llu layout_solves=%llu layout_offset_solves=%llu layout_pending_seeded=%llu layout_pending_visits=%llu layout_offset_pending_visits=%llu layout_passes=%llu layout_scratch_bytes=%llu type_count_max=%llu def_scans=%llu def_scan_iterations=%llu def_scan_misses=%llu member_calls=%llu member_direct_visits=%llu member_promoted_calls=%llu member_promoted_memset_bytes=%llu offsetof_member_calls=%llu offsetof_memset_bytes=%llu\n",
            p->layout_calls, p->layout_offset_calls, p->layout_solves, p->layout_offset_solves, p->layout_pending_seeded, p->layout_pending_visits,
            p->layout_offset_pending_visits, p->layout_passes, p->layout_scratch_bytes, p->type_count_max, p->def_scans, p->def_scan_iterations,
            p->def_scan_misses, p->member_calls, p->member_direct_visits, p->member_promoted_calls, p->member_promoted_memset_bytes,
            p->offsetof_member_calls, p->offsetof_memset_bytes);
    }
}
'''
insert("BUSTER_C_INTERNAL bool c_parse_type_layout_core(CTypeParseMachine* machine, Arena* arena, CPreprocessResult preprocess, CParseResult* result,\n", PROBE, after=False)
insert("""                                             CTypeId requested, u64* size_out, u32* alignment_out, u32 offset_member, u64* offset_out)
{
""", "    c_probe.layout_calls += 1; c_probe.layout_offset_calls += offset_out != 0;\n")
insert("""    u32 type_count = result->type_count;
    u32* pending;
    u32 pending_count;
""", "    c_probe.layout_solves += 1; c_probe.layout_offset_solves += offset_out != 0; if (type_count > c_probe.type_count_max) c_probe.type_count_max = type_count;\n")
insert("""    u64* sizes = arena_allocate(arena, u64, type_count + 1);
    u32* alignments = arena_allocate(arena, u32, type_count + 1);""", "    c_probe.layout_pending_seeded += pending_count; c_probe.layout_scratch_bytes += (unsigned long long)(type_count + 1) * 14 + (pending_count + 1) * 4;\n", after=False)
insert("""        bool progress = false;
        for (u32 pending_index = 0; pending_index < pending_count; pending_index += 1)
        {
            u32 type_index = pending[pending_index];
""", "            c_probe.layout_pending_visits += 1; c_probe.layout_offset_pending_visits += offset_out != 0;\n")
insert("""    for (u32 pass = 0; pass < type_count; pass += 1)
    {
        bool progress = false;
""", "        c_probe.layout_passes += 1;\n")
insert("""            for (u32 index = 0; index < result->type_count && type.value == C_ID_UNDERLYING_INVALID; index += 1)
            {
                if (result->types[index].definition_start == open + 1 &&""", "", after=True)
src = src.replace("""            for (u32 index = 0; index < result->type_count && type.value == C_ID_UNDERLYING_INVALID; index += 1)
            {
                if (result->types[index].definition_start == open + 1 &&""", """            c_probe.def_scans += 1;
            for (u32 index = 0; index < result->type_count && type.value == C_ID_UNDERLYING_INVALID; index += 1)
            {
                c_probe.def_scan_iterations += 1;
                if (result->types[index].definition_start == open + 1 &&""")
insert("""        if (type.value == C_ID_UNDERLYING_INVALID)
        {
            type = c_parse_scalar_type_core_begin(machine, frame, &declarator_start);
        }
        // The base type is read; a type word""", "", after=True)
src = src.replace("""                    type = c_parse_apply_trailing_qualifiers(result, frame->preprocess, type, &declarator_start, frame->end);
                }
            }
        }
        if (type.value == C_ID_UNDERLYING_INVALID)
        {
            type = c_parse_scalar_type_core_begin(machine, frame, &declarator_start);""", """                    type = c_parse_apply_trailing_qualifiers(result, frame->preprocess, type, &declarator_start, frame->end);
                }
            }
            c_probe.def_scan_misses += type.value == C_ID_UNDERLYING_INVALID;
        }
        if (type.value == C_ID_UNDERLYING_INVALID)
        {
            type = c_parse_scalar_type_core_begin(machine, frame, &declarator_start);""")
insert("""    bool promoted = false;
    for (u32 index = 0; index < value.member_count && field_type.value == C_ID_UNDERLYING_INVALID; index += 1)
    {
""", "        c_probe.member_direct_visits += 1;\n")
insert("""        memset(visited, 0, sizeof(*visited) * (result->type_count + 1));
        u32 work_index = 0;
""", "        c_probe.member_promoted_calls += 1; c_probe.member_promoted_memset_bytes += result->type_count + 1;\n")
insert("""    memset(visited, 0, result->type_count + 1);
    u32 count = 1;
""", "    c_probe.offsetof_member_calls += 1; c_probe.offsetof_memset_bytes += result->type_count + 1;\n")
insert("""    if (bit_width_out)
    {
        *bit_width_out = 0;
    }
    CType value = result->types[type.value];
    CTypeId field_type = C_TYPE_ID_INVALID;
""", "    c_probe.member_calls += 1;\n")
open(path, "w").write(src)
print("probe applied")
