"""Diagnostic copies of a Buster exe/pdb for #2717: nonzero timestamp/signature variants."""
import os
import shutil
import struct
import sys


def patch_exe_timestamp(path, value):
    data = bytearray(open(path, "rb").read())
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    struct.pack_into("<I", data, pe + 8, value)
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    dirs = opt + (112 if magic == 0x20B else 96)
    debug_rva, debug_size = struct.unpack_from("<II", data, dirs + 6 * 8)
    sections = opt + struct.unpack_from("<H", data, pe + 20)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    for i in range(count):
        s = sections + 40 * i
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, s + 8)
        if va <= debug_rva < va + max(vsize, rsize):
            off = raw + debug_rva - va
            for e in range(debug_size // 28):
                struct.pack_into("<I", data, off + 28 * e + 4, value)
    open(path, "wb").write(data)


def patch_pdb_signature(path, value):
    data = bytearray(open(path, "rb").read())
    block_size, _, num_blocks, dir_bytes, _, dir_map = struct.unpack_from("<IIIIII", data, 32)
    dir_blocks = (dir_bytes + block_size - 1) // block_size
    dir_block_ids = struct.unpack_from("<%dI" % dir_blocks, data, dir_map * block_size)
    directory = b"".join(data[b * block_size:(b + 1) * block_size] for b in dir_block_ids)[:dir_bytes]
    nstreams = struct.unpack_from("<I", directory, 0)[0]
    sizes = struct.unpack_from("<%dI" % nstreams, directory, 4)
    pos = 4 + 4 * nstreams
    blocks = []
    for size in sizes:
        n = 0 if size == 0xFFFFFFFF else (size + block_size - 1) // block_size
        blocks.append(struct.unpack_from("<%dI" % n, directory, pos))
        pos += 4 * n
    struct.pack_into("<I", data, blocks[1][0] * block_size + 4, value)
    open(path, "wb").write(data)


def main():
    exe, out_dir = sys.argv[1], sys.argv[2]
    base = os.path.splitext(exe)[0]
    name = os.path.basename(base)
    made = []
    for tag, exe_ts, pdb_sig in (("ts", True, False), ("sig", False, True), ("both", True, True)):
        os.makedirs(os.path.join(out_dir, tag), exist_ok=True)
        new = os.path.join(out_dir, tag, name)
        shutil.copyfile(exe, new + ".exe")
        shutil.copyfile(base + ".pdb", new + ".pdb")
        if exe_ts:
            patch_exe_timestamp(new + ".exe", 0x5F000000)
        if pdb_sig:
            patch_pdb_signature(new + ".pdb", 0x5F000000)
        made.append(new + ".exe")
    print("\n".join(made))


if __name__ == "__main__":
    main()
