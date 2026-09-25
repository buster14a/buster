#!/usr/bin/env python3
"""Frozen-source #1125 experiment. File transformation only; never builds locally.

Production changes emitted by this tool are C. Python is only experiment
orchestration, as in existing retained repository probes. No third-party modules.
Every changed source is pinned before a patch is applied. All emitted files,
including the generated keys, belong to the disposable experiment, not main.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import re
from pathlib import Path

PIN = 'd9e736e2cf08804a2c603d153e9fb608f18c5c49'
TREE = 'ae9f274b6768b023fe6e0ef80a161e14b290e0a2'
CG = 'src/buster/lib/compiler/codegen/'
MD = 'src/buster/lib/compiler/assembly/x86_64_metadata.c'
IDE = 'src/buster/apps/ide/ide.c'
TEST = 'src/buster/tests/compiler/codegen/machine_test.c'
PINS = {
    CG+'machine_x86_64.c': '6133a35abc7ca5fb07bf50ea8d1aa16b2fa84c74',
    MD: '3b73954ef49ad27f67bcb3bba81de3e3a012d928',
    IDE: '9c64fb34c6c165e4ac233d1d0531c341710a4fbb',
    TEST: '603dfbc15d6d7785db76c75058d727007919f2f1',
}
INCLUDE = '#include <buster/lib/compiler/codegen/research_1125_mode.h>\n'

SHAPE = r'''// Disposable #1125 frozen-source partial evaluation, not a production binding.
// Only the original closed eager definitions call this adapter. Runtime machine
// operands still pass through the original checked exact encoder. No lazy fill.
#if BUSTER_1125_REPLAY
typedef struct Research1125Key Research1125Key;
struct Research1125Key { u64 signature; u64 guard; u64 stable_hash; u32 form_id; u32 reserved; };
#include <buster/lib/compiler/codegen/research_1125_keys.h>
BUSTER_GLOBAL_LOCAL u32 research_1125_cursor;

BUSTER_GLOBAL_LOCAL BusterX86MetadataSelectResult research_1125_lookup(
    MachineX64MetadataShapeHashes hashes, struct Research1125Key const* rows,
    u32 count, u32 ordinal)
{
    BusterX86MetadataSelectResult result = {
        .status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT,
        .form_id = UINT32_MAX, .failure_form_id = UINT32_MAX,
        .selected_memory_operand = UINT8_MAX,
    };
    if (rows && ordinal < count)
    {
        struct Research1125Key const* row = rows + ordinal;
        if (row->signature == hashes.signature && row->guard == hashes.guard &&
            row->stable_hash && row->form_id != UINT32_MAX)
        {
            result.status = BUSTER_X86_METADATA_ENCODE_SUCCESS;
            result.form_id = row->form_id;
            result.stable_hash = row->stable_hash;
        }
    }
    return result;
}

#if BUSTER_INCLUDE_TESTS
u32 research_1125_lookup_tests(void)
{
    u32 mask = 0;
    struct Research1125Key row = research_1125_keys[0];
    MachineX64MetadataShapeHashes hashes = {.signature = row.signature, .guard = row.guard};
    BusterX86MetadataSelectResult ok = research_1125_lookup(hashes, &row, 1, 0);
    if (ok.status == BUSTER_X86_METADATA_ENCODE_SUCCESS && ok.form_id == row.form_id && ok.stable_hash == row.stable_hash) mask |= 1u;
    if (research_1125_lookup(hashes, &row, 1, 1).status != BUSTER_X86_METADATA_ENCODE_SUCCESS) mask |= 2u;
    if (research_1125_lookup(hashes, &row, 0, 0).status != BUSTER_X86_METADATA_ENCODE_SUCCESS) mask |= 4u;
    if (research_1125_lookup(hashes, 0, 1, 0).status != BUSTER_X86_METADATA_ENCODE_SUCCESS) mask |= 8u;
    hashes.signature ^= 1u;
    if (research_1125_lookup(hashes, &row, 1, 0).status != BUSTER_X86_METADATA_ENCODE_SUCCESS) mask |= 16u;
    hashes.signature ^= 1u; hashes.guard ^= 1u;
    if (research_1125_lookup(hashes, &row, 1, 0).status != BUSTER_X86_METADATA_ENCODE_SUCCESS) mask |= 32u;
    hashes.guard ^= 1u;
    row.stable_hash = 0;
    if (research_1125_lookup(hashes, &row, 1, 0).status != BUSTER_X86_METADATA_ENCODE_SUCCESS) mask |= 64u;
    row = research_1125_keys[0]; row.form_id = UINT32_MAX;
    if (research_1125_lookup(hashes, &row, 1, 0).status != BUSTER_X86_METADATA_ENCODE_SUCCESS) mask |= 128u;
    BusterX86MetadataExactPlan plan = {0};
    if (!buster_x86_metadata_exact_plan_prepare((BusterX86MetadataFormKey){.form_id = UINT32_MAX, .stable_hash = 1}, &plan)) mask |= 256u;
    row = research_1125_keys[0];
    if (!buster_x86_metadata_exact_plan_prepare((BusterX86MetadataFormKey){.form_id = row.form_id, .stable_hash = row.stable_hash ^ 1u}, &plan)) mask |= 512u;
    return mask;
}
#endif
#endif

BUSTER_GLOBAL_LOCAL BusterX86MetadataSelectResult research_1125_select(
    BusterX86MetadataPhysicalQuery query, MachineX64MetadataShapeHashes hashes)
{
#if BUSTER_1125_REPLAY
    BusterX86MetadataSelectResult selected = research_1125_lookup(
        hashes, research_1125_keys, BUSTER_ARRAY_LENGTH(research_1125_keys), research_1125_cursor);
    research_1125_cursor += 1;
#if BUSTER_1125_OBSERVE
    research_1125_counts[7] += 1;
#endif
#if BUSTER_1125_ORACLE
    BusterX86MetadataSelectResult oracle = buster_x86_metadata_select_form(query);
    // These are all selection-result fields consumed by the original caller.
    // Preserve its subsequent plan, feature/token and duplicate-consistency checks.
    if (oracle.status != selected.status || oracle.form_id != selected.form_id || oracle.stable_hash != selected.stable_hash)
    {
        selected.status = BUSTER_X86_METADATA_ENCODE_INVALID_INPUT;
    }
#else
    (void)query;
#endif
#else
    BusterX86MetadataSelectResult selected = buster_x86_metadata_select_form(query);
#endif
#if BUSTER_1125_OBSERVE
    if (getenv("B1125_DUMP_KEYS"))
    {
        fprintf(stderr, "B1125_KEY %llu %llu %llu %u %u\n",
                (unsigned long long)hashes.signature, (unsigned long long)hashes.guard,
                (unsigned long long)selected.stable_hash, (unsigned)selected.form_id, (unsigned)(selected.status == BUSTER_X86_METADATA_ENCODE_SUCCESS));
    }
#endif
    return selected;
}
'''
REPORT = r'''
#if BUSTER_1125_OBSERVE
u64 research_1125_counts[8];
void research_1125_report(char const* phase)
{
    fprintf(stderr, "B1125_WORK %s select=%llu form=%llu pattern_uncached=%llu plan=%llu encode=%llu decode=%llu normalize=%llu replay=%llu\n",
            phase, (unsigned long long)research_1125_counts[0], (unsigned long long)research_1125_counts[1],
            (unsigned long long)research_1125_counts[2], (unsigned long long)research_1125_counts[3],
            (unsigned long long)research_1125_counts[4], (unsigned long long)research_1125_counts[5],
            (unsigned long long)research_1125_counts[6], (unsigned long long)research_1125_counts[7]);
}
#endif
'''

def replace(text: str, old: str, new: str) -> str:
    n = text.count(old)
    if n != 1:
        raise ValueError(f'anchor count {n}, expected one: {old[:110]!r}')
    return text.replace(old, new, 1)

def count_statement(index: int) -> str:
    return f'\n#if BUSTER_1125_OBSERVE\n    research_1125_counts[{index}] += 1;\n#endif\n'

def mode(root: Path, name: str) -> None:
    observe, replay, oracle = {'record': (1,0,0), 'observed-replay': (1,1,0), 'oracle': (0,1,1), 'candidate': (0,1,0)}[name]
    text = f'''#ifndef RESEARCH_1125_MODE_H\n#define RESEARCH_1125_MODE_H\n#define BUSTER_1125_OBSERVE {observe}\n#define BUSTER_1125_REPLAY {replay}\n#define BUSTER_1125_ORACLE {oracle}\n#if BUSTER_1125_OBSERVE\n#include <stdio.h>\n#include <stdlib.h>\nextern u64 research_1125_counts[8];\nBUSTER_F_DECL void research_1125_report(char const* phase);\n#endif\n#if BUSTER_1125_REPLAY && BUSTER_INCLUDE_TESTS\nBUSTER_F_DECL u32 research_1125_lookup_tests(void);\n#endif\n#endif\n'''
    (root / CG / 'research_1125_mode.h').write_text(text)

def prepare(root: Path) -> None:
    source = {}
    for path, expected in PINS.items():
        data = (root/path).read_bytes()
        actual = hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
        if actual != expected:
            raise ValueError(f'stale source: {path}: {actual} != {expected}')
        source[path] = data.decode()
    machine = source[CG+'machine_x86_64.c']
    hashes = '    MachineX64MetadataShapeHashes hashes = machine_x64_metadata_shape_hashes(mnemonic, operands, operand_count, features, attributes);\n'
    start = machine.index('BUSTER_GLOBAL_LOCAL bool machine_x64_metadata_shape_cache_add(')
    end = machine.index('BUSTER_GLOBAL_LOCAL BusterX86MetadataMachineExactToken const* machine_x64_metadata_shape_cache_find(', start)
    body = machine[start:end]
    body = replace(body, hashes, '')
    body = replace(body, '    BusterX86MetadataSelectResult selected = buster_x86_metadata_select_form(query);',
                   hashes+'    BusterX86MetadataSelectResult selected = research_1125_select(query, hashes);')
    machine = machine[:start]+'#include <buster/lib/compiler/codegen/research_1125_shape.h>\n\n'+body+machine[end:]
    machine = INCLUDE + machine
    machine = replace(machine, '    if (machine_x64_metadata_shape_cache_ready) return;\n',
                      '    if (machine_x64_metadata_shape_cache_ready) return;\n#if BUSTER_1125_REPLAY\n    research_1125_cursor = 0;\n#endif\n#if BUSTER_1125_OBSERVE\n    research_1125_report("shape_before");\n#endif\n')
    machine = replace(machine, '    machine_x64_metadata_shape_cache_publish_slots();\n',
                      '    machine_x64_metadata_shape_cache_publish_slots();\n#if BUSTER_1125_REPLAY\n    if (research_1125_cursor != BUSTER_ARRAY_LENGTH(research_1125_keys)) machine_x64_metadata_shape_cache_invalid_count += 1;\n#endif\n#if BUSTER_1125_OBSERVE\n    research_1125_report("shape_after");\n#endif\n')
    machine = replace(machine, '    machine_x64_exact_opcode_map_ready = true;\n',
                      '    machine_x64_exact_opcode_map_ready = true;\n#if BUSTER_1125_OBSERVE\n    research_1125_report("exact_after");\n#endif\n')
    metadata = source[MD]
    metadata = replace(metadata, '#include <buster/lib/os.h>\n', '#include <buster/lib/os.h>\n'+INCLUDE+REPORT)
    for symbol, index in [('buster_x86_metadata_select_form', 0), ('buster_x86_metadata_form', 1),
                          ('buster_x86_metadata_exact_plan_prepare', 3), ('buster_x86_metadata_emit_exact_machine', 4)]:
        pattern = r'(?m)^(?:bool|BusterX86MetadataSelectResult|BusterX86MetadataEmitResult) '+symbol+r'\([^;]+?\)\n\{\n'
        matches = list(re.finditer(pattern, metadata))
        if len(matches) != 1:
            raise ValueError(f'function anchor: {symbol}, {len(matches)}')
        pos = matches[0].end()
        metadata = metadata[:pos]+count_statement(index)+metadata[pos:]
    metadata = replace(metadata, '    buster_x86_metadata_tables_decoding = true;\n',
                       '    buster_x86_metadata_tables_decoding = true;\n'+count_statement(5))
    metadata = replace(metadata, '    BusterX86MetadataPatternSemantics pattern;\n    memset(&pattern, 0, sizeof(pattern));',
                       count_statement(2)+'    BusterX86MetadataPatternSemantics pattern;\n    memset(&pattern, 0, sizeof(pattern));')
    anchor = '            buster_x86_metadata_copy_form(buster_x86_metadata_form_record(form_id), form_id, &buster_x86_metadata_normalized_forms[form_id]);'
    metadata = replace(metadata, anchor, count_statement(6)+anchor)
    ide = replace(source[IDE], '#include <buster/lib/compiler/driver/driver.h>\n', '#include <buster/lib/compiler/driver/driver.h>\n'+INCLUDE)
    start = ide.index('BUSTER_GLOBAL_LOCAL ProcessResult run_c_compiler(void)')
    end = ide.index('#if BUSTER_INCLUDE_TESTS', start)
    body = ide[start:end]
    body = replace(body, '    arena_destroy(arena, 1);\n    return result;',
                   '    arena_destroy(arena, 1);\n#if BUSTER_1125_OBSERVE\n    research_1125_report("artifact_complete");\n#endif\n    return result;')
    ide = ide[:start]+body+ide[end:]
    test = source[TEST]
    # Include after the first project header has supplied core types.
    line = next(x for x in test.splitlines(True) if x.startswith('#include '))
    test = replace(test, line, line+INCLUDE)
    anchor = '    MachineX64ExactMapAudit exact_map = machine_x86_64_exact_map_audit();'
    checks = '\n#if BUSTER_1125_REPLAY\n    u32 research_1125_mask = research_1125_lookup_tests();\n'
    checks += ''.join(f'    BUSTER_TEST(arguments, (research_1125_mask & {1<<i}u) != 0);\n' for i in range(10))
    test = replace(test, anchor, anchor+checks+'#endif\n')
    # Commit no partial transformed source if any anchor/pin failed above.
    for path, text in [(CG+'machine_x86_64.c', machine), (MD, metadata), (IDE, ide), (TEST, test)]:
        (root/path).write_text(text)
    (root/CG/'research_1125_shape.h').write_text(SHAPE)
    mode(root, 'record')

def freeze(log: Path, output: Path) -> None:
    rows = []
    for line in log.read_text().splitlines():
        if line.startswith('B1125_KEY '):
            fields = line.split()
            if len(fields) != 6:
                raise ValueError(f'malformed key: {line}')
            signature, guard, stable_hash, form_id, status = map(int, fields[1:])
            if not (0 <= signature < 2**64 and 0 <= guard < 2**64 and 0 < stable_hash < 2**64 and 0 <= form_id < 11013):
                raise ValueError(f'invalid key: {line}')
            rows.append((signature, guard, stable_hash, form_id, status))
    if not rows or len(rows) > 512 or {x[4] for x in rows} != {1}:
        raise ValueError('empty, oversized or inconsistent selection census')
    # The caller must also prove the generating compile succeeded and zero invalid
    # shape rows. Never use this as a general mnemonic selector or accept stale data.
    text = '// Derived at '+PIN+' by the retained successful generic-selection census.\n'
    text += 'BUSTER_GLOBAL_LOCAL struct Research1125Key const research_1125_keys[] = {\n'
    for a,b,c,d,_ in rows:
        text += f'    {{.signature = UINT64_C({a}), .guard = UINT64_C({b}), .stable_hash = UINT64_C({c}), .form_id = {d}u}},\n'
    text += '};\nBUSTER_CT_CHECK(sizeof(struct Research1125Key) == 32);\n'
    output.write_text(text)
    output.with_suffix('.json').write_text(json.dumps({'source_commit':PIN, 'source_tree':TREE, 'rows':len(rows),
        'row_bytes':32, 'table_bytes':32*len(rows), 'source_bytes':len(text.encode()),
        'log_sha256':hashlib.sha256(log.read_bytes()).hexdigest(), 'header_sha256':hashlib.sha256(text.encode()).hexdigest()}, indent=2)+'\n')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=['prepare','mode','freeze'])
    parser.add_argument('path', type=Path)
    parser.add_argument('argument', nargs='?')
    args = parser.parse_args()
    if args.operation == 'prepare': prepare(args.path.resolve())
    elif args.operation == 'mode': mode(args.path.resolve(), args.argument)
    else: freeze(args.path, Path(args.argument))
