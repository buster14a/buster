#!/usr/bin/env python3
"""Lexical source census, NOT preprocessed or executed switch distribution."""
import bisect, hashlib, json, re, sys
from pathlib import Path

ROOT = Path(sys.argv[1])
OUT = Path(sys.argv[2])
SCOPE = sys.argv[3] if len(sys.argv)>3 else 'compiler-c'
lex = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|(?:u8|[LuU])?"(?:\\[\s\S]|[^"\\])*"|(?:[LuU])?\'(?:\\[\s\S]|[^\'\\])*\'|[A-Za-z_]\w*|(?:0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*|\.\.\.|[^\s]')
records=[]; inputs=[]; anomalies=[]
if SCOPE=='compiler-c': paths=(ROOT/'src/buster/lib/compiler').rglob('*.c')
elif SCOPE=='lib-c': paths=(ROOT/'src/buster/lib').rglob('*.c')
elif SCOPE=='lib-h': paths=(ROOT/'src/buster/lib').rglob('*.h')
elif SCOPE=='entry-c': paths=[p for p in (ROOT/'src').rglob('*.c') if '/lib/' not in str(p) and '/tests/' not in str(p)]
else: raise ValueError(SCOPE)
for p in sorted(paths):
    raw=p.read_bytes(); text=raw.decode('utf-8',errors='replace')
    inputs.append({'path':str(p.relative_to(ROOT)),'sha256':hashlib.sha256(raw).hexdigest()})
    tokens=[]; offsets=[]
    for m in lex.finditer(text):
        tok=m.group()
        if not tok.startswith(('//','/*')):
            tokens.append(tok); offsets.append(m.start())
    match={}; stack=[]
    for i,t in enumerate(tokens):
        if t in ('(','[','{'): stack.append((t,i))
        elif t in (')',']','}'):
            if stack and '([{'.index(stack[-1][0])==')]}'.index(t):
                _,j=stack.pop(); match[j]=i
            else: anomalies.append({'path':str(p.relative_to(ROOT)),'offset':offsets[i],'kind':'lexical-unmatched-close'})
    spans=[]
    for i,t in enumerate(tokens):
        if t=='switch' and i+1<len(tokens) and tokens[i+1]=='(' and i+1 in match:
            op=match[i+1]+1
            if op<len(tokens) and tokens[op]=='{' and op in match:
                spans.append({'start':i,'body':op,'end':match[op],'case_tokens':[]})
    spans.sort(key=lambda x:x['body']); span_index=0; active=[]
    for i,t in enumerate(tokens):
        while active and active[-1]['end']<=i: active.pop()
        while span_index<len(spans) and spans[span_index]['body']==i:
            active.append(spans[span_index]); span_index+=1
        if t=='case' and active: active[-1]['case_tokens'].append(i)
    for s in spans:
        ranges=0; numeric=0; labels=[]
        for j in s['case_tokens']:
            k=j+1; q=0; extent=[]
            while k<s['end']:
                t=tokens[k]
                if t in ('(','[','{') and k in match:
                    extent.extend(tokens[k:match[k]+1]);k=match[k]+1;continue
                if t==':' and not q: break
                q+=t=='?';q-=t==':' and q>0
                extent.append(t);k+=1
            ranges+='...' in extent
            numeric+=bool(re.fullmatch(r'[+-]?(?:0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*',''.join(extent)))
            labels.append(' '.join(extent))
        count=len(s['case_tokens'])
        records.append({'path':str(p.relative_to(ROOT)),'line':text.count('\n',0,offsets[s['start']])+1,'cases':count,'gnu_ranges':ranges,'simple_numeric':numeric,'candidate_pairs':count*(count-1)//2,'label_expressions':labels})
counts=sorted(r['cases'] for r in records)
def pct(p): return counts[min(len(counts)-1,int((len(counts)-1)*p))] if counts else None
summary={'scope':SCOPE,'input_files':len(inputs),'source':'lexical production text; comments/string/char literals excluded, preprocessor branches all retained, macro-generated cases omitted; braced switches only; nested switch labels owned by innermost braced switch','switches':len(records),'cases':sum(counts),'p50':pct(.5),'p90':pct(.9),'p99':pct(.99),'max':max(counts,default=0),'candidate_pairs_one_pass':sum(r['candidate_pairs'] for r in records),'gnu_ranges':sum(r['gnu_ranges'] for r in records),'simple_numeric':sum(r['simple_numeric'] for r in records),'lexical_unmatched_close_count':len(anomalies),'top':sorted(records,key=lambda x:x['candidate_pairs'],reverse=True)[:15]}
OUT.write_text(json.dumps({'summary':summary,'inputs':inputs,'switches':records,'anomalies':anomalies},indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k!='top'},indent=2))
for r in summary['top']: print(r['path'],r['line'],r['cases'],r['candidate_pairs'])
