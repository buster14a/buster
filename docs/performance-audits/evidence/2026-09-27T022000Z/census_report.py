#!/usr/bin/env python3
"""Summarize identity-census TSV output. usage: census_report.py <binary> <census.tsv> [--sites N] [--kind K]"""
import sys, subprocess, collections, argparse
PH=['other','preprocess','syntax','semantic','lower','ir_prepare','codegen','object','write','link']
KI=['string_equal','hash64','fnv','symbol_intern','symbol_middle','symbol_insert','object_name_hash','link_name_hash','probe','map_row','structural','construct']
ap=argparse.ArgumentParser(); ap.add_argument('binary'); ap.add_argument('tsv'); ap.add_argument('--sites',type=int,default=0); ap.add_argument('--kind',default=None); ap.add_argument('--phase',default=None)
a=ap.parse_args()
rows=[]
for line in open(a.tsv):
    if line.startswith('#'): continue
    p,k,s0,s1,c,b,x=line.split('\t')
    rows.append((int(p),int(k),int(s0,16),int(s1,16),int(c),int(b),int(x)))
tot=collections.defaultdict(lambda:[0,0,0])
for p,k,s0,s1,c,b,x in rows:
    t=tot[(p,k)]; t[0]+=c; t[1]+=b; t[2]+=x
print("phase\tkind\tcalls\tbytes\taux")
for (p,k),(c,b,x) in sorted(tot.items()):
    print(f"{PH[p]}\t{KI[k]}\t{c}\t{b}\t{x}")
if a.sites:
    sel=[r for r in rows if (a.kind is None or KI[r[1]]==a.kind) and (a.phase is None or PH[r[0]]==a.phase)]
    agg=collections.defaultdict(lambda:[0,0,0])
    for p,k,s0,s1,c,b,x in sel:
        t=agg[(p,k,s0,s1)]; t[0]+=c; t[1]+=b; t[2]+=x
    top=sorted(agg.items(), key=lambda kv:-kv[1][0])[:a.sites]
    addrs=sorted({hex(s) for (p,k,s0,s1),_ in top for s in (s0,s1) if s})
    sym={}
    if addrs:
        out=subprocess.run(['addr2line','-f','-C','-i','-s','-e',a.binary]+[hex(int(x,16)-1) for x in addrs],capture_output=True,text=True).stdout
        # -i may print multiple frames per address; re-run per address for robustness
        for ad in addrs:
            o=subprocess.run(['addr2line','-f','-i','-s','-e',a.binary,hex(int(ad,16)-1)],capture_output=True,text=True).stdout.strip().split('\n')
            frames=[f"{o[i]}@{o[i+1]}" for i in range(0,len(o)-1,2)]
            sym[ad]=' <- '.join(frames)
    print()
    print("calls\tbytes\taux\tphase\tkind\tsite0\tsite1")
    for (p,k,s0,s1),(c,b,x) in top:
        print(f"{c}\t{b}\t{x}\t{PH[p]}\t{KI[k]}\t{sym.get(hex(s0),hex(s0)) if s0 else '-'}\t{(sym.get(hex(s1),hex(s1)) if s1 else '-') if s0 else 'tag='+str(s1)}")
