#!/usr/bin/env python3
"""Run focused Win64 ABI tests on Linux x86-64; this does not test PE unwinding."""
import argparse, json, struct, subprocess
from pathlib import Path
parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, default=Path.cwd())
parser.add_argument('--ide', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--fixture', type=Path, required=True)
parser.add_argument('--host', type=Path, required=True)
options = parser.parse_args()
repo = options.repo.resolve()
ide = options.ide.resolve()
out = options.out.resolve()
out.mkdir(parents=True, exist_ok=False)
records = []
def run(args):
    command = [str(arg) for arg in args]
    result = subprocess.run(command, cwd=repo, capture_output=True, text=True, timeout=30)
    records.append({'command': command, 'exit': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr})
    (out / 'results.json').write_text(json.dumps(records, indent=2))
    if result.returncode:
        raise RuntimeError(records[-1])
def convert(source, destination):
    # GNU objcopy maps COFF REL32 to ELF PC32 without changing its addend.
    # COFF's displacement uses P+4 and an inline addend, whereas ELF uses P
    # and a RELA addend; transfer the inline addend and subtract four once
    # after a fresh conversion. The independent Clang control validates this
    # transport before any Buster result is accepted.
    run(['objcopy', '-I', 'pe-x86-64', '-O', 'elf64-x86-64', '--remove-section=.pdata', '--remove-section=.xdata', source, destination])
    # COFF coalesces compiler-generated floating constants through COMDAT;
    # objcopy does not preserve that ELF linkage. Each fixture's references
    # are local, so retain its own constants under local symbol bindings.
    run(['objcopy', '--wildcard', '--localize-symbol=__real@*', '--localize-symbol=__xmm@*', destination])
    data = bytearray(destination.read_bytes())
    assert data[:6] == bytes([127, 69, 76, 70, 2, 1])
    header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
    assert header[2] == 62
    adjusted = 0
    for index in range(header[12]):
        section = struct.unpack_from('<IIQQQQIIQQ', data, header[6] + index * header[11])
        if section[1] == 4:
            assert section[9] == 24 and section[5] % 24 == 0
            for offset in range(section[4], section[4] + section[5], 24):
                location, info, addend = struct.unpack_from('<QQq', data, offset)
                if info & 0xffffffff == 2:
                    target = struct.unpack_from('<IIQQQQIIQQ', data, header[6] + section[7] * header[11])
                    original = struct.unpack_from('<i', data, target[4] + location)[0]
                    struct.pack_into('<q', data, offset + 16, addend + original - 4)
                    struct.pack_into('<i', data, target[4] + location, 0)
                    adjusted += 1
    destination.write_bytes(data)
    records.append({'converted': str(destination), 'pc32_adjustments': adjusted})
host = options.host.resolve().read_text()
first_definition = host.index('long long native_aggregate_host_mix(')
main = host.index('int main(void)')
callees = host[host.index('typedef __builtin_va_list'):main]
(out / 'callees.c').write_text(callees)
observer = host[:first_definition]
for spelling in ['long long native_aggregate_', 'struct NativeAggregate24 native_aggregate_', 'int native_aggregate_']:
    observer = observer.replace('\n' + spelling, '\n__attribute__((ms_abi)) ' + spelling)
observer += host[main:]
(out / 'observer.c').write_text(observer)
run(['clang', '-target', 'x86_64-pc-windows-msvc', '-O2', '-c', out / 'callees.c', '-o', out / 'callees.obj'])
convert(out / 'callees.obj', out / 'callees.o')
subjects = [('clang-control', ['clang', '-target', 'x86_64-pc-windows-msvc', '-O2'])]
for target in ['windows', 'uefi']:
    for mode in ['none', 'mir-stack', 'fast', 'quality']:
        for ssa in ['-ffrontend-ssa', '-fno-frontend-ssa']:
            flags = [ide, 'cc', '-target', 'x86_64-pc-' + target, '-g0', '-fregister-allocator=' + mode, ssa, '-fverify-codegen']
            if mode != 'none':
                flags += ['-fno-machine-fallback']
            subjects.append((target + '-' + mode + ssa, flags))
for name, compiler in subjects:
    run([*compiler, '-c', options.fixture.resolve(), '-o', out / (name + '.obj')])
    convert(out / (name + '.obj'), out / (name + '.o'))
    run(['clang', '-O2', out / 'observer.c', out / (name + '.o'), out / 'callees.o', '-o', out / name])
    run([out / name])
    print(name + ' passed', flush=True)
(out / 'results.json').write_text(json.dumps(records, indent=2))
print('Independent Clang control and all 16 Buster mixed-compiler legs passed.', flush=True)
