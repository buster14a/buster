import subprocess,hashlib,json
from pathlib import Path
out=Path('../evidence')
rows=[]
for mode in ('executable-g','object-g','object-g0'):
    flags=['-Isrc','-Ibuild/generated','-DBUSTER_UNITY_BUILD=1','-DBUSTER_INCLUDE_TESTS=0', '-g0' if mode=='object-g0' else '-g']
    if mode!='executable-g': flags+=['-c']
    flags+=['src/buster/apps/ide/ide.c']
    if mode=='executable-g': flags+=['-lm']
    flags+=['-o',str(out/'identity-output')]
    digests=[]
    for arm in ('base','cand'):
        p=subprocess.run([str(Path('../bin')/('ide-'+arm)),'cc']+flags,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        assert p.returncode==0,(mode,arm,p.stderr.decode())
        artifact=out/'identity-output'
        assert artifact.stat().st_size>0
        digests.append(hashlib.sha256(artifact.read_bytes()).hexdigest())
        artifact.rename(out/(mode+'-'+arm))
    assert digests[0]==digests[1],(mode,digests)
    rows.append({'mode':mode,'sha256':digests[0],'bytes':(out/(mode+'-base')).stat().st_size,'identical':True})
(out/'artifact-identity.json').write_text(json.dumps(rows,indent=2)+'\n')
print(json.dumps(rows,indent=2))
