"""Place matching timestamps around real cold snapshot creation in the C/link drivers."""
import sys
from pathlib import Path
from statistics import median
import os
import random
import re
import shutil
import subprocess

if sys.argv[1] == 'instrument':
    src = Path('src/buster/compiler/snapshot.c')
    text = src.read_text()
    first = '    Arena arena = named ? arena_reserve(BUSTER_SNAPSHOT_WRITE_BYTES)'
    image = '    const uint8_t* image = arena.base != 0 ? snapshot_image(runtime, compiler, &arena, &bytes) : 0;'
    last = '    arena_release(&arena);\n    return written;'
    for site in (first, image, last):
        assert text.count(site)==1,site
    text = text.replace(first, '    const uint64_t write_start = executable_now();\n'+first)
    text = text.replace(image,'    const uint64_t image_start = executable_now();\n'+image+'\n    const uint64_t image_end = executable_now();')
    text = text.replace(last, '''    arena_release(&arena);
    const uint64_t write_end = executable_now();
    fprintf(stderr, "PHASE kind=%s bytes=%llu image_ns=%llu write_ns=%llu\\n", snapshot_name(runtime->kind),
            (unsigned long long)bytes, (unsigned long long)(image_end - image_start), (unsigned long long)(write_end - write_start));
    return written;''')
    src.write_text(text)
    sys.exit(0)

root = Path(sys.argv[2])
cpu = min(os.sched_getaffinity(0))
os.sched_setaffinity(0, {cpu})
print('PHASE_PLACEMENT cpu',cpu)
def pctile(vals,f):
    return sorted(vals)[round(f*(len(vals)-1))]
for kind, flags in [('dynamic',[]),('static',['-static']),('static-pie',['-static-pie']),('no-pie',['-no-pie']),('shared',[])]:
    data = {part:{v:[] for v in ('base','head')} for part in ('image','write')}
    order = [i&1 for i in range(128)]
    random.Random(3046).shuffle(order)
    for n,flip in enumerate([0,1,1,0]+order):
        for v in (('head','base') if flip else ('base','head')):
            cache = root/('cache-'+v)
            cache.mkdir(exist_ok=True)
            if (cache/'buster').exists():
                shutil.rmtree(cache/'buster')
            cmd = [str(root/v)]
            if kind == 'shared':
                cmd += ['benchmark-link','--only','direct','--kinds','shared','--functions','1','--runs','1',
                        '--cpus',str(cpu),'--cache',str(cache/'buster'),'--directory',str(root)]
            else:
                cmd += [*flags,'tools/ci/qualification.c','-o',str(root/'qualification')]
            p = subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=dict(os.environ,XDG_CACHE_HOME=str(cache)))
            rows = re.findall(r'(?m)^PHASE kind=(\S+) bytes=(\d+) image_ns=(\d+) write_ns=(\d+)$',p.stderr)
            if p.returncode or len(rows)!=1:
                raise RuntimeError((kind,v,p.returncode,p.stdout[-1300:],p.stderr[-1300:]))
            name,size,image,total=rows[0]
            assert (cache/'buster'/name).stat().st_size==int(size)
            if n>=4:
                data['image'][v].append(int(image))
                data['write'][v].append(int(total))
    for part,by in data.items():
        a,b=by['base'],by['head']
        ds=[100.0*(x-y)/x for x,y in zip(a,b)]
        rng=random.Random(3046)
        ci=[median(rng.choices(ds,k=len(ds))) for i in range(4000)]
        print('PHASE_SAMPLES',kind,part,'base_ns',','.join(map(str,a)),'head_ns',','.join(map(str,b)),flush=True)
        print('PHASE_TIME',kind,part,'pairs',len(a),'base_us',round(median(a)/1000,2),'head_us',round(median(b)/1000,2),
              'paired_gain_pct',round(median(ds),2),'boot95',round(pctile(ci,.025),2),round(pctile(ci,.975),2),flush=True)
