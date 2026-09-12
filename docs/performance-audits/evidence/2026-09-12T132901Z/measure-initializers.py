import csv
import pathlib
import subprocess
import statistics

root = pathlib.Path(__file__).resolve().parent
cases = [(8, 0), (1024, 0), (4096, 0), (8192, 0), (16384, 0), (32768, 0), (65536, 0),
         (8, 1), (1024, 1), (65536, 1), (65536, 2)]
rows = []
with (root / 'initializer-timing.csv').open('w') as output:
    writer = csv.writer(output)
    writer.writerow(['round', 'variant', 'count', 'shape', 'repeat', 'link_ns', 'cpu_ns', 'output_arena_bytes', 'sort_scratch_bytes', 'checksum', 'failed'])
    for round_index in range(2):
        for count, shape in cases:
            variants = ('base', 'patch') if round_index == 0 else ('patch', 'base')
            for variant in variants:
                process = subprocess.run([str(root / ('initializer-bench-' + variant)), str(count), str(shape)],
                                         check=True, text=True, capture_output=True, timeout=180)
                for line in process.stdout.splitlines():
                    values = [int(v) for v in line.split(',')]
                    assert len(values) == 9 and values[-1] == 0, line
                    row = [round_index, variant, *values]
                    writer.writerow(row)
                    rows.append(row)
                output.flush()
            print('completed', round_index, count, shape, flush=True)

for count, shape in cases:
    samples = {}
    for variant in ('base', 'patch'):
        samples[variant] = [r[5] for r in rows if r[1] == variant and r[2:4] == [count, shape] and r[4] != 0]
    base = statistics.median(samples['base'])
    patch = statistics.median(samples['patch'])
    print(count, shape, 'base_us', base / 1000, 'patch_us', patch / 1000, 'speedup', base / patch)
