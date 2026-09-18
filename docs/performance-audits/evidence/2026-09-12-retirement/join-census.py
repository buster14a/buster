import csv,collections,json,hashlib,sys
from pathlib import Path
root=Path(__file__).parent
prefix=sys.argv[1];shards=int(sys.argv[2]) if len(sys.argv)>2 else 4;out=root/(prefix+'-joined-v3');out.mkdir(exist_ok=False)
def read(p):
 with p.open() as f:return list(csv.DictReader(f,delimiter='\t'))
def fields(t):return dict(w.split('=',1) for w in t.split() if '=' in w)
def write(name,rows):
 if rows:
  with (out/name).open('w') as f:
   w=csv.DictWriter(f,fieldnames=list(rows[0]),delimiter='\t');w.writeheader();w.writerows(rows)
identity=None;inventory=None;results={};records={};counters={};manifests=[]
for i in range(shards):
 d=root/f'{prefix}-{i}';rows=read(d/'rows.tsv');ids={r['row']:{k:v for k,v in r.items() if k!='selected'} for r in rows}
 if identity is None:identity=ids;inventory=(d/'inputs.tsv').read_bytes()
 assert identity==ids and inventory==(d/'inputs.tsv').read_bytes(), 'inventory drift'
 manifest=dict(x.split('=',1) for x in (d/'manifest.txt').read_text().splitlines());manifests.append(manifest)
 assert (d/'summary.txt').exists(), 'unfinished shard'
 for r in read(d/'results.tsv'):
  assert r['row'] not in results and rows[int(r['row'])]['selected']=='1', 'duplicate or unselected row'
  assert int(r['group'])%shards==i;results[r['row']]=r
 for r in read(d/'fallback-functions.tsv'):
  assert r['record_valid']=='1';p=fields(r['telemetry']);key=r['row'];ident=ids[key]
  target=ident['target'];arch=target.split('-')[0];os_name=target.split('-')[-1]
  if os_name=='gnu':os_name='linux'
  if os_name=='msvc':os_name='windows'
  assert p['allocator']==ident['allocator'] and p['target']==arch+'-'+os_name,(key,target,p['target'])
  p['telemetry_target']=p.pop('target');p.pop('allocator')
  p['source']=bytes.fromhex(p.pop('source_hex')).decode();p['function']=bytes.fromhex(p.pop('function_hex')).decode()
  p['source']=p['source'].removeprefix(str(d/'inputs')+'/')
  records.setdefault(key,[]).append(p)
 for r in read(d/'fallback-counters.tsv'):counters.setdefault(r['row'],[]).append(r['telemetry'])
assert set(results)==set(identity), 'missing rows'
for m in manifests[1:]:assert {k:v for k,v in m.items() if k!='shard_index'}=={k:v for k,v in manifests[0].items() if k!='shard_index'},'provenance drift'
for key,r in results.items():
 rs=records.get(key,[])
 assert len(rs)==int(r['fallbacks']),(key,len(rs),r['fallbacks'])
 assert len({x['function_id'] for x in rs})==len(rs)
 reasons=collections.Counter(x['reason'] for x in rs);claimed=collections.Counter()
 opcodes=collections.Counter('47' if x['opcode_id']=='4294967295' else x['opcode_id'] for x in rs);opclaimed=collections.Counter()
 for text in counters.get(key,[]):
  f=fields(text)
  if text.startswith('CODEGEN_FALLBACK_REASON '):claimed[f['reason']]+=int(f['count'])
  if text.startswith('CODEGEN_FALLBACK opcode='):opclaimed[f['opcode']]+=int(f['count'])
 assert reasons==claimed,(key,reasons,claimed)
 assert opcodes==opclaimed,(key,opcodes,opclaimed)
joined=[dict(identity[k],**{f:v for f,v in results[k].items() if f not in identity[k]},cpu=manifests[0]['cpu']) for k in sorted(results,key=int)]
write('results.tsv',joined)
functions=[dict(identity[k],**p,cpu=manifests[0]['cpu'],disposition=results[k]['disposition']) for k in sorted(records,key=int) for p in records[k]]
write('functions.tsv',functions)
gaps=[r for r in joined if r['disposition']=='supported-native-gap'];write('supported-native-gaps.tsv',gaps)
write('gap-functions.tsv',[r for r in functions if r['disposition']=='supported-native-gap'])
write('unresolved.tsv',[r for r in joined if 'unresolved' in r['disposition']])
summary={'scope':'complete tracked-test baseline-CPU object matrix','compiler_revision':manifests[0]['compiler_revision_claim'],'baseline_revision':manifests[0]['baseline_revision_claim'],'compiler_hash':manifests[0]['compiler_hash'],'rows':len(joined),'groups':len(joined)//4,'subjects':len({r['fixture'] for r in joined}),'dispositions':dict(collections.Counter(r['disposition'] for r in joined)),'supported_native_gap_rows':len(gaps),'gap_fixtures':dict(sorted(collections.Counter(r['fixture'] for r in gaps).items())),'fallback_function_records':len(functions),'rows_reconciled':True,'counters_reconciled':True,'retirement_accepted':False,'unfrozen_dependencies':manifests[0]['unfrozen_dependencies']}
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
