"""Retain original trace lines needed for the selected-placement census.

This drops trial placements and local events for values with no boundary edit.
Every retained line is copied byte-for-byte, including pointer aliases.
"""
import bisect,collections,sys
def relevant_events(rows):
    blocks={};pred=collections.defaultdict(list);entry=collections.defaultdict(set);out=collections.defaultdict(set);defs={};events=collections.defaultdict(list);boundaries=[]
    for i,row in enumerate(rows):
        xs=row.split();kind=xs[1]
        if kind=='P':blocks[int(xs[3])]=[int(x) for x in xs[4:6]]
        elif kind=='E':pred[int(xs[4])].append(int(xs[3]))
        elif kind in ('I','O'):(entry if kind=='I' else out)[int(xs[3])].add(int(xs[4]))
        elif kind=='V':defs[int(xs[3])]=int(xs[4])
        elif kind=='B':boundaries.append((i,*map(int,xs[3:])))
    starts=[blocks[b][0] for b in range(len(blocks))]
    for i,row in enumerate(rows):
        xs=row.split()
        if xs[1] in ('L','X'):
            point,value=map(int,xs[3:5]);b=bisect.bisect_right(starts,point>>2)-1
            events[b,value].append((point,i))
    for values in events.values():values.sort()
    retained=set()
    def query(block,value,point,sequence):
        values=events.get((block,value),[]);index=bisect.bisect_right(values,(point,sequence))-1
        if index>=0:retained.add(values[index][1])
    for seq,point,kind,value,dest in boundaries:
        source=bisect.bisect_right(starts,point>>2)-1
        if kind==2:query(source,value,point,seq)
        elif defs.get(value)!=dest and dest!=4294967295 and value not in entry[dest]:
            for p in pred[dest]:
                if p!=source and value not in out[p]:query(p,value,4*(blocks[p][0]+blocks[p][1]),10**15)
    return retained
attempts={};pointers={};aliases=[]
with open(sys.argv[1]) as source, open(sys.argv[2],'w') as out:
    for line in source:
        if not line.startswith('R '):continue
        xs=line.split();kind=xs[1]
        if kind=='T':
            pointers[xs[3]]=pointers[xs[2]];aliases.append(line)
        elif kind=='F':
            aid=pointers[xs[2]];rows=attempts[aid]
            values={row.split()[5] for row in rows if row.startswith('R B ')}
            events=relevant_events(rows)
            for i,row in enumerate(rows):
                ys=row.split();k=ys[1]
                if k in ('L','X') and i not in events:continue
                index={'I':4,'O':4,'V':3,'L':4,'X':4}.get(k)
                if index is None or ys[index] in values:out.write(row)
            for alias in aliases:
                if pointers[alias.split()[3]]==aid:out.write(alias)
            out.write(line);attempts={};pointers={};aliases=[]
        else:
            aid=int(xs[2])
            if kind=='A':attempts[aid]=[]
            attempts[aid].append(line)
            if kind=='Z':pointers[xs[3]]=aid
