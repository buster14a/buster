"""Diagnostic local-absence census; only the final selected placement counts.

Never-loaded means no residency in the source block, not a proof of a root
cause on every transitive path. Multiple missing predecessors retain their
different reasons. Non-call forced-register losses are separate from pressure.
"""
import bisect, collections, json, sys

def analyze(a):
    blocks=a['blocks'];starts=[blocks[b][0] for b in range(len(blocks))]
    for es in a['events'].values(): es.sort()
    def absent(block,value,point,sequence=10**15):
        events=a['events'].get((block,value),[])
        i=bisect.bisect_right(events,(point,sequence,99))-1
        if i>=0:
            code=events[i][2]
            return {2:'call_flush',3:'pressure_eviction',4:'forced_eviction',5:'edge_publication_release',6:'contract_replacement',7:'tied_source_release',0:'release_unattributed'}.get(code,'unknown')
        if value in a['entry'][block]: return 'release_unattributed'
        if any(value in a['out'][p] for p in a['pred'][block]): return 'contract_intersection'
        return 'never_loaded_in_source_block'
    counts={1:collections.Counter(),2:collections.Counter()}
    for seq,point,kind,value,dest in a['boundary']:
        source=bisect.bisect_right(starts,point>>2)-1
        if kind==2:
            cause=absent(source,value,point,seq)
        elif a['definitions'].get(value)==dest:
            cause='parameter_home'
        elif dest==4294967295:
            cause='dispatch_empty_contract'
        elif value in a['entry'][dest]:
            cause='clean_contract_or_cycle'
        else:
            missing=[p for p in a['pred'][dest] if p!=source and value not in a['out'][p]]
            reasons={absent(p,value,4*(blocks[p][0]+blocks[p][1])) for p in missing}
            cause='+'.join(sorted(reasons)) if reasons else 'contract_policy'
        counts[kind][cause]+=1
    # The sole boundary-spill producer outside conform_append is the QUALITY
    # split landing-pad store. Z includes it; preserve it as its own category.
    split_stores=a['expected'][0]-sum(counts[1].values())
    assert split_stores>=0 and sum(counts[2].values())==a['expected'][1], (a['expected'],counts)
    if split_stores: counts[1]['quality_split_landing_store']+=split_stores
    return counts

def main(path):
    attempts={};pointers={};total={1:collections.Counter(),2:collections.Counter()};chosen=0;seq=0
    with open(path) as source:
        for line in source:
            if not line.startswith('R '): continue
            seq+=1;xs=line.split();kind=xs[1]
            if kind=='T':
                assert xs[2] in pointers, ('missing predicate input',xs)
                pointers[xs[3]]=pointers[xs[2]]
                continue
            if kind=='F':
                assert xs[2] in pointers, ('missing selected placement',xs)
                a=attempts[pointers[xs[2]]]
                c=analyze(a)
                for k in (1,2): total[k].update(c[k])
                chosen+=1;attempts={};pointers={}
                continue
            aid=int(xs[2]);ns=list(map(int,xs[3:])) if kind!='Z' else None
            if kind=='A':
                attempts[aid]={'blocks':{},'pred':collections.defaultdict(list),'entry':collections.defaultdict(set),'out':collections.defaultdict(set),'events':collections.defaultdict(list),'boundary':[],'definitions':{}}
            else:
                a=attempts[aid]
                if kind=='P': a['blocks'][ns[0]]=ns[1:]
                elif kind=='E': a['pred'][ns[1]].append(ns[0])
                elif kind=='I': a['entry'][ns[0]].add(ns[1])
                elif kind=='O': a['out'][ns[0]].add(ns[1])
                elif kind=='V': a['definitions'][ns[0]]=ns[1]
                elif kind in ('X','L'):
                    point,value=ns[:2]
                    if 'starts' not in a: a['starts']=[a['blocks'][b][0] for b in range(len(a['blocks']))]
                    b=bisect.bisect_right(a['starts'],point>>2)-1
                    a['events'][b,value].append((point,seq,ns[2] if kind=='X' else 0))
                elif kind=='B': a['boundary'].append((seq,*ns))
                elif kind=='Z':
                    pointers[xs[3]]=aid;a['expected']=list(map(int,xs[4:6]))
    return {'selected_functions':chosen,'spills':dict(total[1]),'reloads':dict(total[2]),'totals':[sum(total[k].values()) for k in (1,2)]}

if __name__=='__main__':
    result=main(sys.argv[1]);print(json.dumps(result,indent=2,sort_keys=True))
