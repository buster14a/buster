#!/usr/bin/env python3
"""Fixed #125 experiment adapter; reuse the existing selector runner primitives.

Temporary branch only. No acceptance threshold or generic benchmark framework.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import statistics
import subprocess

BASE = '2e942e80a87666409cf29d3e24a68d322b9e71fd'
HEAD = '6644f7a9dd9c9b360091c5c9d7a6eda75183f293'


def scan_overlay(root: Path) -> None:
    path = root / 'src/buster/lib/compiler/codegen/register_allocator_quality.c'
    text = path.read_text()
    if hashlib.sha256(text.encode()).hexdigest() != 'c9f872b7b28c2d8ab06659c5dadbc1c96de70001f89c375c2069cfa48e9cb591':
        raise RuntimeError('scan control requires the exact tested heap source')
    old = '                machine_quality_region_heap_build(region_heap, region_heap_count);'
    assert text.count(old) == 1
    text = text.replace(old, '                // Scan control: construction is identical, no heap preparation.\n                (void)region_heap_count;')
    old = '            u32 previous_region = UINT32_MAX;'
    assert text.count(old) == 1
    text = text.replace(old, old + '\n            MachineQualityTraffic previous_sparse_traffic = 0;')
    old = '                    MachineQualityRegionTraffic selected = machine_quality_region_heap_pop(region_heap, &region_heap_count);'
    assert text.count(old) == 1
    text = text.replace(old, '''                    MachineQualityRegionTraffic selected = {.traffic = 0, .region = UINT32_MAX};
                    BUSTER_QUALITY_COUNT(sparse_region_queries, 1);
                    for (u32 entry = 0; entry < region_heap_count; entry += 1)
                    {
                        MachineQualityRegionTraffic value = region_heap[entry];
                        if (previous_region != UINT32_MAX && (value.traffic > previous_sparse_traffic ||
                            (value.traffic == previous_sparse_traffic && value.region <= previous_region)))
                        {
                            continue;
                        }
                        if (machine_quality_region_precedes(value, selected))
                        {
                            selected = value;
                        }
                    }
                    previous_sparse_traffic = selected.traffic;''')
    path.write_text(text)
    print('scan_overlay_sha256=' + hashlib.sha256(text.encode()).hexdigest())


def complete_compiler(root: Path, evidence: Path, frozen: Path) -> None:
    spec = importlib.util.spec_from_file_location('selection_benchmark', root / 'tools/selection_benchmark.py')
    assert spec and spec.loader
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    bins = {name: (evidence / f'compiler-{name}').resolve() for name in ('base', 'heap', 'scan')}
    for compiler in bins.values():
        if not compiler.is_file():
            raise RuntimeError(f'missing compiler: {compiler}')
    plan = {'base': BASE, 'heap': HEAD, 'scan': 'exact HEAD plus retained scan-control.patch',
            'pairs': 20, 'warmups': 2, 'rounds': 2, 'forms': ['direct', 'memory'],
            'comparisons': [['base', 'base'], ['base', 'heap'], ['heap', 'scan']],
            'compilers': {name: runner.digest(path) for name, path in bins.items()},
            'scope': 'fresh-process complete QUALITY compiler; shared-host screening, not dedicated acceptance'}
    manifest = {str(path.relative_to(frozen)): runner.digest(path) for path in sorted(frozen.rglob('*')) if path.is_file()}
    plan['frozen_manifest_sha256'] = hashlib.sha256(json.dumps(manifest, sort_keys=True).encode()).hexdigest()
    (evidence / 'unity-plan.json').write_text(json.dumps(plan, indent=2)+'\n')
    (evidence / 'unity-input-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    rows = []
    expected = {}
    old_cwd = Path.cwd()
    try:
        os.chdir(frozen)
        output = (evidence / 'unity-output.o').resolve()
        with (evidence / 'unity-samples.jsonl').open('w') as raw:
            for comparison, (left, right) in enumerate(plan['comparisons']):
                for round_number in range(2):
                    for form in plan['forms']:
                        for pair in range(22):
                            for step in range(2):
                                side = (pair + step + round_number) & 1
                                name = (left, right)[side]
                                command = [str(bins[name]), 'cc', '-Isrc', '-Ibuild/generated', '-DBUSTER_UNITY_BUILD=1',
                                    '-DBUSTER_INCLUDE_TESTS=0', '-g0', '-O0', '-fregister-allocator=quality']
                                if form == 'memory':
                                    command += ['-fno-frontend-ssa']
                                command += ['-c', 'src/buster/apps/ide/ide.c', '-o', str(output)]
                                text, duration = runner.run(command, timeout=120)
                                digest = runner.digest(output)
                                if form not in expected:
                                    expected[form] = digest
                                if digest != expected[form]:
                                    raise RuntimeError(f'object mismatch: {name}/{form}: {digest} != {expected[form]}')
                                row = dict(comparison=comparison, left=left, right=right, round=round_number, form=form,
                                    pair=pair-2, side=side, compiler=name, ns=duration, output_sha256=digest, command=command,
                                    stdout_sha256=hashlib.sha256(text.encode()).hexdigest())
                                raw.write(json.dumps(row)+'\n'); raw.flush()
                                if pair >= 2:
                                    rows.append(row)
                        print(f'complete compiler: {left}/{right} round={round_number} form={form}', flush=True)
    finally:
        os.chdir(old_cwd)
    summaries = []
    for comparison, (left, right) in enumerate(plan['comparisons']):
        for round_number in range(2):
            for form in plan['forms']:
                selected = [row for row in rows if row['comparison']==comparison and row['round']==round_number and row['form']==form]
                paired = {}
                for row in selected:
                    paired.setdefault(row['pair'], {})[row['side']] = row['ns']
                assert len(paired)==20 and all(len(pair)==2 for pair in paired.values())
                ratios = [pair[1]/pair[0] for pair in paired.values()]
                summaries.append(dict(left=left,right=right,round=round_number,form=form,
                    left_median_ns=statistics.median([p[0] for p in paired.values()]),
                    right_median_ns=statistics.median([p[1] for p in paired.values()]),
                    median_pair_ratio=statistics.median(ratios),min_pair_ratio=min(ratios),max_pair_ratio=max(ratios)))
    result = dict(scope=plan['scope'],summary=summaries,output_hashes=expected)
    (evidence/'unity-summary.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result,indent=2))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=['scan-overlay', 'compiler'])
    parser.add_argument('--root', type=Path, default=Path.cwd())
    parser.add_argument('--evidence', type=Path)
    parser.add_argument('--frozen', type=Path)
    args = parser.parse_args()
    if args.command == 'scan-overlay':
        scan_overlay(args.root.resolve())
    else:
        if args.evidence is None or args.frozen is None:
            parser.error('compiler needs --evidence and --frozen')
        complete_compiler(args.root.resolve(), args.evidence.resolve(), args.frozen.resolve())


if __name__ == '__main__':
    main()
