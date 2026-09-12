import csv, hashlib, json, os, pathlib, re, statistics, subprocess, time
import sys
root = pathlib.Path(sys.argv[3]).resolve()
root.mkdir(parents=True, exist_ok=True)
base = pathlib.Path(sys.argv[1]).resolve()
patch = pathlib.Path(sys.argv[2]).resolve()
rows=[]
for n in (0, 8191, 8192, 8193, 12000):
    work=root/str(n)
    (work/'tests').mkdir(parents=True,exist_ok=True)
    source=''.join(f'struct Tag{i} {{ int value; }}; typedef const struct Tag{i} Alias{i};\n' for i in range(n))
    source+='int example(int x) { return x + 1; }\n'
    path=work/'tests/basic_c_operations.c'
    path.write_text(source)
    for pair in range(4):
        for variant in (('base','patch') if pair%2==0 else ('patch','base')):
            exe = base if variant=='base' else patch
            log=work/f'{pair}-{variant}.log'
            start=time.monotonic_ns()
            with log.open('w') as output:
                child=subprocess.Popen([str(exe),'bench'],cwd=work,stdout=output,stderr=subprocess.STDOUT)
                _,status,usage=os.wait4(child.pid,0)
                child.returncode=os.waitstatus_to_exitcode(status)
            wall=time.monotonic_ns()-start
            text=log.read_text()
            match=re.search(r'BENCH_C_FRONTEND .*bytes=(\d+) min_ns=(\d+) median_ns=(\d+)',text)
            if child.returncode or not match:
                raise RuntimeError(f'{variant} {n}: {text}')
            rows.append(dict(tags=n,pair=pair,variant=variant,bytes=int(match[1]),frontend_min_ns=int(match[2]),frontend_median_ns=int(match[3]),wall_ns=wall,user_s=usage.ru_utime,system_s=usage.ru_stime,max_rss_kib=usage.ru_maxrss,source_sha256=hashlib.sha256(source.encode()).hexdigest()))
            with (root/'samples.csv').open('w') as out:
                writer=csv.DictWriter(out,fieldnames=rows[0].keys());writer.writeheader();writer.writerows(rows)
    a=[x for x in rows if x['tags']==n]
    print(json.dumps({variant:{k:statistics.median(x[k] for x in a if x['variant']==variant) for k in ('frontend_median_ns','max_rss_kib')} for variant in ('base','patch')}),flush=True)
