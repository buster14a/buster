import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--buster-lexer', type=Path)
parser.add_argument('--all-library', action='store_true')
parser.add_argument('--clang', default='clang')
args = parser.parse_args()
repo = args.repo.resolve()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)

def extract_function(text, name):
    match = re.search(r'^BUSTER_[^\n]*\b' + re.escape(name) + r'\([^\n]*\)\n\{', text, re.M)
    assert match, name
    end = text.index('\n}', match.end()) + 2
    return text[match.start():end]

meta = (repo / 'src/buster/lib/compiler/assembly/x86_64_metadata.c').read_text()
gen = (repo / 'src/buster/lib/compiler/frontend/c/c_gen.c').read_text()
generated = (repo / 'src/buster/lib/compiler/assembly/generated/x86_64-assembly.generated.h').read_bytes()
metadata_kernels = meta[meta.index('BUSTER_GLOBAL_LOCAL u8 buster_x86_metadata_base64_values'):meta.index('// Decodes a whole blob into')]
metadata_kernels = metadata_kernels.replace('#if BUSTER_METADATA_AVX512_VBMI', '#if 1')
functions = ['c_ir_hex_digit', 'c_ir_append_utf8', 'c_ir_decode_escape', 'c_ir_decode_quoted', 'c_ir_count_quoted', 'c_ir_decode_quoted_reference']
quoted_kernels = '\n\n'.join(extract_function(gen, name) for name in functions)
quoted_kernels = quoted_kernels.replace('BUSTER_C_INTERNAL ', 'static ')
for name in ('c_ir_decode_quoted', 'c_ir_decode_quoted_reference'):
    quoted_kernels = quoted_kernels.replace('static bool ' + name + '(', 'static __attribute__((noinline)) bool ' + name + '(')
# Keep direct calls visible; prevent benchmark loop folding without volatile input.
metadata_kernels = metadata_kernels.replace('BUSTER_GLOBAL_LOCAL void buster_x86_metadata_decode_base64_chunk_avx512', 'static __attribute__((noinline)) void buster_x86_metadata_decode_base64_chunk_avx512')
metadata_kernels = metadata_kernels.replace('BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL void buster_x86_metadata_decode_base64_chunk_scalar', 'static __attribute__((noinline)) void buster_x86_metadata_decode_base64_chunk_scalar')
value_kernel = extract_function(generated.decode(), 'buster_x86_generated_base64_value')
intern_kernel = extract_function((repo / 'src/buster/lib/compiler/frontend/c/c_source.c').read_text(), 'c_symbols_intern_tokens').replace('BUSTER_C_INTERNAL ', 'static __attribute__((noinline)) ')
(out / 'kernels.inc').write_text(value_kernel + '\n' + metadata_kernels + '\n' + quoted_kernels + '\n' + intern_kernel + '\n')

blobs = []
for match in re.finditer(rb'static const char8 buster_x86_generated_(\w+)_blob_chunk_(\d+)\[\] = "([A-Za-z0-9+/=]+)"\s*;', generated):
    data = match.group(3)
    assert len(data) % 4 == 0
    blobs.append(data)
assert len(blobs) > 500

# Clang's raw lexer supplies identifier classes before keyword conversion,
# matching the stage on which Buster interns tokens. These are physical source
# runs, not a reconstituted stage-1 preprocessing trace.
files = [
    'src/buster/lib/compiler/assembly/generated/x86_64-assembly.generated.h',
    'src/buster/lib/compiler/frontend/c/c_gen.c',
    'src/buster/lib/compiler/frontend/c/c_source.c',
    'src/buster/lib/compiler/frontend/c/c_parse.c',
    'src/buster/lib/compiler/codegen/machine_x86_64.c',
    'src/buster/lib/compiler/codegen/machine_register_allocator.c',
]
files = [name for name in files if (repo / name).exists()]
if args.all_library:
    files = [str(path.relative_to(repo)) for path in sorted((repo / 'src/buster/lib').rglob('*')) if path.suffix in ('.c', '.h')]
literals = []
runs = []
manifest = []
for name in files:
    path = repo / name
    source = path.read_bytes()
    line_offsets = [0] + [m.end() for m in re.finditer(b'\n', source)]
    command = [args.clang, '-x', 'c', '-Xclang', '-dump-raw-tokens', '-fsyntax-only', str(path)]
    records = []
    if not args.buster_lexer:
        process = subprocess.run(command, capture_output=True, check=True)
        records = process.stderr.split(b'>\n')
    shapes = bytearray()
    count_before = len(literals)
    if args.buster_lexer:
        shape_path = out / 'last-shapes.bin'
        literal_path = out / 'last-literals.bin'
        subprocess.run([str(args.buster_lexer), str(path), str(shape_path), str(literal_path)], capture_output=True, check=True)
        shape_data = shape_path.read_bytes()
        shape_count, = struct.unpack_from('<I', shape_data)
        shapes = bytearray(shape_data[4:])
        assert len(shapes) == shape_count
        literal_data = literal_path.read_bytes()
        literal_count, = struct.unpack_from('<I', literal_data)
        cursor = 4
        for index in range(literal_count):
            length, = struct.unpack_from('<I', literal_data, cursor)
            cursor += 4
            literals.append(literal_data[cursor:cursor+length])
            cursor += length
        assert cursor == len(literal_data)
        records = []
    for record in records:
        found = re.match(rb"(\w+) '", record)
        location = re.search(rb'Loc=<.*:(\d+):(\d+)$', record)
        if found and location:
            kind = found.group(1)
            if kind not in (b'unknown', b'comment'):
                shapes.append(2 if kind == b'raw_identifier' else 128)
            if kind in (b'string_literal', b'utf8_string_literal'):
                line, col = (int(value) for value in location.groups())
                offset = line_offsets[line - 1] + col - 1
                opening = offset + (2 if source[offset:offset+2] == b'u8' else 0)
                assert source[opening] == 34, (name, line, col)
                cursor = opening + 1
                while cursor < len(source) and source[cursor] != 34:
                    cursor += 2 if source[cursor] == 92 else 1
                assert cursor < len(source)
                # The production decoder consumes translated spellings.
                spelling = source[offset:cursor+1].replace(b'\\\r\n', b'').replace(b'\\\n', b'')
                literals.append(spelling)
    runs.append(bytes(shapes))
    manifest.append(dict(file=name, bytes=len(source), tokens=len(shapes), identifiers=shapes.count(2), literals=len(literals)-count_before, sha256=hashlib.sha256(source).hexdigest()))
    print(manifest[-1], flush=True)

with (out / 'corpus.bin').open('wb') as corpus:
    for records in (blobs, literals, runs):
        corpus.write(struct.pack('<I', len(records)))
        for data in records:
            corpus.write(struct.pack('<I', len(data)))
            corpus.write(data)

manifest_all = dict(source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(), clang=subprocess.check_output([args.clang, '--version'], text=True).splitlines()[0], lexer='Buster c_lex' if args.buster_lexer else 'Clang raw lexer', files=manifest, blob_chunks=len(blobs), blob_encoded_bytes=sum(map(len, blobs)), literals=len(literals), literal_spelling_bytes=sum(map(len,literals)), runs=len(runs), tokens=sum(map(len,runs)), max_literal_length=max(map(len,literals)))
(out / 'manifest.json').write_text(json.dumps(manifest_all, indent=2) + '\n')
print(json.dumps(manifest_all, indent=2), flush=True)
