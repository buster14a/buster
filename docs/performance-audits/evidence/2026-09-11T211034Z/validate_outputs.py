import hashlib,json,pathlib,subprocess,sys
root=pathlib.Path('/workspace/scratch/938e5009f152/issue40')
production=pathlib.Path('/workspace/scratch/938e5009f152/issue40-production/build/Release/ide')
tested=root/'build/Release/ide'
out=root/'build/fast-production-output-validation'
out.mkdir(exist_ok=False)
metadata=json.load(open(root/'build/fast-throughput-optimized-on-off/metadata.json'))
records=[]
for job in metadata['jobs']:
    source=pathlib.Path(job['source'])
    assert hashlib.sha256(source.read_bytes()).hexdigest()==job['sha256']
    for flag in ['-fno-canonical-fast','-fcanonical-fast']:
        tag='off' if flag.startswith('-fno') else 'all'
        pair=[]
        for variant,compiler in [('tested',tested),('production',production)]:
            prefix=out/f"{job['job']:02}-{job['name']}-{job['mode']}-{tag}-{variant}"
            artifact=prefix.with_suffix('.o');metrics=prefix.with_suffix('.metrics');log=prefix.with_suffix('.log')
            command=[str(compiler),'cc','-g0','-O0',f'-fsource-metrics={metrics}','-c',f"-fregister-allocator={job['mode']}",str(source),'-o',str(artifact),flag]
            run=subprocess.run(command,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            log.write_bytes(run.stdout)
            record={'job':job['job'],'name':job['name'],'mode':job['mode'],'mask':tag,'variant':variant,'command':command,'cwd':str(root),'exit':run.returncode,'log':str(log),'metrics':str(metrics)}
            if artifact.exists():
                data=artifact.read_bytes();record.update(bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
            pair.append(record);records.append(record)
            (out/'results.json').write_text(json.dumps(records,indent=2)+'\n')
            assert run.returncode==0,record
        assert pair[0]['sha256']==pair[1]['sha256'],pair
        print(f"MATCH {job['name']}/{job['mode']}/{tag} {pair[0]['sha256']}",flush=True)
provenance={'tested':{'path':str(tested),'sha256':hashlib.sha256(tested.read_bytes()).hexdigest(),'bytes':tested.stat().st_size},'production':{'path':str(production),'sha256':hashlib.sha256(production.read_bytes()).hexdigest(),'bytes':production.stat().st_size},'cases':len(metadata['jobs']),'masks':2,'compilations':len(records),'matching_pairs':len(records)//2}
(out/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
print(json.dumps(provenance,indent=2))
