#!/usr/bin/env python3
"""Bounded hosted controls; consumes the SHA-256-pinned prior experiment.
No rebuild, production modification, timing measurement, or unrecorded retry.
"""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
import zipfile

PIN = 'ade6ac4b6ecb21f30b61b656439bac476c145e2f'
TREE = '4c5306221fdb22fccc929b55e333163742de17d0'
ARCHIVE_SHA = '55c94630a63814316801f22232123c841d1292e2f454990a5429405c8fee204a'
root = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
assert subprocess.check_output(['git','rev-parse','HEAD','HEAD^{tree}'],cwd=root,text=True).splitlines() == [PIN,TREE]
(out/'production.txt').write_text(PIN+'\n'+TREE+'\n')

# Do not forward the API credential to the artifact storage redirect.
class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None
request = urllib.request.Request(
    'https://api.github.com/repos/buster14a/buster/actions/artifacts/10893718733/zip',
    headers={'Authorization':'Bearer '+os.environ['GITHUB_TOKEN'],
             'Accept':'application/vnd.github+json'})
try:
    response = urllib.request.build_opener(NoRedirect).open(request,timeout=60)
except urllib.error.HTTPError as error:
    if error.code not in (301,302,303,307,308):
        raise
    location = error.headers['Location']
    if not location.startswith('https://'):
        raise RuntimeError('Non-HTTPS artifact redirect')
    response = urllib.request.urlopen(location,timeout=60)
archive = out/'prior.zip'
with response, archive.open('wb') as stream:
    shutil.copyfileobj(response,stream)
assert hashlib.sha256(archive.read_bytes()).hexdigest() == ARCHIVE_SHA
prior = out/'prior'
with zipfile.ZipFile(archive) as bundle:
    for name in bundle.namelist():
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts:
            raise RuntimeError('Unsafe archive member')
    bundle.extractall(prior)
for line in (prior/'SHA256SUMS').read_text().splitlines():
    expected,name = line.split('  ',1)
    assert hashlib.sha256((prior/name).read_bytes()).hexdigest() == expected,name
for name in ('base-ide','probe-ide'):
    (prior/name).chmod(0o755)
(root/'build').mkdir(exist_ok=True)
shutil.copytree(prior/'generated',root/'build/generated')
(out/'frozen-compilers.json').write_text((prior/'compilers.json').read_text())

common = ['cc','-target','x86_64-linux','-std=gnu17','-fwrapv',
          '-fno-strict-aliasing','-funsigned-char','-g0','-O0',
          '-fverify-codegen','-fregister-allocator=fast']
env = dict(os.environ,BUSTER_ALLOCATION_CENSUS='1')
env.pop('GITHUB_TOKEN',None)
records = []
def run(argv: list[str], name: str) -> tuple[int,Path]:
    stem = out/'runs'/name
    stem.parent.mkdir(parents=True,exist_ok=True)
    with Path(str(stem)+'.stdout').open('wb') as stdout, Path(str(stem)+'.stderr').open('wb') as stderr:
        try:
            status = subprocess.run(argv,cwd=root,env=env,stdout=stdout,stderr=stderr,timeout=240,check=False).returncode
        except subprocess.TimeoutExpired:
            status = 124
    record = {'name':name,'argv':argv,'cwd':str(root),'returncode':status}
    records.append(record)
    Path(str(stem)+'.json').write_text(json.dumps(record,indent=2)+'\n')
    print(name,status,flush=True)
    return status,Path(str(stem)+'.stderr')

def count(stderr: Path) -> dict:
    text = stderr.read_text(errors='replace')
    totals = [line.split('\t') for line in text.splitlines() if line.startswith('BUSTER_ALLOC_TOTAL_V2\t') and '\tarena\t' in line]
    if len(totals)!=1 or 'BUSTER_ALLOC_DONE_V2' not in text:
        raise RuntimeError('Incomplete allocation census')
    fields = totals[0]
    trace = [tuple(map(int,m)) for m in re.findall(r'^SCRATCH_WATERMARK (\d+) (\d+) (\d+) (\d+) (\d+)$',text,re.M)]
    written_sites=[]
    for line in text.splitlines():
        row=line.split('\t')
        if len(row)==16 and row[0]=='BUSTER_ALLOC_V2' and row[2]=='arena' and int(row[10]):
            written_sites.append({'file':row[3],'line':int(row[4]),'function':row[5],
                                  'calls':int(row[6]),'zero_written':int(row[10])})
    return {'zero_written':int(fields[7]),'trace_queries':len(trace),
            'trace_dirty_raises':sum(end>before for before,start,end,capacity,requested in trace),
            'trace_requested_bytes':sum(requested for before,start,end,capacity,requested in trace),
            'written_sites':written_sites}

# Same source path and output path on both legs; remove only the semantic queries.
controls=[]
for original in sorted((prior/'inputs').glob('*.c')):
    name=original.stem
    path=out/'same-input.c'
    original_text=original.read_text()
    control_text,n=re.subn(r'return \(.*\) != (\d+);',r'return \1 != \1;',original_text)
    assert n==1
    outcomes={}
    output=out/'same-output.o'
    for label,text in [('query',original_text),('query-free',control_text)]:
        path.write_text(text)
        output.unlink(missing_ok=True)
        status,stderr=run([str(prior/'probe-ide'),*common,'-fno-machine-fallback',str(path),'-c','-o',str(output)],name+'/'+label)
        outcomes[label]={'status':status,'counts':count(stderr),
                         'object_sha256':hashlib.sha256(output.read_bytes()).hexdigest() if status==0 else None,
                         'input_sha256':hashlib.sha256(text.encode()).hexdigest()}
        saved=out/'inputs'/(name+'-'+label+'.c')
        saved.parent.mkdir(exist_ok=True)
        saved.write_text(text)
    row={'subject':name,'outcomes':outcomes,
         'identical_objects':outcomes['query']['status']==outcomes['query-free']['status']==0 and outcomes['query']['object_sha256']==outcomes['query-free']['object_sha256']}
    controls.append(row)
    (out/'query-controls.json').write_text(json.dumps(controls,indent=2)+'\n')

# The previous strict self-compile remains a refusal, not a passing result.
# This is a separately named, supported default-fallback control, not a retry
# of that strict gate. Required validation is retained.
self_results={}
output=out/'same-self-output.o'
for label in ('base','probe'):
    output.unlink(missing_ok=True)
    status,stderr=run([str(prior/(label+'-ide')),*common,'-Isrc','-Ibuild/generated',
        '-DBUSTER_UNITY_BUILD=1','-DBUSTER_INCLUDE_TESTS=0','src/buster/apps/ide/ide.c',
        '-c','-o',str(output)],'self-supported/'+label)
    self_results[label]={'status':status,'counts':count(stderr),
                        'object_sha256':hashlib.sha256(output.read_bytes()).hexdigest() if status==0 else None}
    if status==0:
        shutil.copy2(output,out/(label+'-self.o'))
(out/'self-supported.json').write_text(json.dumps(self_results,indent=2)+'\n')
(out/'commands.json').write_text(json.dumps(records,indent=2)+'\n')
self_equal=self_results['base']['status']==self_results['probe']['status']==0 and self_results['base']['object_sha256']==self_results['probe']['object_sha256']
(out/'outcome.json').write_text(json.dumps({
    'prior_run':36205927896,'prior_artifact':10893718733,'prior_archive_sha256':ARCHIVE_SHA,
    'query_control_pairs':len(controls),'query_control_identical':sum(row['identical_objects'] for row in controls),
    'self_supported_identical':self_equal,
    'limits':'No performance measurements, optimization prototype, full suite, sanitizer, platform matrix or self-host fixed point. The earlier strict self-compile refusal is not superseded.'},indent=2)+'\n')
if not self_equal or not all(row['identical_objects'] for row in controls):
    raise RuntimeError('An equivalence control failed; raw outcomes are retained')
