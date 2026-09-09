from pathlib import Path
import argparse, csv, hashlib, json, os, statistics, subprocess, time

parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, default=Path.cwd())
parser.add_argument('--work', type=Path, required=True)
parser.add_argument('--cpu', type=int, default=2)
options = parser.parse_args()
repo = options.repo.resolve()
work = options.work.resolve()
out = work / '36-query-measurement'
out.mkdir(exist_ok=True)
binaries = {v: work / f'36-query-{v}-ide' for v in ('before', 'after')}
cases = {
    'compiler-fast': ['cc', '-Isrc', '-Ibuild/generated', '-DBUSTER_UNITY_BUILD=1', '-DBUSTER_INCLUDE_TESTS=0', '-march=native', '-g0', '-fregister-allocator=fast', '-v', '-c', 'src/buster/apps/ide/ide.c'],
    'operations-fast': ['cc', '-march=native', '-g0', '-fregister-allocator=fast', '-v', '-c', 'tests/basic_c_operations.c'],
}
rows = []
commands = []
for case, args in cases.items():
    for sample in range(-2, 9):
        for variant in (('before', 'after') if sample % 2 == 0 else ('after', 'before')):
            output = out / f'{case}.o'
            command = ['perf', 'stat', '-x,', '-e', 'instructions:u', '-o', str(out / 'perf.csv'), '--', 'taskset', '-c', str(options.cpu), str(binaries[variant]), *args, '-o', str(output)]
            start = time.perf_counter_ns()
            with (out / 'current.log').open('w') as log:
                run = subprocess.Popen(command, cwd=repo, stdout=log, stderr=subprocess.STDOUT)
                _, status, usage = os.wait4(run.pid, 0)
                run.returncode = os.waitstatus_to_exitcode(status)
            cpu_seconds = usage.ru_utime + usage.ru_stime
            wall_ns = time.perf_counter_ns() - start
            output_text = (out / 'current.log').read_text()
            assert run.returncode == 0, (case, variant, output_text)
            if sample == -2:
                (out / f'{case}-{variant}.log').write_text(output_text)
                commands.append({'case': case, 'variant': variant, 'command': command})
            fields = next(x.split(',') for x in (out / 'perf.csv').read_text().splitlines() if ',instructions:u,' in x)
            assert fields[0].isdigit(), fields
            if sample >= 0:
                rows.append({'case': case, 'variant': variant, 'sample': sample, 'wall_ns': wall_ns, 'cpu_seconds': cpu_seconds, 'peak_rss_kib': usage.ru_maxrss, 'object_bytes': output.stat().st_size, 'instructions': int(fields[0]), 'counter_running_percent': float(fields[4])})
        print(case, sample, flush=True)
with (out / 'samples.csv').open('w') as f:
    writer = csv.DictWriter(f, fieldnames=rows[0].keys(), lineterminator='\n')
    writer.writeheader()
    writer.writerows(rows)
summary = {}
for case in cases:
    summary[case] = {}
    for metric in ('instructions', 'wall_ns', 'cpu_seconds', 'peak_rss_kib', 'object_bytes'):
        values = {v: statistics.median(row[metric] for row in rows if row['case'] == case and row['variant'] == v) for v in binaries}
        summary[case][metric] = {**values, 'percent': 100 * (values['after'] / values['before'] - 1)}
(out / 'summary.json').write_text(json.dumps(summary, indent=2))
(out / 'commands.json').write_text(json.dumps(commands, indent=2))
(out / 'binaries.json').write_text(json.dumps({v: {'path': str(p), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for v, p in binaries.items()}, indent=2))
print(json.dumps(summary, indent=2), flush=True)
