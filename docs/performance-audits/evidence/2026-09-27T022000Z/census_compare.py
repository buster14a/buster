#!/usr/bin/env python3
import sys, collections
PH=['other','preprocess','syntax','semantic','lower','ir_prepare','codegen','object','write','link']
KI=['string_equal','hash64','fnv','symbol_intern','symbol_middle','symbol_insert','object_name_hash','link_name_hash','probe','map_row','structural','construct']
def load(p):
    t=collections.defaultdict(lambda:[0,0,0])
    for line in open(p):
        if line.startswith('#'): continue
        ph,k,s0,s1,c,b,x=line.split('\t')
        key=(PH[int(ph)],KI[int(k)])
        if int(s0,16)==0: key=key+('tag%d'%int(s1,16),)
        v=t[key]; v[0]+=int(c); v[1]+=int(b); v[2]+=int(x)
    return t
a=load(sys.argv[1]); b=load(sys.argv[2])
print(f"{'phase':<11}{'kind':<17}{'tag':<7}{'calls A':>12}{'calls B':>12}{'Δcalls':>12}{'bytes A':>14}{'bytes B':>14}{'aux A':>12}{'aux B':>12}")
for k in sorted(set(a)|set(b), key=lambda k:(PH.index(k[0]),KI.index(k[1]),k[2:] )):
    x=a.get(k,[0,0,0]); y=b.get(k,[0,0,0])
    tag=k[2] if len(k)>2 else ''
    print(f"{k[0]:<11}{k[1]:<17}{tag:<7}{x[0]:>12}{y[0]:>12}{y[0]-x[0]:>12}{x[1]:>14}{y[1]:>14}{x[2]:>12}{y[2]:>12}")
