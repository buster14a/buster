"""Count logical output-byte writes in a temporary copy of snapshot.c."""
from pathlib import Path
import re
import sys

path = Path(sys.argv[1])
text = path.read_text()
start = text.index('static uint64_t snapshot_add_string(')
stop = text.index('// Makes the directory', start)
body = text[start:stop]

def once(s, old, new):
    assert s.count(old) == 1, old
    return s.replace(old, new)

helpers = r"""
// Measurement-only. Every mark names bytes in the output (not traffic in caches or the kernel).
static uint8_t* witness_image;
static uint8_t* witness_hits;
static uint64_t witness_length;
static uint64_t witness_memsets;
static uint64_t witness_totals[3];

static void witness_mark(void* dest, uint64_t size, uint32_t kind)
{
    const uint64_t offset = (uintptr_t)dest - (uintptr_t)witness_image;
    if (offset > witness_length || size > witness_length - offset)
    {
        abort();
    }
    witness_totals[kind] += size;
    for (uint64_t i = 0; i < size; i += 1)
    {
        uint8_t* hit = witness_hits + offset + i;
        if ((*hit & 15) == 15) abort();
        *hit = (*hit + 1) | (16u << kind);
    }
}

static void witness_clear(void* dest, int value, size_t bytes)
{
    witness_memsets += bytes;
    witness_mark(dest, bytes, 0);
    memset(dest, value, bytes);
}

static void witness_copy(void* dest, const void* source, size_t bytes)
{
    witness_mark(dest, bytes, 1);
    memcpy(dest, source, bytes);
}

static void witness_begin(uint8_t* image, uint64_t bytes)
{
    witness_image = image;
    witness_length = bytes;
    witness_memsets = 0;
    witness_hits = calloc(bytes, 1);
    if (witness_hits == 0) abort();
    memset(witness_totals, 0, sizeof(witness_totals));
}

static void witness_end(const Runtime* runtime)
{
    uint64_t exactly[16] = {0};
    uint64_t clear_copy = 0;
    for (uint64_t i = 0; i < witness_length; ++i)
    {
        uint8_t hit = witness_hits[i];
        exactly[hit & 15] += 1;
        clear_copy += (hit & 48) == 48;
    }
    if (exactly[0] != 0) abort();
    const SnapshotHeader* header = (const SnapshotHeader*)witness_image;
    const SnapshotPlace piece = header->regions[SNAPSHOT_REGION_PIECE_HASHES];
    fprintf(stderr, "HASH kind=%s pieces_offset=%llu pieces_bytes=%llu directory=", snapshot_name(runtime->kind),
            (unsigned long long)piece.offset, (unsigned long long)piece.bytes);
    for (uint32_t i = 0; i < sizeof(header->hash); ++i) fprintf(stderr, "%02x", header->hash[i]);
    fprintf(stderr, "\n");
    fprintf(stderr, "TRACE kind=%s bytes=%llu memset=%llu clear=%llu copy=%llu fields=%llu once=%llu twice=%llu three_plus=%llu clear_and_copy=%llu repeat_writes=%llu\n",
            snapshot_name(runtime->kind), (unsigned long long)witness_length, (unsigned long long)witness_memsets, (unsigned long long)witness_totals[0],
            (unsigned long long)witness_totals[1], (unsigned long long)witness_totals[2], (unsigned long long)exactly[1],
            (unsigned long long)exactly[2], (unsigned long long)(witness_length - exactly[1] - exactly[2]), (unsigned long long)clear_copy,
            (unsigned long long)(witness_totals[0] + witness_totals[1] + witness_totals[2] - witness_length));
}
"""
body = body.replace('memcpy(', 'witness_copy(').replace('memset(', 'witness_clear(')
needle = 'bool fits = image != 0 && snapshot_pieces_contiguous(runtime);'
body = once(body, needle, needle + '\n    if (image != 0) witness_begin(image, end);')
needle = '*file_bytes = fits ? end : 0;'
body = once(body, needle, 'if (fits) witness_end(runtime);\n    if (image != 0) free(witness_hits);\n    ' + needle)
# These are the writer's individual destination stores, including NULs and offset sentinels.
field = re.compile(r'^(\s*)((?:(?:probes\[p\]|versions\[v\]|members\[m\])\.[\w.]+|strings->bytes\[strings->length \+ length\]|(?:object_name_column|member_name_column|object_atom_column|member_atom_column|piece_data)\[[^\n]+?\])) = (.+?);$', re.M)
fields = []
def mark(match):
    indent, lhs, value = match.groups()
    fields.append(lhs)
    kind = 0 if value in ('0', '(String8){ 0 }') else 2
    return f'{indent}witness_mark(&{lhs}, sizeof({lhs}), {kind});\n{indent}{lhs} = {value};'
body = field.sub(mark, body)
assert len(fields) >= 27, fields
needle = 'runtime_piece_hash(&view, p, hashes + 16 * (uint64_t)p);'
body = once(body, needle, 'witness_mark(hashes + 16 * (uint64_t)p, 16, 2);\n            ' + needle)
# Two different binaries deliberately have different file identities. Supply both instrumented
# writers with the same actual stat result for the complete on-disk format comparison.
text = once(text[:start], 'stat("/proc/self/exe", &status)', 'stat("/usr/bin/env", &status)') + helpers + body + text[stop:]
path.write_text(text)
print('instrumented scalar destination writes:', ', '.join(fields))
