import hashlib,json,pathlib,statistics,subprocess,time,os
root=pathlib.Path.cwd();work=root.parent;inputs=work/'archive-e2e'
compilers=[work/'ide-baseline',root/'build/Release/ide']
rows=[]
for shape in ['reverse','forward','irrelevant','single']:
    archive=inputs/('reverse.a' if shape in ['reverse','irrelevant'] else 'forward.a')
    source=inputs/('root.c' if shape in ['reverse','forward'] else 'root-last.c')
    obj=source.with_suffix('.o')
    if shape in ['irrelevant','single']:
        source.write_text('extern int chain_255(void); int main(void) {return chain_255()!=1;}\n')
        subprocess.run(['clang','-O0','-c',str(source),'-o',str(obj)],check=True)
    if shape=='single':
        archive=inputs/'single.a'
        subprocess.run(['ar','rcs',str(archive),str(inputs/'member_255.o')],check=True)
    expected=None
    for repeat in range(15):
        for method in ([0,1] if repeat%2==0 else [1,0]):
            output=inputs/f'{shape}-{method}.exe';mem=inputs/'memory.txt'
            command=[str(compilers[method]),'cc',str(obj),str(archive),'-o',str(output)]
            start=time.perf_counter_ns()
            with mem.open('wb') as log:
                result=subprocess.Popen(command,stdout=log,stderr=log)
                _,status,usage=os.wait4(result.pid,0)
                result.returncode=os.waitstatus_to_exitcode(status)
            elapsed=time.perf_counter_ns()-start
            if result.returncode:
                raise RuntimeError(mem.read_text(errors='replace'))
            digest=hashlib.sha256(output.read_bytes()).hexdigest()
            if expected is None: expected=digest
            assert digest==expected,(shape,repeat,method,digest,expected)
            run=subprocess.run([str(output)],capture_output=True)
            assert run.returncode==0,(shape,method,run.returncode)
            rows.append(dict(shape=shape,repeat=repeat,method=['baseline','indexed'][method],elapsed_ns=elapsed,
                             maxrss_kib=usage.ru_maxrss,cpu_ns=round((usage.ru_utime+usage.ru_stime)*1e9),artifact_sha256=digest))
provenance=dict(base='719ba64e3022a0ae1db078ea97cb951a70ac02a0',compiler_sha256=[hashlib.sha256(c.read_bytes()).hexdigest() for c in compilers],rows=rows)
(work/'archive-e2e.json').write_text(json.dumps(provenance,indent=2)+'\n')
for shape in ['reverse','forward','irrelevant','single']:
    for method in ['baseline','indexed']:
        data=[r for r in rows if r['shape']==shape and r['method']==method]
        print(shape,method,'median_ms',statistics.median(r['elapsed_ns'] for r in data)/1e6,'median_cpu_ms',statistics.median(r['cpu_ns'] for r in data)/1e6,'median_rss_kib',statistics.median(r['maxrss_kib'] for r in data))
print('All 120 artifacts matched and executed successfully.')
