"""Classify retained Buster CI build intervals; never execute captured commands.

Reads a hash-pinned original/published evidence.zip. Classification joins the
native plan, phase summary, coverage row IDs and configured CMake cache.
This is diagnostic accounting, not a phase-journal or performance validator.
"""
from __future__ import annotations
import argparse
import csv
import hashlib
import io
import json
from collections import Counter, defaultdict
from decimal import Decimal
from pathlib import Path
import zipfile

SOURCE = 'e424b387fcb51b00c1d19e87e2d369ae8a212792'
TREE = 'b2ab099b499f7d5c8e91de92312f270c475c0a8f'
BUNDLES = {
    '422a4cebdd04c3d27d80cb7a23c144b3ade681017caafe7023ba74c334a9d5b0',
    'c5a3078a691750a37569dbce3b04d895895f432da4c7585c33e108ab51305889',
}
RUNS = {'36991068435': 'A3 combined', '36996168725': 'C4 split'}
TOTALS = {'36991068435': 3893006979, '36996168725': 2367750329}
COMPILERS = {'clang': 'Clang', 'gcc': 'GCC', 'zig': 'Zig cc', 'cl': 'MSVC'}
DIMENSIONS = ['configuration', 'sanitizer', 'fuzz_support', 'compiler_family',
              'compiler', 'os', 'architecture', 'unity']
JOINT = ['os', 'architecture', 'compiler', 'configuration', 'sanitizer',
         'fuzz_support', 'unity']


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def configuration_bucket(configurations: str) -> str:
    values = configurations.split(';')
    require(len(values) == len(set(values)), 'duplicate configuration')
    require(set(values) <= {'Debug', 'Release'} and bool(values), 'unknown configuration')
    result = values[0] if len(values) == 1 else 'Debug+Release (shared)'
    return result


def cache_values(raw: bytes) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in raw.decode('utf-8').splitlines():
        if line and not line.startswith(('#', '//')) and ':' in line and '=' in line:
            key = line.split(':', 1)[0]
            require(key not in result, 'duplicate CMake cache key: ' + key)
            result[key] = line.split('=', 1)[1]
    return result


def collect(bundle: Path) -> tuple[list[dict], list[dict]]:
    outer = bundle.read_bytes()
    require(digest(outer) in BUNDLES, 'unrecognized evidence ZIP SHA-256')
    records: list[dict] = []
    sources: list[dict] = []
    seen_trees: set[tuple[str, str]] = set()
    seen_rows: set[tuple[str, str]] = set()
    with zipfile.ZipFile(io.BytesIO(outer)) as package:
        inventory = json.loads(package.read('derived/verified_artifacts.json'))
        require(len(inventory) == 27, 'expected 27 source artifacts')
        for artifact in sorted(inventory, key=lambda a: a['filename']):
            raw = package.read('raw/' + artifact['filename'])
            require(digest(raw) == artifact['published_sha256'], 'artifact hash mismatch')
            require(len(raw) == artifact['bytes'], 'artifact length mismatch')
            sources.append({'filename': artifact['filename'], 'artifact_id': artifact['artifact_id'],
                            'sha256': digest(raw), 'bytes': len(raw)})
            if not artifact['filename'].startswith('desktop-'):
                continue
            with zipfile.ZipFile(io.BytesIO(raw)) as archive:
                plan = json.loads(archive.read('matrix-phases/plan.json'))
                summary = json.loads(archive.read('matrix-phases/summary.json'))
                coverage = json.loads(archive.read('coverage.json'))
                identity = summary['identity']
                run = identity['run_id']
                require(summary['complete'] is True and not summary['errors'], 'incomplete phase summary')
                require(run in RUNS and identity['run_attempt'] == '1', 'unexpected run/attempt')
                require(identity['source_revision'] == SOURCE and identity['source_tree'] == TREE, 'source mismatch')
                require(identity == plan['identity'], 'plan/summary identity mismatch')
                for key in ('lane_id', 'source_revision', 'run_id', 'run_attempt', 'platform', 'architecture', 'shard'):
                    require(identity[key] == coverage['identity'][key], 'coverage identity mismatch: ' + key)
                planned = {t['id']: t for t in plan['trees']}
                require(len(planned) == len(plan['trees']) == len(summary['trees']), 'duplicate/missing tree')
                executed = []
                for entry in coverage['executed']:
                    require(entry['status'] == 'success', 'unsuccessful coverage entry')
                    executed.extend(entry['rows'])
                require(len(executed) == len(set(executed)), 'duplicate executed row')
                selected = []
                for t in summary['trees']:
                    key = (identity['lane_id'], t['id'])
                    require(key not in seen_trees, 'duplicate classified tree')
                    seen_trees.add(key)
                    require(all(t[k] == v for k, v in planned[t['id']].items()), 'tree metadata mismatch')
                    require(t['sanitize'] in (0, 1) and t['fuzz'] in (0, 1), 'unknown policy flag')
                    configs = t['configurations'].split(';')
                    bucket = configuration_bucket(t['configurations'])
                    cache_path = 'configure/' + t['build_directory'].split('/')[-1] + '/CMakeCache.txt'
                    cache = cache_values(archive.read(cache_path))
                    require(cache['CMAKE_CONFIGURATION_TYPES'] == t['configurations'], 'cache configuration mismatch')
                    for cache_key, value in [('BUSTER_SANITIZE', t['sanitize']), ('BUSTER_FUZZ_AVAILABLE', t['fuzz'])]:
                        require(cache[cache_key] == ('ON' if value else 'OFF'), 'cache policy mismatch: ' + cache_key)
                    row_configs = set()
                    unity_values = set()
                    execution_values = set()
                    for row_id in t['rows']:
                        require((run, row_id) not in seen_rows, 'row repeated across sibling jobs')
                        seen_rows.add((run, row_id))
                        selected.append(row_id)
                        parts = row_id.split('/')
                        require(parts[2:4] == [identity['platform'], identity['architecture']], 'row platform mismatch')
                        fields = dict(p.split('=', 1) for p in parts if '=' in p)
                        require(fields['compiler'] == t['compiler'], 'row compiler mismatch')
                        require(fields['sanitize'] == ('on' if t['sanitize'] else 'off'), 'row sanitizer mismatch')
                        require(fields['fuzz'] == ('on' if t['fuzz'] else 'off'), 'row fuzz mismatch')
                        row_configs.add(fields['configuration'])
                        unity_values.add(fields['unity'])
                        execution_values.add(fields['execution'])
                    require(row_configs == set(configs), 'row configuration mismatch')
                    require(len(unity_values) == len(execution_values) == 1, 'mixed unity/execution policy')
                    require(type(t['elapsed_us']['build']) is int and t['elapsed_us']['build'] >= 0, 'invalid build interval')
                    family = COMPILERS[t['compiler']]
                    variant = 'Apple Clang' if t['compiler_version'].startswith('Apple clang') else family
                    records.append({
                        'sample': RUNS[run], 'run_id': run, 'attempt': 1,
                        'os': identity['platform'], 'architecture': identity['architecture'],
                        'compiler_family': family, 'compiler': variant,
                        'configuration': bucket, 'sanitizer': 'on' if t['sanitize'] else 'off',
                        'fuzz_support': 'on' if t['fuzz'] else 'off', 'unity': next(iter(unity_values)),
                        'build_elapsed_us': t['elapsed_us']['build'],
                        'build_seconds': str(Decimal(t['elapsed_us']['build']) / Decimal(1000000)),
                        'shard': identity['shard'], 'tree_id': t['id'], 'artifact_id': artifact['artifact_id'],
                        'artifact_filename': artifact['filename'], 'artifact_sha256': artifact['published_sha256'],
                        'compiler_version': t['compiler_version'].splitlines()[0],
                        'compiler_target': t['target'], 'compiler_sha256': t['compiler_sha256'],
                        'source_revision': SOURCE, 'source_tree': TREE,
                        'execution': next(iter(execution_values)),
                        'configuration_attribution': 'joint-not-splittable' if len(configs) > 1 else 'single-configuration',
                        'cache_path': cache_path, 'cache_sha256': digest(archive.read(cache_path)),
                        'row_ids': t['rows'], 'runner_image': summary['runner'].get('ImageVersion', 'unknown'),
                    })
                require(set(selected) == set(executed), 'classified/executed row set mismatch')
    require(len(records) == 44, 'expected 44 configured trees')
    for run, expected in TOTALS.items():
        selected_records = [r for r in records if r['run_id'] == run]
        require(len(selected_records) == 22, 'expected 22 trees per run')
        require(sum(r['build_elapsed_us'] for r in selected_records) == expected, 'build total changed')
    require(len(seen_rows) == 46, 'expected 46 logical configuration rows')
    return records, sources


def aggregate(records: list[dict], dimensions: list[str]) -> list[dict]:
    totals = Counter()
    counts = Counter()
    for row in records:
        key = tuple(row[k] for k in ['run_id'] + dimensions)
        totals[key] += row['build_elapsed_us']
        counts[key] += 1
    output = []
    for key, value in sorted(totals.items()):
        row = dict(zip(['run_id'] + dimensions, key))
        row.update(sample=RUNS[key[0]], tree_count=counts[key], build_elapsed_us=value,
                   build_seconds=str(Decimal(value) / Decimal(1000000)),
                   build_minutes=f'{value / 60000000:.9f}',
                   share_of_run_build_percent=f'{100 * value / TOTALS[key[0]]:.9f}')
        output.append(row)
    for run, expected in TOTALS.items():
        require(sum(r['build_elapsed_us'] for r in output if r['run_id'] == run) == expected,
                'aggregate is not an exhaustive partition')
    return output


def write_csv(path: Path, rows: list[dict]) -> None:
    require(bool(rows), 'cannot write empty table')
    with path.open('w', encoding='utf-8', newline='') as out:
        writer = csv.DictWriter(out, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows({k: json.dumps(v, separators=(',', ':')) if isinstance(v, list) else v
                         for k, v in row.items()} for row in rows)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, default=Path(__file__).resolve().parent.parent / 'evidence.zip')
    parser.add_argument('--out', type=Path, default=Path(__file__).resolve().parent / 'derived')
    args = parser.parse_args()
    records, sources = collect(args.bundle)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'build_records.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
    write_csv(args.out / 'build_records.csv', records)
    compact = ['sample', 'run_id', 'os', 'architecture', 'compiler', 'configuration',
               'sanitizer', 'fuzz_support', 'unity', 'build_elapsed_us', 'build_seconds',
               'shard', 'tree_id', 'artifact_id']
    write_csv(args.out / 'builds.csv', [{k: r[k] for k in compact} for r in records])
    marginals = []
    for dimension in DIMENSIONS:
        table = aggregate(records, [dimension])
        for row in table:
            value = row.pop(dimension)
            marginals.append({'dimension': dimension, 'value': value, **row})
    write_csv(args.out / 'build_by_dimension.csv', marginals)
    write_csv(args.out / 'build_by_all_dimensions.csv', aggregate(records, JOINT))
    write_csv(args.out / 'build_by_configuration_sanitizer_fuzz.csv',
              aggregate(records, ['configuration', 'sanitizer', 'fuzz_support']))
    write_csv(args.out / 'build_by_os_architecture_compiler.csv',
              aggregate(records, ['os', 'architecture', 'compiler']))
    validation = {
        'schema': 'buster-ci-build-classification-v1', 'source_revision': SOURCE, 'source_tree': TREE,
        'verified_raw_archives': len(sources), 'classified_desktop_archives': 26,
        'classified_trees': len(records), 'logical_configuration_rows': 46,
        'build_total_us': TOTALS, 'all_marginals_reconcile': True,
        'all_joint_tables_reconcile': True, 'plans_summaries_coverage_caches_agree': True,
        'full_phase_journal_replay': 'not performed by this classifier',
        'cpu_time': 'not measured', 'performance_acceptance': 'not claimed',
        'raw_artifacts': sources,
    }
    (args.out / 'validation.json').write_text(json.dumps(validation, indent=2) + '\n', encoding='utf-8')
    print('Verified 27 source ZIPs; classified 44 trees / 46 configuration rows; all partitions reconcile.')
    print(json.dumps(TOTALS, sort_keys=True))


if __name__ == '__main__':
    main()
