#!/usr/bin/env python3
"""Temporary experiment transport. No benchmark logic; never ship in product PR."""
from pathlib import Path
import hashlib
import subprocess

root = Path('.')
quality = root / 'src/buster/lib/compiler/codegen/register_allocator_quality.c'
header = root / 'src/buster/lib/compiler/codegen/register_allocator_quality_internal.h'

def blob(data: bytes) -> str:
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()

assert blob(quality.read_bytes()) == '2ce4bfdfb73d07cd9a127812700108403b770de8'
assert blob(header.read_bytes()) == 'ce03f8c884dd7674a0b10155922c3ce51de7f1b1'
s = header.read_text()
new = '''// Function-local sparse traffic, keyed by the pre-heap candidate slot. The
// Inputs use the baseline's valid subjects and positive instruction weights. The
// constructor falls back to dense storage unless its worst-case allocation is
// smaller; offsets == 0 denotes that fallback. Rows initially have increasing
// region IDs. Probing may heapify/permutate a row, never change its key/value set.
typedef struct MachineEdit MachineEdit;
typedef struct MachineQualityRegionTraffic MachineQualityRegionTraffic;
struct MachineQualityRegionTraffic
{
    MachineQualityTraffic traffic;
    u32 region;
};

typedef struct MachineQualitySparseRegions MachineQualitySparseRegions;
struct MachineQualitySparseRegions
{
    MachineQualityRegionTraffic* entries;
    u32* offsets;
};

BUSTER_F_DECL MachineQualitySparseRegions machine_quality_sparse_regions_build(Arena* arena, u64 const* spans, u32 region_count,
    u32 candidate_count, u32 const* candidate_indices, MachineEdit const* edits, u32 edit_count,
    u32 const* instruction_weights, u32 candidate_edit_limit);
BUSTER_F_DECL void machine_quality_region_heap_sift(MachineQualityRegionTraffic* heap, u32 count, u32 root);
BUSTER_F_DECL void machine_quality_region_heap_build(MachineQualityRegionTraffic* heap, u32 count);
// Exhaustion returns {0, UINT32_MAX}. Popping only permutes the complete row,
// including its inactive suffix, so rebuilding it starts the same enumeration.
BUSTER_F_DECL MachineQualityRegionTraffic machine_quality_region_heap_pop(MachineQualityRegionTraffic* heap, u32* count);

#if BUSTER_INCLUDE_TESTS
typedef struct MachineFunction MachineFunction;
typedef struct MachineStackPlacement MachineStackPlacement;
BUSTER_F_DECL MachineStackPlacement machine_quality_placement_build_regions_test(Arena* arena, MachineFunction* function, bool dense);
#endif

'''
assert s.count('// Private QUALITY work census.') == 1
s = s.replace('// Private QUALITY work census.', new + '// Private QUALITY work census.', 1)
needle = '    X(candidate_region_clear_bytes) \\\n'
assert s.count(needle) == 1
extra = ['sparse_region_tables', 'sparse_region_entries', 'sparse_region_storage_bytes', 'sparse_region_construction_edits',
         'sparse_region_order_fallbacks', 'sparse_region_heap_entries', 'sparse_region_heap_steps', 'sparse_region_queries']
s = s.replace(needle, needle + ''.join('    X(' + name + ') \\\n' for name in extra), 1)
header.write_text(s)
s = quality.read_text()
anchor = '// The merged loop region containing an instruction, or UINT32_MAX.'
assert s.count(anchor) == 1
s = s.replace(anchor, Path('tools/quality_125_sparse_helpers.c').read_text() + '\n' + anchor, 1)
s = s.replace('    u32 heap_count = 0;', '    u32 heap_count = 0;\n    u32 candidate_edit_limit = 0;', 1)
s = s.replace('        candidate_indices[register_index] = heap_count;', '        candidate_indices[register_index] = heap_count;\n        candidate_edit_limit += baseline_traffic_counts[register_index];', 1)
s = s.replace('    MachineQualityTraffic* candidate_region_traffic = 0;', '    MachineQualityTraffic* candidate_region_traffic = 0;\n    MachineQualitySparseRegions sparse_regions = {0};', 1)
start = s.index('        BUSTER_QUALITY_COUNT(candidate_region_clear_bytes,')
end = s.index('\n    }\n#if BUSTER_BENCH_ALLOCATIONS', start)
block = s[start:end]
s = s[:start] + '''        sparse_regions = machine_quality_sparse_regions_build(scratch.arena, loop_spans, merged_span_count, heap_count,
            candidate_indices, baseline.edits, baseline.edit_count, instruction_weights, candidate_edit_limit);
        if (!sparse_regions.offsets)
        {
''' + ''.join('    ' + line + '\n' for line in block.splitlines()).rstrip() + '''
        }''' + s[end:]
s = s.replace('    if (candidate_region_traffic)\n    {\n        u64 cells', '    if (candidate_region_traffic || sparse_regions.offsets)\n    {\n        u64 cells', 1)
s = s.replace('        u64 nonzero = 0;\n        for (u64 cell_index = 0; cell_index < cells; cell_index += 1)', '        u64 nonzero = sparse_regions.offsets ? sparse_regions.offsets[heap_count] : 0;\n        for (u64 cell_index = 0; candidate_region_traffic && cell_index < cells; cell_index += 1)', 1)
s = s.replace('!allow_splits || !candidate_region_traffic || candidate_slot', '!allow_splits || (!candidate_region_traffic && !sparse_regions.offsets) || candidate_slot', 1)
s = s.replace('            u32 previous_region = UINT32_MAX;\n            while (!assigned)', '''            u32 previous_region = UINT32_MAX;
            MachineQualityRegionTraffic* region_heap = 0;
            u32 region_heap_count = 0;
            if (sparse_regions.offsets)
            {
                region_heap = sparse_regions.entries + sparse_regions.offsets[candidate_slot];
                region_heap_count = sparse_regions.offsets[candidate_slot + 1] - sparse_regions.offsets[candidate_slot];
                machine_quality_region_heap_build(region_heap, region_heap_count);
            }
            while (!assigned)''', 1)
a = '''                BUSTER_QUALITY_COUNT(region_selection_passes, 1);
                BUSTER_QUALITY_COUNT(region_selection_cells, merged_span_count);
                MachineQualityTraffic const* traffic = candidate_region_traffic + (u64)candidate_slot * merged_span_count;
                u32 best_region = machine_quality_region_next(traffic, merged_span_count, previous_region);'''
b = '''                u32 best_region;
                MachineQualityTraffic best_traffic;
                if (sparse_regions.offsets)
                {
                    MachineQualityRegionTraffic selected = machine_quality_region_heap_pop(region_heap, &region_heap_count);
                    best_region = selected.region;
                    best_traffic = selected.traffic;
                }
                else
                {
                    BUSTER_QUALITY_COUNT(region_selection_passes, 1);
                    BUSTER_QUALITY_COUNT(region_selection_cells, merged_span_count);
                    MachineQualityTraffic const* traffic = candidate_region_traffic + (u64)candidate_slot * merged_span_count;
                    best_region = machine_quality_region_next(traffic, merged_span_count, previous_region);
                    best_traffic = best_region == UINT32_MAX ? 0 : traffic[best_region];
                }'''
assert s.count(a) == 1
s = s.replace(a, b, 1).replace('                MachineQualityTraffic best_traffic = traffic[best_region];\n', '', 1)
s = s.replace('// pinned_values records', '// machine_quality_sparse_regions_build stores sparse traffic only when bounded\n// below the dense table; region heaps preserve immutable traffic/ID ranking.\n// pinned_values records', 1)
quality.write_text(s)
p = root / 'src/buster/tests/compiler/codegen/machine_test.c'
s = p.read_text()
anchor = 'UnitTestResult machine_tests(UnitTestArguments* arguments)'
assert s.count(anchor) == 1
s = s.replace(anchor, '#include <buster/tests/compiler/codegen/quality_regions_test_internal.h>\n\n' + anchor, 1)
s = s.replace('    BUSTER_TEST_FIXTURE(arguments, machine_test_quality_traffic);', '    BUSTER_TEST_FIXTURE(arguments, machine_test_quality_traffic);\n    BUSTER_TEST_FIXTURE(arguments, machine_test_quality_regions);\n    BUSTER_TEST_FIXTURE(arguments, machine_test_quality_region_placements);', 1)
p.write_text(s)
p = root / 'CMakeLists.txt'
s = p.read_text().replace('    src/buster/tests/compiler/codegen/machine_test.h', '    src/buster/tests/compiler/codegen/machine_test.h\n    src/buster/tests/compiler/codegen/quality_regions_test_internal.h', 1)
p.write_text(s)
subprocess.run(['git', 'diff', '--check'], check=True)
for path in [quality, header, root/'CMakeLists.txt', root/'src/buster/tests/compiler/codegen/machine_test.c', root/'src/buster/tests/compiler/codegen/quality_regions_test_internal.h']:
    print(hashlib.sha256(path.read_bytes()).hexdigest(), path)
