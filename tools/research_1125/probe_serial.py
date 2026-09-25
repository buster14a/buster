"""Serial-lifetime correction to the retained #1125 frozen-source probe.

The original attempt is retained unchanged. This adapter moves the two
negative preparation tests before workers exist; module tests only read their
result. Observation totals include the two added test-only prepare attempts.
"""
from pathlib import Path
import probe as original
from probe import PIN, TREE, CG, PINS, mode, freeze

old = '''#if BUSTER_INCLUDE_TESTS
u32 research_1125_lookup_tests(void)
'''
new = '''#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL u32 research_1125_plan_test_mask;
// Even a rejected prepare request has a serial-only contract. Publish these
// negative test results during prewarm, not from a module with parked workers.
BUSTER_GLOBAL_LOCAL void research_1125_prepare_test_results(void)
{
    BusterX86MetadataExactPlan plan = {0};
    struct Research1125Key row = research_1125_keys[0];
    research_1125_plan_test_mask = 0;
    if (!buster_x86_metadata_exact_plan_prepare((BusterX86MetadataFormKey){.form_id = UINT32_MAX, .stable_hash = 1}, &plan)) research_1125_plan_test_mask |= 256u;
    if (!buster_x86_metadata_exact_plan_prepare((BusterX86MetadataFormKey){.form_id = row.form_id, .stable_hash = row.stable_hash ^ 1u}, &plan)) research_1125_plan_test_mask |= 512u;
}

u32 research_1125_lookup_tests(void)
'''
original.SHAPE = original.replace(original.SHAPE, old, new)
old = '''    BusterX86MetadataExactPlan plan = {0};
    if (!buster_x86_metadata_exact_plan_prepare((BusterX86MetadataFormKey){.form_id = UINT32_MAX, .stable_hash = 1}, &plan)) mask |= 256u;
    row = research_1125_keys[0];
    if (!buster_x86_metadata_exact_plan_prepare((BusterX86MetadataFormKey){.form_id = row.form_id, .stable_hash = row.stable_hash ^ 1u}, &plan)) mask |= 512u;
    return mask;
'''
original.SHAPE = original.replace(original.SHAPE, old, '    return mask | research_1125_plan_test_mask;\n')

def prepare(root: Path) -> None:
    original.prepare(root)
    path = root/CG/'machine_x86_64.c'
    old = '    machine_x64_metadata_shape_cache_ready = machine_x64_metadata_shape_cache_invalid_count == 0;'
    new = '#if BUSTER_1125_REPLAY && BUSTER_INCLUDE_TESTS\n    research_1125_prepare_test_results();\n#endif\n'+old
    path.write_text(original.replace(path.read_text(), old, new))
    # This read-only probe uses the same function as the registered module.
    # It is observation-only and absent from the uninstrumented candidate.
    path = root/'src/buster/apps/ide/ide.c'
    old = '    research_1125_report("artifact_complete");'
    new = old+'\n#if BUSTER_1125_REPLAY && BUSTER_INCLUDE_TESTS\n    fprintf(stderr, "B1125_TEST_MASK %u\\n", (unsigned)research_1125_lookup_tests());\n#endif'
    path.write_text(original.replace(path.read_text(), old, new))
