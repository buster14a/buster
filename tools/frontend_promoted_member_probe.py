#!/usr/bin/env python3
"""Disposable source/work-count diagnostic; not a benchmark or production patch.

Run only on a standard GitHub correctness runner. No timing, PMU, host-policy,
service access, caches, or generated binding edits. The instrumented compiler
must not be used as a performance baseline. Preserve every result, including
unreached hypotheses and failed source anchors.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

SOURCE = Path('src/buster/lib/compiler/frontend/c/c_gen.c')

def command(argv: list[str], output: Path, *, timeout: int = 120) -> dict:
    try:
        result = subprocess.run(argv, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=timeout)
        text, code = result.stdout, result.returncode
    except subprocess.TimeoutExpired as exc:
        text = str(exc.stdout or '') + '\nCOMMAND TIMEOUT\n'
        code = 124
    output.write_text(text)
    return {'argv': argv, 'exit': code, 'log': output.name}

def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise RuntimeError(f'Expected one source anchor, got {text.count(old)}: {old!r}')
    return text.replace(old, new, 1)

def inspect(out: Path) -> None:
    identities = {}
    for path in [Path('AGENTS.md'), Path('docs/agents/frontend.md'),
                 Path('docs/agents/benchmarking.md'), Path('src/buster/lib/arena.h'),
                 SOURCE, SOURCE.with_name('c_parse.c'), SOURCE.with_name('c_source.c')]:
        identities[str(path)] = digest(path)
        # Retain the real source orientation rather than historical excerpts.
        head = '\n'.join(path.read_text().splitlines()[:100])
        (out / (path.name + '.head.txt')).write_text(head + '\n')
    identities['commit'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    identities['tree'] = subprocess.check_output(['git', 'rev-parse', 'HEAD^{tree}'], text=True).strip()
    identities['c_gen_blob'] = subprocess.check_output(['git', 'hash-object', str(SOURCE)], text=True).strip()
    newest = subprocess.check_output(['python3', 'tools/new_audit.py', '--newest'], text=True).strip()
    identities['newest_audit_command_output'] = newest
    if Path(newest).is_file():
        (out / 'newest-audit.md').write_text(Path(newest).read_text())
        print('NEWEST_AUDIT', newest)
        print('\n'.join(Path(newest).read_text().splitlines()[:45]))
    (out / 'identities.json').write_text(json.dumps(identities, indent=2) + '\n')
    print('SOURCE_IDENTITIES', json.dumps(identities, sort_keys=True))

def instrument(out: Path) -> None:
    text = SOURCE.read_text()
    begin = text.index('BUSTER_C_INTERNAL bool c_ir_promoted_member_queued(')
    path_begin = text.index('BUSTER_C_INTERNAL bool c_ir_promoted_member_path(', begin)
    end = text.index('BUSTER_C_INTERNAL bool c_ir_promoted_member_type(', path_begin)
    queued, path = text[begin:path_begin], text[path_begin:end]
    (out / 'original-promoted-member.c.txt').write_text(text[begin:end])
    queued = once(queued, 'u32 count, IrTypeId type)',
                  'u32 count, IrTypeId type, u64* probe_comparisons)')
    queued = once(queued, '        if (queued[index].type.value == type.value)',
                  '        *probe_comparisons += 1;\n        if (queued[index].type.value == type.value)')
    path = once(path, '    u32 capacity = builder->program->types.count;',
                '    u64 probe_comparisons = 0;\n    u64 probe_fields = 0;\n'
                '    u32 capacity = builder->program->types.count;')
    path = once(path, '            IrField* field = type->fields + field_index;',
                '            probe_fields += 1;\n            IrField* field = type->fields + field_index;')
    path = once(path, 'c_ir_promoted_member_queued(work, work_count, field->type)',
                'c_ir_promoted_member_queued(work, work_count, field->type, &probe_comparisons)')
    path = once(path, '    return found && !ambiguous;',
                '    fprintf(stderr, "PROMOTED_WORK types=%u queue=%u comparisons=%llu fields=%llu requested=%llu found=%u ambiguous=%u\\n", '
                'capacity, work_count, (unsigned long long)probe_comparisons, '
                '(unsigned long long)probe_fields, '
                '(unsigned long long)(sizeof(CIrPromotedMemberWork) * (u64)capacity), '
                '(unsigned)found, (unsigned)ambiguous);\n    return found && !ambiguous;')
    SOURCE.write_text('#include <stdio.h> /* disposable correctness-count probe */\n' +
                      text[:begin] + queued + path + text[end:])
    patch = subprocess.check_output(['git', 'diff', '--', str(SOURCE)], text=True)
    (out / 'instrumentation.patch').write_text(patch)
    (out / 'instrumented-source.sha256').write_text(digest(SOURCE) + '\n')
    print('INSTRUMENTED_SOURCE_SHA256', digest(SOURCE))

def source_text(unrelated: int, width: int, queries: int, flat: bool) -> str:
    lines = [f'struct U{i:04d} {{ unsigned unrelated; }};' for i in range(unrelated)]
    lines.append('struct Root {')
    for i in range(width):
        lines.append(f'    unsigned f{i:04d};' if flat else
                     f'    struct {{ unsigned f{i:04d}; }};')
    lines += ['};', 'unsigned probe(void)', '{', '    struct Root value;',
              '    unsigned result = 0;']
    lines += ['    result += sizeof(value.f0000);'] * queries
    lines += ['    return result;', '}', '']
    return '\n'.join(lines)

def run_cases(out: Path, base: str, probe: str) -> int:
    # Vary unrelated type declarations, anonymous breadth, and query-site count
    # independently. Both compilers consume each identical file/path/argv.
    specs = {(0, k, 1, False) for k in (1, 2, 4, 8, 16, 32, 64)}
    specs |= {(n, 8, 1, False) for n in (0, 64, 256, 1024)}
    specs |= {(0, 8, q, False) for q in (1, 2, 4, 8)}
    specs |= {(n, 8, 1, True) for n in (0, 256, 1024)}
    records = []
    failures = 0
    pattern = re.compile(r'PROMOTED_WORK types=(\d+) queue=(\d+) comparisons=(\d+) fields=(\d+) requested=(\d+) found=(\d+) ambiguous=(\d+)')
    keys = ('types', 'queue', 'comparisons', 'fields', 'requested', 'found', 'ambiguous')
    for unrelated, width, queries, flat in sorted(specs):
        case = out / f'n{unrelated}-k{width}-q{queries}-flat{int(flat)}'
        case.mkdir()
        source = case / 'input.c'
        source.write_text(source_text(unrelated, width, queries, flat))
        row = {'unrelated_declarations': unrelated, 'anonymous_width': 0 if flat else width,
               'root_fields': width, 'query_sites': queries, 'flat': flat,
               'source_bytes': source.stat().st_size, 'source_sha256': digest(source)}
        row['oracle'] = command(['clang', '-std=c11', '-Werror', '-fsyntax-only', str(source)], case / 'oracle.log')
        objects = []
        for name, binary in [('base', base), ('probe', probe)]:
            # Reuse the output and metrics paths to avoid source/path differences.
            obj, metrics = case / 'out.o', case / 'source.metrics'
            if obj.exists():
                obj.unlink()
            if metrics.exists():
                metrics.unlink()
            row[name] = command([binary, 'cc', '-g0', '-std=c11', '-c', str(source),
                                 '-fsource-metrics=' + str(metrics), '-o', str(obj)], case / (name + '.log'))
            row[name]['object_sha256'] = digest(obj) if obj.is_file() else None
            objects.append(row[name]['object_sha256'])
            if metrics.is_file():
                (case / (name + '.metrics')).write_bytes(metrics.read_bytes())
        text = (case / 'probe.log').read_text()
        row['work'] = [dict(zip(keys, map(int, match))) for match in pattern.findall(text)]
        row['object_identical'] = bool(objects[0]) and objects[0] == objects[1]
        row['query_path_reached'] = bool(row['work'])
        row['total_queue_comparisons'] = sum(w['comparisons'] for w in row['work'])
        row['query_calls'] = len(row['work'])
        row['predicted_distinct_star_comparisons_per_call'] = 0 if flat else width * (width + 1) // 2
        row['status'] = ('passed' if all(row[k]['exit'] == 0 for k in ('oracle', 'base', 'probe'))
                         and row['object_identical'] else 'failed')
        failures += row['status'] != 'passed'
        records.append(row)
        print('CASE_RESULT', json.dumps(row, sort_keys=True), flush=True)
    summary = {'cases': records, 'failures': failures,
               'scope': 'Hosted compile/oracle/count probe only. No candidate optimization, full regression, self-host, sanitizer, or performance acceptance.'}
    (out / 'results.json').write_text(json.dumps(summary, indent=2) + '\n')
    print('PROBE_SUMMARY', json.dumps({'cases': len(records), 'failures': failures,
          'reached_cases': sum(r['query_path_reached'] for r in records)}, sort_keys=True))
    return int(bool(failures))

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['inspect', 'instrument', 'run'])
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--base')
    parser.add_argument('--probe')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    code = 0
    if args.mode == 'inspect':
        inspect(args.out)
    elif args.mode == 'instrument':
        instrument(args.out)
    else:
        if not args.base or not args.probe:
            parser.error('run requires --base and --probe')
        code = run_cases(args.out, args.base, args.probe)
    return code

if __name__ == '__main__':
    sys.exit(main())
