from pathlib import Path
import struct
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)

def write(name, base64, quoted, shapes):
    with (out / (name + '.bin')).open('wb') as file:
        for records in (base64, quoted, shapes):
            file.write(struct.pack('<I', len(records)))
            for record in records:
                file.write(struct.pack('<I', len(record)))
                file.write(record)

for length in (4, 8, 16, 32, 64, 128):
    write('base64-' + str(length), [b'A' * length] * 8192, [], [])
for length in (0, 4, 16, 32, 64, 128):
    write('quoted-plain-' + str(length), [], [b'"' + b'x' * length + b'"'] * 8192, [])
write('quoted-dense-128', [], [b'"' + b'\\n' * 64 + b'"'] * 8192, [])
for density in (0, 1, 20, 100):
    record = bytes(2 if (index * 73 + 37) % 100 < density else 128 for index in range(16384))
    write('identifier-' + str(density), [], [], [record] * 16)
