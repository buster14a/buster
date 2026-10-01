#!/usr/bin/env python3
"""Patch an isolated runner checkout; never a production policy change.

Uses existing FAST implementations, one compaction, frozen deterministic schedules.
All outputs are diagnostic work counts; no diagnostic clock is a benchmark.
"""
from pathlib import Path
import hashlib
import json
import subprocess

root = Path(__file__).resolve().parents[2]
path = root / "src/buster/lib/compiler/ir/ir_fast.c"
header = root / "src/buster/lib/compiler/ir/ir.h"
original = path.read_text()
text = original

def replace(old, new):
    global text
    assert text.count(old) == 1, (old[:90], text.count(old))
    text = text.replace(old, new)

replace("    u32 head = 0, tail = 0;\n    for (u32 index = 0; index < function->instruction_count; index += 1)",
        "    statistics->research_seed_rows += function->instruction_count;\n    u32 head = 0, tail = 0;\n    for (u32 index = 0; index < function->instruction_count; index += 1)")
replace("        sweep += 1;\n        for (u32 block = 0; block < function->block_count; block += 1)",
        "        sweep += 1;\n        pass->research_sweeps += 1;\n        pass->research_block_visits += function->block_count;\n        for (u32 block = 0; block < function->block_count; block += 1)")
replace("    IrFastPassStatistics* pass = statistics->passes + IR_FAST_PARAMETERS;",
        '''    IrFastPassStatistics* pass = statistics->passes + IR_FAST_PARAMETERS;
    String8 research_trace = os_get_environment_variable(S8("BUSTER_PHASE_ORDER_TRACE_PARAMETERS"));
    if (research_trace.length)
    {
        for (u32 block = 0; block < function->block_count; block += 1)
        {
            for (IrBlockParameter* parameter = function->blocks[block].first_parameter; parameter; parameter = parameter->next)
            {
                string_print(S8("PHASE_PARAM function={S8} block={u32} value={u32} incoming="), function->name, block, parameter->value.value);
                for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
                {
                    string_print(S8("{u32},"), ir_promote_root(replacements, incoming->value.value));
                }
                string_print(S8("\\n"));
            }
        }
    }''')
replace("    statistics->instructions_before += function->instruction_count;",
        "    statistics->instructions_before += function->instruction_count;\n    string_print(S8(\"PHASE_FUNCTION function={S8} instructions={u32} values={u32} blocks={u32}\\n\"),\n                 function->name, function->instruction_count, function->value_count, function->block_count);")
old = text[text.index("        u64 changes = 0;\n        for (u32 pass"):text.index("        if (changes)\n", text.index("        u64 changes = 0;\n        for (u32 pass"))]
new = '''        u64 changes = 0;
        u64 parameter_changes = 0;
        u32* research_uses = 0;
        u32* research_queue = 0;
        String8 schedule = os_get_environment_variable(S8("BUSTER_PHASE_ORDER_SCHEDULE"));
        if (!schedule.length) schedule = S8("FADP");
        BUSTER_CHECK(schedule.length <= 8);
        for (u32 ordinal = 0; ordinal < schedule.length; ordinal += 1)
        {
            u8 code = schedule.pointer[ordinal];
            bool selective = code == 'f' || code == 'd';
            u32 pass = code == 'F' || code == 'f' ? IR_FAST_FOLD : code == 'A' ? IR_FAST_ADDRESS :
                       code == 'D' || code == 'd' ? IR_FAST_DCE : IR_FAST_PARAMETERS;
            BUSTER_CHECK(code == 'F' || code == 'A' || code == 'D' || code == 'P' || code == 'f' || code == 'd');
            if (!(program->fast_passes & IR_FAST_PASS_BIT(pass)) || (selective && !parameter_changes)) continue;
            IrFastPassStatistics* measurement = statistics->passes + pass;
            IrFastPassStatistics before = *measurement;
            if (pass == IR_FAST_FOLD || pass == IR_FAST_ADDRESS)
            {
                ir_fast_fold(program, function, replacements, removed, (IrFastPass)pass, measurement);
            }
            else if (pass == IR_FAST_DCE)
            {
                if (!research_uses)
                {
                    research_uses = arena_allocate(arena, u32, function->value_count);
                    research_queue = arena_allocate(arena, u32, function->instruction_count);
                }
                memset(research_uses, 0, sizeof(u32) * function->value_count);
                measurement->research_value_clear += function->value_count;
                ir_fast_dce(program, function, replacements, removed, research_uses, research_queue, measurement);
            }
            else ir_fast_parameters(function, replacements, statistics);
            u64 delta = measurement->changes - before.changes;
            changes += delta;
            if (pass == IR_FAST_PARAMETERS) parameter_changes += delta;
            string_print(S8("PHASE_STEP function={S8} ordinal={u32} pass={u32} visits={u64} changes={u64} "
                            "setup_values={u64} seed_rows={u64} block_visits={u64} sweeps={u64}\\n"),
                         function->name, ordinal, pass, measurement->visits - before.visits, delta,
                         measurement->research_value_clear - before.research_value_clear,
                         measurement->research_seed_rows - before.research_seed_rows,
                         measurement->research_block_visits - before.research_block_visits,
                         measurement->research_sweeps - before.research_sweeps);
        }
'''
replace(old, new)
replace("    statistics->instructions_after += function->instruction_count;",
        "    statistics->instructions_after += function->instruction_count;\n    string_print(S8(\"PHASE_DONE function={S8} instructions={u32}\\n\"), function->name, function->instruction_count);")
# The diagnostic struct is transient and is never committed as production code.
h = header.read_text()
old_h = "struct IrFastPassStatistics\n{\n    u64 nanoseconds;\n    u64 visits;\n    u64 changes;\n};"
assert h.count(old_h) == 1
h = h.replace(old_h, old_h[:-3] + "\n    u64 research_seed_rows;\n    u64 research_value_clear;\n    u64 research_block_visits;\n    u64 research_sweeps;\n};")
path.write_text(text)
header.write_text(h)
evidence = root / "phase-order-evidence"
evidence.mkdir(exist_ok=True)
(evidence / "instrument.patch").write_text(subprocess.check_output(["git", "diff", "--", str(path), str(header)], cwd=root, text=True))
(evidence / "source-identities.json").write_text(json.dumps({
    "base": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
    "original_ir_fast_sha256": hashlib.sha256(original.encode()).hexdigest(),
    "diagnostic_ir_fast_sha256": hashlib.sha256(text.encode()).hexdigest(),
    "diagnostic_ir_h_sha256": hashlib.sha256(h.encode()).hexdigest(),
    "schedule_length_cap": 8,
    "production_mutated": False,
}, indent=2) + "\n")
