#!/usr/bin/env python3
"""Run the paired FDE validation probes on the evidence-only compiler."""
import dataclasses
import hashlib
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import sys
import tempfile


def cpu():
    data = Path("/proc/cpuinfo").read_text().split("\n\n")[0].splitlines()
    fields = dict(line.split(":", 1) for line in data if ":" in line)
    fields = {k.strip(): v.strip() for k, v in fields.items()}
    name = fields["model name"]
    flags = set(fields["flags"].split())
    required = set("avx512f avx512bw avx512vl avx512dq avx512cd avx512vbmi avx512_vbmi2 avx512_vpopcntdq avx512_bitalg avx512_vnni gfni vaes aes bmi1 bmi2 popcnt avx2 ssse3 sse4_1 xsave".split())
    if "9700X" in name.upper() or "benchpress" in platform.node().lower() or not required <= flags or not ({"abm", "lzcnt"} & flags):
        raise SystemExit("Refusing unqualified or prohibited host: " + name)
    print("CIE3155 HOST", name, platform.machine(), flush=True)


def elf_section(data, name):
    off = struct.unpack_from("<Q", data, 40)[0]
    width, count, shstr = struct.unpack_from("<HHH", data, 58)
    rows = [struct.unpack_from("<IIQQQQIIQQ", data, off + n * width) for n in range(count)]
    strings = data[rows[shstr][4]:rows[shstr][4] + rows[shstr][5]]
    for row in rows:
        if strings[row[0]:].split(b"\0", 1)[0] == name.encode():
            return row[4:6]
    raise ValueError(name)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def tool(*names):
    for name in names:
        path = shutil.which(name)
        if path:
            return path
    raise RuntimeError(f"Missing tool {names}")


def compile_o(cc, stem, source, flags):
    path = Path(stem + (".cc" if "++" in Path(cc).name else ".c"))
    path.write_text(source)
    subprocess.run([cc, "-O2", "-fno-stack-protector", *flags, "-c", str(path), "-o", stem + ".o"], check=True)
    return stem + ".o"


def start(cc, stem, declarations, expr):
    return compile_o(cc, stem, declarations + "\nvoid _start(void){long code=" + expr +
                     "; __asm__ volatile(\"syscall\" :: \"a\"(60L), \"D\"(code) : \"rcx\",\"r11\",\"memory\");"
                     "__builtin_unreachable();}\n",
                     ["-ffreestanding", "-fno-asynchronous-unwind-tables", "-fno-unwind-tables"])


def fde_positions(data):
    off, size = elf_section(data, ".eh_frame")
    fdes = []
    cur = 0
    while cur < size:
        length, = struct.unpack_from("<I", data, off + cur)
        if length == 0:
            break
        key, = struct.unpack_from("<I", data, off + cur + 4)
        if key:
            fdes.append((cur, cur + 4 - key))
        cur += length + 4
    return off, size, fdes


@dataclasses.dataclass
class Observation:
    rows: list
    map_lines: list
    errors: list
    sites: list
    status: int
    stdout: bytes
    stderr: bytes
    elf: bytes
    eh: bytes
    decoded: bytes
    executed: tuple


def observe(binary, args, mode, fault=None):
    env = os.environ.copy()
    env["CIE3155_MODE"] = mode
    env.pop("CIE3155_FAULT", None)
    if fault:
        env["CIE3155_FAULT"] = fault
    output = Path("out")
    output.unlink(missing_ok=True)
    child = subprocess.run([binary, "link-foreign", "--lanes", "1", "-o", str(output), *args],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    rows, maps, errors, sites, ordinary = [], [], [], [], []
    for line in child.stderr.splitlines(keepends=True):
        if line.startswith(b"CIE3155 F "):
            rows.append(tuple(map(int, line.split()[2:])))
        elif line.startswith(b"CIE3155 M "):
            maps.append(line)
        elif line.startswith(b"CIE3155 ERR "):
            errors.append(tuple(map(int, line.split()[2:])))
        elif line.startswith(b"CIE3155 SITE "):
            sites.append(int(line.split()[-1]))
        else:
            ordinary.append(line)
    data = eh = decoded = b""
    ran = ()
    if child.returncode == 0 and output.is_file():
        data = output.read_bytes()
        off, size = elf_section(data, ".eh_frame")
        eh = data[off:off + size]
        decoded = subprocess.check_output(["readelf", "--debug-dump=frames", str(output)])
        result = subprocess.run(["./out"], capture_output=True)
        ran = result.returncode, result.stdout, result.stderr
    return Observation(rows, maps, errors, sites, child.returncode, child.stdout, b"".join(ordinary),
                       data, eh, decoded, ran)


def compare(binary, name, args, *, disabled=False, bad=False, fault=None, expected=None):
    base = observe(binary, args, "base", fault)
    head = observe(binary, args, "head", fault)
    for key in ("map_lines", "errors", "sites", "status", "stdout", "stderr", "elf", "eh", "decoded", "executed"):
        assert getattr(base, key) == getattr(head, key), (name, key, base.stderr[-512:], head.stderr[-512:])
    assert [row[:4] for row in base.rows] == [row[:4] for row in head.rows], name
    previous = None
    direct = 0
    for b, h in zip(base.rows, head.rows):
        atom, index, cursor, target, search, probes = h
        assert b[4] == 1 and b[5] > 0, (name, b)
        this = atom, target
        assert search == int(previous != this) and probes == (b[5] if search else 0), (name, h, previous)
        direct += int(previous is not None and atom == previous[0])
        previous = this
    bs, bp = (sum(r[i] for r in base.rows) for i in (4, 5))
    hs, hp = (sum(r[i] for r in head.rows) for i in (4, 5))
    if expected:
        assert (bs, bp, hs, hp) == expected, (name, bs, bp, hs, hp, expected)
    if fault:
        assert len(base.sites) == 1 and base.errors == [(3, base.sites[0], 0)], (name, base.sites, base.errors)
        print("CIE3155 PLANNER", name, *base.errors[0], "base=head", flush=True)
    elif bad:
        assert base.status != 0 and base.stderr, (name, base.status)
        print("CIE3155 RAW", name, base.status, base.stderr.decode().strip(), flush=True)
    else:
        assert base.status == 0 and base.executed == (37, b"", b""), (name, base.status, base.stderr, base.executed)
        assert (not head.rows and not head.map_lines) if disabled else (head.rows and head.map_lines), name
        mapbytes = b"".join(head.map_lines)
        print("CIE3155 RESULT", name, len(base.rows), bs, bp, hs, hp, direct, len(head.eh),
              len(head.map_lines), sha(mapbytes), sha(head.eh), sha(head.elf), flush=True)
        if head.map_lines:
            print("CIE3155 PLAN", name, head.map_lines[0].decode().strip(), flush=True)


def library(cxx, name):
    loc = subprocess.check_output([cxx, "-print-file-name=" + name], text=True).strip()
    if not Path(loc).is_file():
        for entry in subprocess.check_output(["ldconfig", "-p"], text=True).splitlines():
            if entry.strip().split(" ")[0] == name and "x86-64" in entry:
                loc = entry.split(" => ")[-1]
                break
    if not Path(loc).is_file():
        raise FileNotFoundError(name)
    return loc


def main(binary):
    cpu()
    clang, gcc, cxx = tool("clang-23", "clang"), tool("gcc"), tool("g++")
    for cc in (clang, gcc, cxx):
        print("CIE3155 TOOL", subprocess.check_output([cc, "--version"], text=True).splitlines()[0], flush=True)
    baseflags = ["-ffreestanding", "-fasynchronous-unwind-tables"]
    s = start(clang, "start", "extern int a0(int),b0(int);", "b0(a0(35))")
    pairs = {}
    for n in (32, 64):
        pair = [compile_o(clang, label + str(n),
                          "".join(f"int {label}{i}(int x){{return x+{i+1};}}\n" for i in range(n)), baseflags)
                for label in ("a", "b")]
        pairs[n] = pair
        for obj in pair:
            record = fde_positions(Path(obj).read_bytes())[2]
            assert len(record) == n and len({v for k, v in record}) == 1
        compare(binary, "shared-" + str(n), ["--dedup-unwind", s, *pair],
                expected=(64, 384, 2, 12) if n == 32 else (128, 896, 2, 14))
    inter = compile_o(gcc, "interleaved", "".join(
        "int alt" + str(i) + "(int x){" + ('__asm__ volatile (".cfi_return_column 15");' if i % 2 else "") +
        "return x+" + str(i + 1) + ";}\n" for i in range(5)), baseflags + ["-fno-toplevel-reorder"])
    targets = [v for k, v in fde_positions(Path(inter).read_bytes())[2]]
    assert len(targets) == 5 and targets[0] == targets[2] == targets[4] != targets[1] == targets[3], targets
    start2 = start(clang, "start2", "".join(f"extern int alt{i}(int);" for i in range(5)),
                   "alt4(alt3(alt2(alt1(alt0(22)))))")
    compare(binary, "alternating", ["--dedup-unwind", start2, inter], expected=(5, 15, 5, 15))
    compare(binary, "mixed", ["--dedup-unwind", s, pairs[32][0], inter, pairs[32][1]], expected=(69, 399, 7, 27))
    compare(binary, "disabled", [s, *pairs[32]], disabled=True)
    raw = Path(pairs[32][0]).read_bytes()
    startoff, size, positions = fde_positions(raw)
    first, second, third = [p for p, _ in positions[:3]]
    for fault in ("underflow", "interior", "fde", "truncated", "double"):
        bad = bytearray(raw)
        if fault == "truncated":
            struct.pack_into("<I", bad, startoff + second, size - second + 12)
        else:
            key = {"underflow": second + 5, "interior": second + 3, "fde": second + 4 - first,
                   "double": second + 5}[fault]
            struct.pack_into("<I", bad, startoff + second + 4, key)
        if fault == "double":
            struct.pack_into("<I", bad, startoff + third, size - third + 12)
        Path("bad.o").write_bytes(bad)
        compare(binary, fault, ["--dedup-unwind", s, "bad.o", pairs[32][1]], bad=True)
    for fault in ("warm", "underflow", "interior", "fde", "self", "sentinel", "short", "truncated", "double", "late"):
        compare(binary, fault, ["--dedup-unwind", s, *pairs[32]], fault=fault)
    a = 'extern "C" int invoked(int);\n'
    declarations = []
    for i in range(5):
        name = "cpp" + str(i)
        declarations.append('extern "C" int ' + name + "(int);")
        body = ("try{return invoked(x);}catch(int v){return v+" + str(i + 1) + ";}") if i % 2 else "return x+" + str(i + 1) + ";"
        a += 'extern "C" int ' + name + "(int x){" + body + "}\n"
    cppflags = ["-fPIC", "-fno-toplevel-reorder", "-fno-reorder-blocks-and-partition"]
    obj_a = compile_o(cxx, "exceptions", a, cppflags)
    obj_b = compile_o(cxx, "throw-main", "".join(declarations) +
                      'extern "C" int invoked(int x){throw x;}\nint main(){return cpp4(cpp3(cpp2(cpp1(cpp0(22)))));}\n', cppflags)
    pre = [library(cxx, x) for x in ("Scrt1.o", "crti.o", "crtbeginS.o")]
    post = [library(cxx, x) for x in ("libstdc++.so", "libgcc_s.so.1", "libc.so.6", "crtendS.o", "crtn.o")]
    compare(binary, "cpp-exceptions", ["-pie", "--dedup-unwind", *pre, obj_a, obj_b, *post])
    print("CIE3155 COMPLETE", flush=True)


if __name__ == "__main__":
    executable = Path(sys.argv[1] if len(sys.argv) > 1 else os.environ["CIE3155_BINARY"]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="cie3155-") as working:
        os.chdir(working)
        main(str(executable))
