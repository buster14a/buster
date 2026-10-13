# Writes a relocation-table replay file for the TLS companion workloads (#1915).
# Usage: python3 -I extract.py OBJECT OUT
# Format (little-endian): "BTLSREL1", u32 rows, u32 helper symbol index,
# then rows of {u64 offset, u32 section, u32 symbol, u32 kind, u32 zero}.
# Kinds: 1 PC32, 2 PLT32, 3 GOT-family call, 4 TLSGD, 5 TLSLD, 0 other.
# Every SHT_RELA section is emitted in section-header order, its target section
# index as the section and each row in file order, as the linker loads them.
import struct, sys
data = open(sys.argv[1], "rb").read()
if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
    sys.exit("expected little-endian ELF64")
shoff, = struct.unpack_from("<Q", data, 0x28)
shentsize, shnum = struct.unpack_from("<HH", data, 0x3A)
sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize) for i in range(shnum)]
symtab = next(s for s in sections if s[1] == 2)
strtab = sections[symtab[6]]
helper = None
for i in range(symtab[5] // 24):
    name_offset, = struct.unpack_from("<I", data, symtab[4] + i * 24)
    end = data.index(b"\0", strtab[4] + name_offset)
    if data[strtab[4] + name_offset:end] == b"__tls_get_addr":
        helper = i
if helper is None:
    sys.exit("no __tls_get_addr symbol")
kinds = {2: 1, 4: 2, 9: 3, 41: 3, 42: 3, 19: 4, 20: 5}
rows = []
for section in sections:
    if section[1] == 4:
        for i in range(section[5] // 24):
            offset, info, _ = struct.unpack_from("<QQq", data, section[4] + i * 24)
            rows.append(struct.pack("<QIIII", offset, section[7], info >> 32, kinds.get(info & 0xFFFFFFFF, 0), 0))
with open(sys.argv[2], "wb") as out:
    out.write(b"BTLSREL1" + struct.pack("<II", len(rows), helper) + b"".join(rows))
