"""One-off CI audit for #280; independent sources/bytes, not a production encoder."""
from pathlib import Path
import hashlib, json, os, re, subprocess, sys

out = Path(os.environ['RUNNER_TEMP']) / 'a07-final'
out.mkdir(exist_ok=True)
ide = Path('build/Release/ide').resolve()
results = {'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(), 'sources': {}, 'negative': [], 'runtime': []}

def run(args, *, reject=False):
    command = [str(arg) for arg in args]
    # objcopy without an outfile destructively rewrites its input, including
    # empty symbol tables. Preserve the actual original Buster object for
    # the independent full-object repeat comparison and later linking.
    observed = Path(command[-1]) if command[0] == 'objcopy' else None
    original = observed.read_bytes() if observed is not None else None
    if observed is not None:
        command.append(str(out / ('objcopy-' + observed.name)))
    print('+', ' '.join(command), flush=True)
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(completed.stdout, end='', flush=True)
    if (completed.returncode == 0) == reject:
        raise RuntimeError(f'unexpected exit {completed.returncode}: {command}')
    if observed is not None and observed.read_bytes() != original:
        raise RuntimeError(f'oracle extraction changed input {observed}')
    return completed.stdout

import atexit
atexit.register(lambda: (out / 'partial-results.json').write_text(json.dumps(results, indent=2) + '\n'))

sources = {'att': [], 'intel': []}
expected = {'att': bytearray(), 'intel': bytearray()}
for mnemonic, divisor, element, prefix in [('vcvtps2pd', 2, 4, 0x7c), ('vcvtpd2ps', 1, 8, 0xfd)]:
    for size, vl in enumerate([128, 256, 512]):
        reg = ['xmm', 'ymm', 'zmm'][size] if divisor == 2 else ['xmm', 'xmm', 'ymm'][size]
        memory = vl // divisor
        for broadcast in [False, True]:
            scale = element if broadcast else memory // 8
            vector = vl // 8
            disps = [0, 1, -1, scale, -scale, vector, -vector, 127*scale, -128*scale,
                     128*scale, -129*scale, 127*scale+1, -128*scale-1, 127*vector, -128*vector, 8192, -8192, 64]
            suffix = '{1to%d}' % (memory // (8 * element)) if broadcast else ''
            qualifier = {32:'dword',64:'qword',128:'xmmword',256:'ymmword',512:'zmmword'}[element*8 if broadcast else memory]
            for displacement in disps:
                for address, (att, intel, base) in enumerate([('(%rax)','rax',0),('(%rbp)','rbp',5),('(%rax,%rcx,4)','rax+rcx*4',0)]):
                    compressed = displacement % scale == 0 and -128 <= displacement // scale <= 127
                    mod = 0 if displacement == 0 and base != 5 else 1 if compressed else 2
                    data = bytearray([0x62, 0xf1, prefix, 0x09 | size << 5 | int(broadcast) << 4, 0x5a,
                                      mod << 6 | (4 if address == 2 else base)])
                    if address == 2:
                        data.append(0x88)
                    if mod == 1:
                        data.append((displacement // scale) & 255)
                    elif mod == 2:
                        data.extend(displacement.to_bytes(4, 'little', signed=True))
                    # Unsized XMM narrowing loads are ambiguous; positive ordinary
                    # VL=128/256 coverage uses explicit Intel qualifiers. Both
                    # unsized dialects are tested as independent rejection cases.
                    if not (mnemonic == 'vcvtpd2ps' and vl != 512 and not broadcast):
                        sources['att'].append(f'{mnemonic} {displacement}{att}{suffix}, %{reg}0{{%k1}}')
                        expected['att'].extend(data)
                    sources['intel'].append(f'{mnemonic} {reg}0{{k1}}, {qualifier} ptr [{intel}{displacement:+d}]{suffix}')
                    expected['intel'].extend(data)

sources['intel'].append('vmovntdqa zmm0, zmmword ptr [rax]')
expected['intel'].extend(bytes.fromhex('62 f2 7d 48 2a 00'))

for dialect, lines in sources.items():
    source = out / f'conversions-{dialect}.s'
    source.write_text(('.intel_syntax noprefix\n' if dialect == 'intel' else '') + '.text\n' + '\n'.join(lines) + '\n')
    outputs = {}
    for tool in ['gas', 'clang', 'buster']:
        obj = source.with_suffix('.' + tool + '.o')
        if tool == 'gas':
            run(['as', '--64', source, '-o', obj])
        elif tool == 'clang':
            run(['clang', '--target=x86_64-unknown-linux-gnu', '-c', source, '-o', obj])
        else:
            run([ide, 'cc', '-c', '-march=znver5', source, '-o', obj])
        binary = obj.with_suffix('.bin')
        run(['objcopy', '--dump-section', f'.text={binary}', obj])
        data = binary.read_bytes()
        if data != expected[dialect]:
            mismatch = next((i for i, (a,b) in enumerate(zip(data, expected[dialect])) if a != b), min(len(data),len(expected[dialect])))
            raise RuntimeError(f'{dialect}/{tool}: byte mismatch at {mismatch}, actual={data[mismatch:mismatch+16].hex()} expected={expected[dialect][mismatch:mismatch+16].hex()}')
        outputs[tool] = hashlib.sha256(data).hexdigest()
    repeated = source.with_suffix('.repeat.o')
    run([ide, 'cc', '-c', '-march=znver5', source, '-o', repeated])
    if repeated.read_bytes() != source.with_suffix('.buster.o').read_bytes():
        raise RuntimeError(f'{dialect}: nondeterministic repeated object')
    results['sources'][dialect] = {'instructions':len(lines), 'bytes':len(expected[dialect]), 'sha256':outputs, 'object_repeat_equal':True}

negative = [
    'vmovntdqa zmm0, dword ptr [rax]',
    'vcvtps2pd xmm0{k1}, dword ptr [rax]',
    'vcvtps2pd ymm0{k1}, qword ptr [rax]',
    'vcvtps2pd ymm0{k1}, ymmword ptr [rax]',
    'vcvtps2pd zmm0{k1}, zmmword ptr [rax]',
    'vcvtps2pd zmm0{k1}, qword ptr [rax]{1to8}',
    'vcvtps2pd xmm0{k1}, dword ptr [rax]{1to4}',
    'vcvtpd2ps xmm0{k1}, qword ptr [rax]',
    'vcvtpd2ps ymm0{k1}, ymmword ptr [rax]',
    'vcvtpd2ps ymm0{k1}, dword ptr [rax]{1to8}',
]
for index, instruction in enumerate(negative):
    source = out / f'invalid-{index}.s'
    source.write_text('.intel_syntax noprefix\n.text\n' + instruction + '\n')
    obj = out / f'invalid-{index}.o'
    run(['as','--64',source,'-o',obj], reject=True)
    run(['clang','--target=x86_64-unknown-linux-gnu','-c',source,'-o',obj], reject=True)
    run([ide,'cc','-c','-march=znver5',source,'-o',obj], reject=True)
    results['negative'].append(instruction)

for displacement in [0,64,2032,4064]:
    for dialect in ['att','intel']:
        instruction = (f'vcvtpd2ps {displacement}(%rax), %xmm0{{%k1}}' if dialect == 'att'
                       else f'vcvtpd2ps xmm0{{k1}}, [rax+{displacement}]')
        source = out / f'ambiguous-{dialect}-{displacement}.s'
        source.write_text(('.intel_syntax noprefix\n' if dialect == 'intel' else '') + '.text\n' + instruction + '\n')
        obj = source.with_suffix('.o')
        run(['as','--64',source,'-o',obj], reject=True)
        run(['clang','--target=x86_64-unknown-linux-gnu','-c',source,'-o',obj], reject=True)
        run([ide,'cc','-c','-march=znver5',source,'-o',obj], reject=True)
        results['negative'].append(instruction)

feature_source = out / 'feature-disabled.s'
feature_source.write_text('.text\nvcvtps2pd 64(%rax),%zmm0{%k1}\n')
run([ide,'cc','-c','-march=haswell',feature_source,'-o',out/'feature-disabled.o'], reject=True)
results['feature_disabled_rejected'] = True

asm = out / 'interop.s'
asm.write_text('''.text
.globl a07_widen
.type a07_widen,@function
a07_widen:
 vcvtps2pd 64(%rdi),%zmm0
 vmovupd %zmm0,(%rsi)
 vzeroupper
 ret
.size a07_widen,.-a07_widen
.globl a07_narrow
.type a07_narrow,@function
a07_narrow:
 movl $255,%eax
 # Mask setup uses independently decoded KMOVW bytes; conversion stays source-assembled.
 .byte 0xc5,0xf8,0x92,0xc8
 vcvtpd2ps 64(%rdi),%ymm0{%k1}{z}
 vmovups %ymm0,(%rsi){%k1}
 vzeroupper
 ret
.size a07_narrow,.-a07_narrow
.globl a07_rip
.type a07_rip,@function
a07_rip:
 vcvtps2pd a07_floats+64(%rip){1to8},%zmm0
 vmovupd %zmm0,(%rdi)
 vzeroupper
 ret
.size a07_rip,.-a07_rip
.section .note.GNU-stack,"",@progbits
''')
caller = out / 'caller.c'
caller.write_text('''float a07_floats[32];
extern void a07_widen(const float *, double *);
extern void a07_narrow(const double *, float *);
extern void a07_rip(double *);
int main(void)
{
    double input[32];
    double wide[8];
    float narrow[8];
    double broadcast[8];
    int result = 0;
    for (int i = 0; i < 32; i += 1)
    {
        a07_floats[i] = (float)(i * 2 - 17);
        input[i] = (double)(i * 3 - 19);
    }
    a07_widen(a07_floats, wide);
    a07_narrow(input, narrow);
    a07_rip(broadcast);
    for (int i = 0; i < 8; i += 1)
    {
        if (wide[i] != (double)((i + 16) * 2 - 17)) result = 1;
        if (narrow[i] != (float)((i + 8) * 3 - 19)) result = 2;
        if (broadcast[i] != 15.0) result = 3;
    }
    return result;
}
''')
objects = {}
for tool in ['gas','clang','buster']:
    obj = out / f'interop-{tool}.o'
    if tool == 'gas':
        run(['as','--64',asm,'-o',obj])
    elif tool == 'clang':
        run(['clang','--target=x86_64-unknown-linux-gnu','-c',asm,'-o',obj])
    else:
        run([ide,'cc','-c','-march=znver5',asm,'-o',obj])
    objects[tool] = obj
    (out / f'{tool}-relocations.txt').write_text(run(['readelf','-Wr',obj]))
    (out / f'{tool}-metadata.txt').write_text(run(['readelf','-WhsS',obj]))
    (out / f'{tool}-disassembly.txt').write_text(run(['objdump','-drw',obj]))

# ELF x86-64 psABI: PC32 writes S+A-P. RIP is four bytes after its
# relocation field, hence source a07_floats+64 requires RELA addend 60.
results['relocations'] = {}
interop_text = None
for tool,obj in objects.items():
    text = (out / f'{tool}-relocations.txt').read_text()
    rows = [line.split() for line in text.splitlines() if 'R_X86_64_' in line]
    if len(rows) != 1 or rows[0][2] != 'R_X86_64_PC32' or rows[0][4:] != ['a07_floats','+','3c']:
        raise RuntimeError(f'{tool}: unexpected relocation contract: {rows}')
    results['relocations'][tool] = {'offset':int(rows[0][0],16),'type':rows[0][2],'symbol':rows[0][4],'addend':60}
    binary = out / f'interop-{tool}.bin'
    run(['objcopy','--dump-section',f'.text={binary}',obj])
    data = binary.read_bytes()
    if interop_text is not None and data != interop_text:
        raise RuntimeError(f'{tool}: interoperability text differs from independent assembler')
    interop_text = data
if len({row['offset'] for row in results['relocations'].values()}) != 1:
    raise RuntimeError('relocation offset differs between assemblers')
results['interop_text_bytes'] = len(interop_text)
results['interop_text_sha256'] = hashlib.sha256(interop_text).hexdigest()

probe = out / 'cpu.c'
probe.write_text('int main(void) { __builtin_cpu_init(); return !(__builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512vl")); }\n')
run(['clang',probe,'-o',out/'cpu'])
can_execute = subprocess.run([str(out/'cpu')]).returncode == 0
results['cpu_execution'] = can_execute
(out/'cpu.txt').write_text(run(['lscpu']))
host = out/'caller-clang.o'
run(['clang','-std=c11','-Wall','-Wextra','-Werror','-O2','-fno-pie','-c',caller,'-o',host])

def link_and_execute(label, command):
    executable = out / label
    run(command + ['-o', executable])
    row = {'case':label, 'linked':True, 'executed':can_execute}
    if can_execute:
        run([executable])
        row['exit'] = 0
    results['runtime'].append(row)

for tool,obj in objects.items():
    link_and_execute('host-caller-'+tool,['clang','-no-pie',host,obj])
link_and_execute('buster-link-host-caller',[ide,'cc',host,objects['buster']])
for mode in ['none','mir-stack','fast','quality']:
    obj = out/f'caller-{mode}.o'
    run([ide,'cc','-c','-O2','-fregister-allocator='+mode,caller,'-o',obj])
    for tool in ['gas','clang']:
        link_and_execute('buster-'+mode+'-'+tool,['clang','-no-pie',obj,objects[tool]])
    link_and_execute('buster-link-'+mode,[ide,'cc',obj,objects['clang']])

(out / 'results.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))

