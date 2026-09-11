#!/usr/bin/env python3
"""Relink frozen baseline, replacing only machine.c compiled with identical flags."""
import argparse,json,pathlib,shlex,subprocess
p=argparse.ArgumentParser();p.add_argument('baseline');p.add_argument('candidate');p.add_argument('output');a=p.parse_args()
base=pathlib.Path(a.baseline).resolve();candidate=pathlib.Path(a.candidate).resolve();out=pathlib.Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
entries=json.loads((base/'build/compile_commands.json').read_text());entry=next(x for x in entries if x['file'].endswith('/machine.c') and '/Release/' in x['output'])
args=[x.replace('\\"','"') for x in shlex.split(entry['command'])];args[1:1]=['-I'+str(candidate/'src')];args=[str(candidate/'src/buster/lib/compiler/codegen/machine.c') if x==entry['file'] else x for x in args];args[args.index('-o')+1]=str(out/'machine.o')
subprocess.run(args,cwd=entry['directory'],check=True)
commands=subprocess.check_output(['ninja','-f','build-Release.ninja','-t','commands','Release/ide'],cwd=base/'build',text=True).splitlines();link=next(x for x in reversed(commands) if ' -o Release/ide' in x)
args_link=shlex.split(link)
if args_link[:2]==[':','&&']:args_link=args_link[2:]
if args_link[-2:]==['&&',':']:args_link=args_link[:-2]
args_link[args_link.index('-o')+1]=str(out/'ide');args_link=[str(out/'machine.o') if x.endswith('/compiler/codegen/machine.c.o') else x for x in args_link]
subprocess.run(args_link,cwd=base/'build',check=True)
(out/'build.json').write_text(json.dumps({'baseline':str(base),'candidate':str(candidate),'compile':args,'link':args_link},indent=2))
