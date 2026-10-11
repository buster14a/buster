"""#3411 bisection: rewrite one Buster PDB container stream per variant."""
import os
import struct
import sys

BLOCK = 4096


def read_msf(path):
    data = open(path, "rb").read()
    block_size, _, _, dir_bytes, _, dir_map = struct.unpack_from("<IIIIII", data, 32)
    dir_blocks = (dir_bytes + block_size - 1) // block_size
    ids = struct.unpack_from("<%dI" % dir_blocks, data, dir_map * block_size)
    directory = b"".join(data[b * block_size:(b + 1) * block_size] for b in ids)[:dir_bytes]
    n = struct.unpack_from("<I", directory, 0)[0]
    sizes = struct.unpack_from("<%dI" % n, directory, 4)
    pos = 4 + 4 * n
    streams = []
    for size in sizes:
        count = 0 if size == 0xFFFFFFFF else (size + block_size - 1) // block_size
        blocks = struct.unpack_from("<%dI" % count, directory, pos)
        pos += 4 * count
        streams.append(bytearray(b"".join(data[b * block_size:(b + 1) * block_size] for b in blocks)[:size]))
    return streams


def write_msf(path, streams):
    # Same layout policy as pdb_msf_build: data, directory, block map; FPM blocks 1 and 2.
    nxt = 3
    def alloc(count):
        nonlocal nxt
        out = []
        for _ in range(count):
            if nxt % BLOCK in (1, 2):
                nxt += 3 - nxt % BLOCK
            out.append(nxt)
            nxt += 1
        return out
    stream_blocks = [alloc((len(s) + BLOCK - 1) // BLOCK) for s in streams]
    directory = struct.pack("<I", len(streams)) + b"".join(struct.pack("<I", len(s)) for s in streams)
    directory += b"".join(struct.pack("<%dI" % len(b), *b) for b in stream_blocks)
    dir_blocks = alloc((len(directory) + BLOCK - 1) // BLOCK)
    block_map = alloc(1)[0]
    total = nxt
    out = bytearray(total * BLOCK)
    out[0:32] = b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0"
    struct.pack_into("<IIIIII", out, 32, BLOCK, 2, total, len(directory), 0, block_map)
    for fpm in (1, 2):
        out[fpm * BLOCK:(fpm + 1) * BLOCK] = b"\xff" * BLOCK
        for b in range(total):
            out[fpm * BLOCK + b // 8] &= ~(1 << (b % 8)) & 0xFF
    for s, blocks in zip(streams, stream_blocks):
        for i, b in enumerate(blocks):
            chunk = s[i * BLOCK:(i + 1) * BLOCK]
            out[b * BLOCK:b * BLOCK + len(chunk)] = chunk
    for i, b in enumerate(dir_blocks):
        chunk = directory[i * BLOCK:(i + 1) * BLOCK]
        out[b * BLOCK:b * BLOCK + len(chunk)] = chunk
        struct.pack_into("<I", out, block_map * BLOCK + 4 * i, b)
    open(path, "wb").write(out)


def gsi_empty():
    # LLVM GSIHashStreamBuilder: header, no records, a 129-word zero bitmap, no buckets.
    bitmap = 4 * ((4096 + 32) // 32)
    return struct.pack("<IIII", 0xFFFFFFFF, 0xF12F091A, 0, bitmap) + b"\0" * bitmap


def dbi_set_u16(dbi, offset, value):
    struct.pack_into("<H", dbi, offset, value)


def named_map(entries, capacity):
    """Info-stream named stream map: (name, stream) pairs placed by hashStringV1 % capacity."""
    strings = b""
    offsets = []
    for name, _ in entries:
        offsets.append(len(strings))
        strings += name.encode() + b"\0"
    slots = [None] * capacity
    for (name, stream), offset in zip(entries, offsets):
        bucket = hash_v1(name.encode()) % capacity
        while slots[bucket] is not None:
            bucket = (bucket + 1) % capacity
        slots[bucket] = (offset, stream)
    present = 0
    for i, slot in enumerate(slots):
        if slot is not None:
            present |= 1 << i
    words = (capacity + 31) // 32
    out = struct.pack("<I", len(strings)) + strings + struct.pack("<II", len(entries), capacity)
    out += struct.pack("<I", words) + b"".join(struct.pack("<I", (present >> (32 * w)) & 0xFFFFFFFF) for w in range(words))
    out += struct.pack("<I", 0)
    for slot in slots:
        if slot is not None:
            out += struct.pack("<II", *slot)
    return out


def hash_v1(data):
    result = 0
    for i in range(0, len(data) - len(data) % 4, 4):
        result ^= struct.unpack_from("<I", data, i)[0]
    rem = len(data) % 4
    tail = len(data) - rem
    if rem >= 2:
        result ^= struct.unpack_from("<H", data, tail)[0]
        tail += 2
    if rem % 2:
        result ^= data[tail]
    result |= 0x20202020
    result ^= result >> 11
    result ^= result >> 16
    return result & 0xFFFFFFFF


def variants(streams, rt=None):
    """Yield (tag, streams) pairs; each changes one suspect."""
    def copy():
        return [bytearray(s) for s in streams]
    # DBI header offsets: GlobalStreamIndex 12, PublicStreamIndex 16, SymRecordStreamIndex 20.
    v = copy()
    for off in (12, 16, 20):
        dbi_set_u16(v[3], off, 0xFFFF)
    yield "no-gsi", v
    v = copy()
    v[5] = bytearray(gsi_empty())
    yield "globals-bitmap", v
    v = copy()
    pub = bytearray(v[6][:28])
    struct.pack_into("<I", pub, 0, len(gsi_empty()))
    v[6] = pub + gsi_empty()
    yield "publics-bitmap", v
    v = copy()
    v[5] = bytearray(gsi_empty())
    pub = bytearray(v[6][:28])
    struct.pack_into("<I", pub, 0, len(gsi_empty()))
    v[6] = pub + gsi_empty()
    yield "both-bitmap", v
    # MS link appends an absolute pseudo-section entry to the section map.
    v = copy()
    dbi = v[3]
    mod_size, contrib_size, map_size = struct.unpack_from("<iii", dbi, 24)
    map_start = 64 + mod_size + contrib_size
    count = struct.unpack_from("<H", dbi, map_start)[0]
    extra = struct.pack("<HHHHHHII", 0x208, 0, 0, count + 1, 0xFFFF, 0xFFFF, 0, 0xFFFFFFFF)
    end = map_start + map_size
    v[3] = bytearray(dbi[:map_start] + struct.pack("<HH", count + 1, count + 1) + dbi[map_start + 4:end] + extra + dbi[end:])
    struct.pack_into("<i", v[3], 32, map_size + 20)
    yield "section-map-absolute", v
    if rt:
        # LLVM-normalized type records without and with LLVM's TPI hash stream.
        v = copy()
        v[2] = bytearray(rt[2])
        struct.pack_into("<HH", v[2], 20, 0xFFFF, 0xFFFF)
        yield "tpi-llvm-records", v
        hash_index = struct.unpack_from("<H", rt[2], 20)[0]
        v = copy()
        v[2] = bytearray(rt[2])
        struct.pack_into("<HH", v[2], 20, len(v), 0xFFFF)
        v.append(bytearray(rt[hash_index]))
        yield "tpi-llvm-records-hash", v
        v = copy()
        v[10] = bytearray(rt[8])
        yield "names-llvm", v
        # Info stream with /LinkInfo (an appended empty stream) and /names.
        v = copy()
        head = v[1][:28]
        v.append(bytearray())
        v[1] = bytearray(head + named_map([("/LinkInfo", len(v) - 1), ("/names", 10)], 4) + struct.pack("<I", 0) + struct.pack("<I", 20140508))
        yield "info-linkinfo", v
        # Reverse direction: the loading LLVM container with Buster's TPI stream.
        yield "llvm-identity", [bytearray(x) for x in rt]
        v = [bytearray(x) for x in rt]
        v[2] = bytearray(streams[2])
        yield "llvm-with-buster-tpi", v


def main():
    pdb, out_dir = sys.argv[1], sys.argv[2]
    streams = read_msf(pdb)
    rt = read_msf(sys.argv[3]) if len(sys.argv) > 3 and os.path.exists(sys.argv[3]) else None
    name = os.path.basename(pdb)
    for tag, v in variants(streams, rt):
        d = os.path.join(out_dir, tag)
        os.makedirs(d, exist_ok=True)
        write_msf(os.path.join(d, name), v)
        print(os.path.join(d, name))
    d = os.path.join(out_dir, "identity")
    os.makedirs(d, exist_ok=True)
    write_msf(os.path.join(d, name), streams)
    print(os.path.join(d, name))


if __name__ == "__main__":
    main()
