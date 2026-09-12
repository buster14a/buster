#!/usr/bin/env python3
"""Untimed temporary instrumentation of actual address consumers, no source edits."""
import argparse, json, pathlib, re, shlex, subprocess
p=argparse.ArgumentParser();p.add_argument('root');p.add_argument('output');p.add_argument('--candidate',action='store_true');a=p.parse_args()
root=pathlib.Path(a.root).resolve();out=pathlib.Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
prefix=pathlib.Path('buster/lib/compiler/codegen');overlay=out/'src'/prefix;overlay.mkdir(parents=True,exist_ok=True)
files=['machine.c','machine_select.c','machine_x86_64.c','machine_aarch64.c']
texts={name:(root/'src'/prefix/name).read_text() for name in files}
texts['machine_select.c']='#include <stdio.h>\n'+texts['machine_select.c']
pos=texts['machine_select.c'].index('\n// Shared')
texts['machine_select.c']=texts['machine_select.c'][:pos]+'\nstatic u64 address_queries, address_definition_reads, address_type_reads;\n'+texts['machine_select.c'][pos:]
if a.candidate:
 s=texts['machine_select.c'];needle='    if (value.value < function->value_count)\n    {\n        if (!cache->entries)';s=s.replace(needle,'    address_queries += 1;\n'+needle)
 s=s.replace('        if (definition)\n        {','        if (definition)\n        {\n            address_definition_reads += 1;',1)
 s=re.sub(r'^([ \t]*)(IrType\* \w+ = ir_type_from_id)',r'\1address_type_reads += 1;\n\1\2',s,flags=re.M)
 texts['machine_select.c']=s
else:
 for arch,tag in [('x86_64','x64'),('aarch64','a64')]:
  s=texts['machine_'+arch+'.c']
  for name in ['select_place_address_offset','select_field','select_index']+(['select_load','select_store'] if tag=='a64' else []):
   start=s.index('BUSTER_GLOBAL_LOCAL bool machine_'+tag+'_'+name+'(');brace=s.index('\n{',start)+2;end=s.index('\nBUSTER_GLOBAL_LOCAL',brace)
   part=s[brace:end];part='\n    address_queries += 1;'+part
   if tag=='a64' and name in ['select_place_address_offset','select_load','select_store']:
    part=part.replace('IrInstruction* definition =','address_definition_reads += 1;\n    IrInstruction* definition =',1)
   reads=(1 if name=='select_field' else (2 if tag=='a64' else 1) if name=='select_index' else 1 if tag=='a64' and name=='select_place_address_offset' else 0)
   if reads:part='\n    address_type_reads += '+str(reads)+';'+part
   s=s[:brace]+part+s[end:]
  texts['machine_'+arch+'.c']=s
s=texts['machine.c'];start=s.index('BUSTER_GLOBAL_LOCAL MachineSelectResult machine_select_canonical_function_internal(');brace=s.index('\n{',start)+2;end=s.index('\nMachineSelectResult machine_select_canonical_function(',brace)
part=s[brace:end];part='\n    address_queries = address_definition_reads = address_type_reads = 0;'+part
part=part.replace('    return result;','    fprintf(stderr, "ADDRESS_CENSUS arch=%u function=%.*s queries=%llu definitions=%llu types=%llu\\n", (unsigned)target.cpu_arch, (int)function->name.length, function->name.pointer, (unsigned long long)address_queries, (unsigned long long)address_definition_reads, (unsigned long long)address_type_reads);\n    return result;')
s=s[:brace]+part+s[end:];texts['machine.c']=s
for name,txt in texts.items():(overlay/name).write_text(txt)
config='Debug' if a.candidate else 'Release'
entries=json.loads((root/'build/compile_commands.json').read_text());entry=next(x for x in entries if x['file'].endswith('/machine.c') and '/'+config+'/' in x['output'])
args=[x.replace('\\"','"') for x in shlex.split(entry['command'])];args[1:1]=['-I'+str(out/'src')]
args=[('-O0' if x.startswith('-O') else str(overlay/'machine.c') if x==entry['file'] else x) for x in args];args[args.index('-o')+1]=str(out/'machine.o')
subprocess.run(args,cwd=entry['directory'],check=True)
commands=subprocess.check_output(['ninja','-f','build-'+config+'.ninja','-t','commands',config+'/ide'],cwd=root/'build',text=True).splitlines()
link=next(x for x in reversed(commands) if ' -o '+config+'/ide' in x)
linkargs=shlex.split(link)
if linkargs[:2] == [':', '&&']: linkargs=linkargs[2:]
if linkargs[-2:] == ['&&', ':']: linkargs=linkargs[:-2]
linkargs[linkargs.index('-o')+1]=str(out/'ide')
linkargs=[str(out/'machine.o') if x.endswith('/compiler/codegen/machine.c.o') else x for x in linkargs]
subprocess.run(linkargs,cwd=root/'build',check=True)
(out/'build.json').write_text(json.dumps({'root':str(root),'config':config,'compile':args,'link':linkargs},indent=2))
