from pathlib import Path
import sys
p=Path(sys.argv[1])
s=p.read_text()
s=s.replace('import hashlib, json, os, subprocess, sys','import hashlib, json, os, re, subprocess, sys')
old='''                    # An unqualified AT&T XMM narrowing load defaults to VL=128.
                    # Use Intel's explicit m256 for the ambiguous VL=256 ordinary case.
                    if not (mnemonic == 'vcvtpd2ps' and vl == 256 and not broadcast):'''
new='''                    # Unsized XMM narrowing loads are ambiguous; positive ordinary
                    # VL=128/256 coverage uses explicit Intel qualifiers. Both
                    # unsized dialects are tested as independent rejection cases.
                    if not (mnemonic == 'vcvtpd2ps' and vl != 512 and not broadcast):'''
assert s.count(old)==1
s=s.replace(old,new)
anchor="asm = out / 'interop.s'"
addition='''for displacement in [0,64,2032,4064]:
    for dialect in ['att','intel']:
        instruction = (f'vcvtpd2ps {displacement}(%rax), %xmm0{{%k1}}' if dialect == 'att'
                       else f'vcvtpd2ps xmm0{{k1}}, [rax+{displacement}]')
        source = out / f'ambiguous-{dialect}-{displacement}.s'
        source.write_text(('.intel_syntax noprefix\\n' if dialect == 'intel' else '') + '.text\\n' + instruction + '\\n')
        obj = source.with_suffix('.o')
        run(['as','--64',source,'-o',obj], reject=True)
        run(['clang','--target=x86_64-unknown-linux-gnu','-c',source,'-o',obj], reject=True)
        run([ide,'cc','-c','-march=znver5',source,'-o',obj], reject=True)
        results['negative'].append(instruction)

'''
assert s.count(anchor)==1
s=s.replace(anchor,addition+anchor)
anchor="probe = out / 'cpu.c'"
addition='''# ELF x86-64 psABI: PC32 writes S+A-P. RIP is four bytes after its
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

'''
assert s.count(anchor)==1
s=s.replace(anchor,addition+anchor)
p.write_text(s)
