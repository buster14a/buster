from pathlib import Path
import zipfile,json,csv,re,collections,hashlib
ROOT=Path(__file__).resolve().parent / 'raw'; OUT=Path(__file__).resolve().parent / 'derived'; OUT.mkdir(exist_ok=True)
rows=[];modules=[];fixtures=[];spawns=[];stages=[];manifest=[];records=[]
for p in sorted(ROOT.glob('desktop-*-*-1.zip')):
 z=zipfile.ZipFile(p); s=json.loads(z.read('matrix-phases/summary.json')); ident=s['identity']; lane=f"{ident['run_id']}:{ident['platform']}-{ident['architecture']}-{ident['shard']}"
 assert s['complete'] and not s['errors']; assert ident['source_revision']=='e424b387fcb51b00c1d19e87e2d369ae8a212792'; assert ident['run_id'] in ('36996168725','36991068435') and ident['run_attempt']=='1'
 manifest.append({'filename':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size,'lane':lane})
 sums=collections.Counter()
 for t in s['trees']:
  d={k:v/1e6 for k,v in t['elapsed_us'].items()}; sums.update(d)
  stages.append({'lane':lane,'tree':t['id'],'compiler':t['compiler'],'configs':t['configurations'],'sanitize':t['sanitize'],'fuzz':t['fuzz'],**d})
 rows.append({'lane':lane,**sums,'cores':s['logical_cpus'],'outer_jobs':s['outer_jobs'],'cpu_budget':s['cpu_budget'],'pool_overlap':s['max_observed_pool_overlap']})
 for name in z.namelist():
  if not name.startswith('unit-observations/') or not name.endswith('/test.log'): continue
  raw=z.read(name); receipt=json.loads(z.read(name.replace('test.log','observation.json')))
  assert hashlib.sha256(raw).hexdigest()==receipt['log_sha256']; assert receipt['capture_complete']; assert receipt['test_result']==0; assert receipt['binary_unchanged']
  # Diagnostic extraction of ASCII tagged records from raw bytes, not proof qualification or decoding arbitrary negative-test payloads.
  mod='unknown'; fixture='unknown'; fixture_counts=collections.Counter(); module_counts=collections.Counter()
  for line in raw.splitlines():
   if line.startswith(b'TEST_FIXTURE_START_V1'):
    d={k.decode():v.decode('ascii') for k,v in re.findall(rb'(\w+)=([^\s]+)',line)}
    if d.get('kind')=='module': mod=d['module'];fixture='body'
    elif d.get('kind')=='fixture': mod=d['module'];fixture=d['fixture']
   if line.startswith(b'Launched ['):
    module_counts[mod]+=1;fixture_counts[(mod,fixture)]+=1
   if line.startswith(b'TEST_MODULE_TIMING '):
    d={k.decode():v.decode('ascii') for k,v in re.findall(rb'(\w+)=([^\s]+)',line)}
    modules.append({'lane':lane,'log':name,**d,'seconds':int(d['duration_ns'])/1e9})
   if line.startswith(b'TEST_FIXTURE_TIMING '):
    d={k.decode():v.decode('ascii') for k,v in re.findall(rb'(\w+)=([^\s]+)',line)}
    fixtures.append({'lane':lane,'log':name,**d})
   if line.startswith((b'CI_UNIT_PROCESS_V1 ',b'CI_UNIT_PARTITION_V1 ',b'CI_UNIT_PLAN_V1 ')):
    records.append({'lane':lane,'log':name,'tag':line.decode('ascii')})
  spawns.extend({'lane':lane,'log':name,'module':m,'fixture':f,'logged_launches':v} for (m,f),v in fixture_counts.items())
for name,data in [('lane_phases',rows),('tree_phases',stages),('module_timings',modules),('logged_process_launches',spawns),('artifacts',manifest),('unit_process_records',records)]:
 (OUT/f'{name}.json').write_text(json.dumps(data,indent=2))
 if data:
  keys=list(dict.fromkeys(k for r in data for k in r));
  with (OUT/f'{name}.csv').open('w',newline='') as f:
   w=csv.DictWriter(f,fieldnames=keys);w.writeheader();w.writerows(data)
print('LANES',len(rows),'TREES',len(stages),'MODULE ROWS',len(modules),'LOGGED LAUNCHES',sum(s['logged_launches'] for s in spawns))
for r in rows: print(r['lane'], {k:round(v,3) if isinstance(v,(int,float)) else v for k,v in r.items() if k!='lane'})
print('TOTALS',dict(sum((collections.Counter({k:r[k] for k in ['configure','build','test','post_test','self_host','evidence']}) for r in rows),collections.Counter())))
for lane in ['windows-x86_64-sanitized-debug','linux-x86_64-sanitized-debug','linux-aarch64-sanitized-debug','macos-aarch64-checks']:
 print('\n',lane,'MODULES')
 for m in sorted((r for r in modules if r['lane']==lane),key=lambda x:x['seconds'],reverse=True)[:8]: print(m['log'],m['module'],m['seconds'],m.get('assertions'))
 print('SPAWNS',dict(sum((collections.Counter({r['module']:r['logged_launches']}) for r in spawns if r['lane']==lane),collections.Counter())))
 for r in records:
  if r['lane']==lane and not r['tag'].startswith('CI_UNIT_PLAN'): print(r['tag'])
