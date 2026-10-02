import hashlib
import json
import pathlib
import subprocess

root = pathlib.Path('switch-evidence')
rows = []
types = {8: ('unsigned char', 'signed char'), 16: ('unsigned short', 'short'),
         32: ('unsigned int', 'int'), 64: ('unsigned long long', 'long long')}
for width in (8, 16, 32, 64):
    for signed in (0, 1):
        for variant in range(5):
            low_mask = (1 << width) - 1
            input_value = (-1 if signed else low_mask) if variant in (2, 3) else 7
            # Explicit bit-vector oracle: equality modulo the selector width.
            key = (1 << width) + 7 if variant == 1 and width < 64 else low_mask if variant == 2 else (1 << 64) - 1 if variant == 3 else 8 if variant == 4 else 7
            expected = 11 if (input_value & low_mask) == (key & low_mask) else 22
            ctype = types[width][signed]
            source = '#include <stdio.h>\nextern int choose(' + ctype + ');\nint main(void) { printf("%d\\n", choose((' + ctype + ')(' + str(input_value) + ('ULL' if input_value >= 0 else 'LL') + '))); return 0; }\n'
            observer = root / f'observer-w{width}-s{signed}-v{variant}.c'
            observer.write_text(source)
            for consumer in range(5):
                stem = f'w{width}-s{signed}-v{variant}-c{consumer}'
                artifact = root / (stem + ('.bc' if consumer == 4 else '.o'))
                row = dict(width=width, signed=signed, variant=variant, consumer=consumer, input=input_value, key=key, expected=expected)
                if not artifact.is_file():
                    row['status'] = 'missing-artifact'
                else:
                    row['sha256'] = hashlib.sha256(artifact.read_bytes()).hexdigest()
                    exe = root / (stem + '.exe')
                    command = ['clang', '-O0', '-no-pie', str(observer), str(artifact), '-o', str(exe)]
                    row['command'] = command
                    built = subprocess.run(command, capture_output=True, text=True, timeout=30)
                    (root / (stem + '.link.stdout')).write_text(built.stdout)
                    (root / (stem + '.link.stderr')).write_text(built.stderr)
                    row['link_exit'] = built.returncode
                    if built.returncode:
                        row['status'] = 'link-failed'
                    else:
                        execution = subprocess.run([str(exe.resolve())], capture_output=True, text=True, timeout=5)
                        row.update(exit=execution.returncode, stdout=execution.stdout, stderr=execution.stderr)
                        row['observed'] = int(execution.stdout.strip()) if execution.returncode == 0 and execution.stdout.strip() in ('11', '22') and not execution.stderr else None
                        row['status'] = 'match' if row['observed'] == expected else 'mismatch'
                rows.append(row)
                print(json.dumps(row, sort_keys=True), flush=True)
(root / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')
assert len(rows) == 200
assert all(row['status'] in ('match', 'mismatch') for row in rows), 'Incomplete consumer execution; retain evidence.'
assert all(row['status'] == 'match' for row in rows if row['variant'] in (0, 4)), 'In-range positive/default controls must agree.'
assert all(row['status'] == 'match' for row in rows if row['consumer'] == 4), 'LLVM output must match the independent bit-vector oracle.'
# A research collection pass is not a production correctness pass.
print('COLLECTION_COMPLETE', json.dumps({status: sum(row['status'] == status for row in rows) for status in ('match', 'mismatch')}))
