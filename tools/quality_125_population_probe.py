#!/usr/bin/env python3
"""Disposable #125 diagnostic overlay; not a production counter or benchmark."""
from __future__ import annotations
import hashlib
import json
import pathlib

path = pathlib.Path('src/buster/lib/compiler/codegen/register_allocator_quality.c')
text = path.read_text()
blob = hashlib.sha1(b'blob ' + str(len(text.encode())).encode() + b'\0' + text.encode()).hexdigest()
if blob != '2ce4bfdfb73d07cd9a127812700108403b770de8':
    raise SystemExit(f'wrong pinned allocator blob: {blob}')

def replace_once(old: str, new: str) -> None:
    global text
    if text.count(old) != 1:
        raise SystemExit(f'non-unique diagnostic anchor: {old[:100]!r}')
    text = text.replace(old, new)

replace_once('        BUSTER_QUALITY_COUNT(candidate_region_nonzero_cells, nonzero);', '''        BUSTER_QUALITY_COUNT(candidate_region_nonzero_cells, nonzero);
        bool probe_ordered = true;
        for (u32 probe_edit = 1; probe_edit < baseline.edit_count; probe_edit += 1)
        {
            probe_ordered &= baseline.edits[probe_edit - 1].point <= baseline.edits[probe_edit].point;
        }
        string_print(S8("Q125_TABLE f={u64} c={u32} l={u32} p={u64} e={u32} ordered={u32}\\n"),
                     machine_quality_census_totals.functions, heap_count, merged_span_count,
                     nonzero, baseline.edit_count, (u32)probe_ordered);
        for (u32 probe_slot = 0; probe_slot < heap_count; probe_slot += 1)
        {
            string_print(S8("Q125_ROW f={u64} s={u32}"), machine_quality_census_totals.functions, probe_slot);
            for (u32 probe_region = 0; probe_region < merged_span_count; probe_region += 1)
            {
                MachineQualityTraffic probe_weight = candidate_region_traffic[(u64)probe_slot * merged_span_count + probe_region];
                if (probe_weight)
                {
                    string_print(S8(" {u32}:{u64}"), probe_region, probe_weight);
                }
            }
            string_print(S8("\\n"));
        }''')
replace_once('            BUSTER_QUALITY_COUNT(split_candidates, 1);', '''            BUSTER_QUALITY_COUNT(split_candidates, 1);
#if BUSTER_BENCH_ALLOCATIONS
            u64 probe_queries_before = machine_quality_census_totals.region_selection_passes;
#endif''')
replace_once('                MachineQualityTraffic best_traffic = traffic[best_region];', '''                MachineQualityTraffic best_traffic = traffic[best_region];
#if BUSTER_BENCH_ALLOCATIONS
                string_print(S8("Q125_DECISION f={u64} s={u32} r={u32} w={u64}\\n"),
                             machine_quality_census_totals.functions, candidate_slot, best_region, best_traffic);
#endif''')
replace_once('        }\n        if (!pinned_mask)', '''#if BUSTER_BENCH_ALLOCATIONS
            string_print(S8("Q125_QUERY f={u64} s={u32} q={u64}\\n"), machine_quality_census_totals.functions,
                         candidate_slot, machine_quality_census_totals.region_selection_passes - probe_queries_before);
#endif
        }
        if (!pinned_mask)''')
path.write_text(text)
print(json.dumps({'base_allocator_blob': blob, 'diagnostic_sha256': hashlib.sha256(text.encode()).hexdigest()}))
