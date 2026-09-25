#!/usr/bin/env python3
"""Second experimental revision: keep first attempt, correct arena startup size.

The unchanged ordinary grow granularity still applies after the explicit
header-sized initial request. OS page rounding is owned by arena_create.
"""
from pathlib import Path
import difflib
import hashlib
import shutil
import subprocess
import sys

work = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
p = work / 'src/buster/lib/compiler/frontend/c/c_source.c'
original = p.read_text()
subprocess.run([sys.executable, str(Path(__file__).with_name('root_output_prototype.py')), str(work), str(out)], check=True)
for name in ('root-output.patch', 'c_source.flat.c', 'candidate-blob.txt'):
    shutil.copyfile(out / name, out / ('v1-' + name))
s = p.read_text()
old = '                    .reserved_size = arena->reserved_size,\n                    .flags = {.no_pool = 1},'
new = '                    .reserved_size = arena->reserved_size,\n                    .initial_size = arena_minimum_position,\n                    .flags = {.no_pool = 1},'
assert s.count(old) == 1
s = s.replace(old, new)
blob = hashlib.sha1(b'blob ' + str(len(s.encode())).encode() + b'\0' + s.encode()).hexdigest()
assert blob == 'dee67e1ab7fd9fcbc122f40433a770219cd8a257', blob
p.write_text(s)
(out / 'c_source.flat.c').write_text(s)
(out / 'candidate-blob.txt').write_text(blob + '\n')
patch = ''.join(difflib.unified_diff(original.splitlines(True), s.splitlines(True), fromfile='a/src/buster/lib/compiler/frontend/c/c_source.c', tofile='b/src/buster/lib/compiler/frontend/c/c_source.c'))
(out / 'root-output.patch').write_text(patch)
print('CLEAN_PROTOTYPE_BLOB=' + blob)
