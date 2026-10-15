#!/usr/bin/env python3
from pathlib import Path
import argparse, base64, hashlib, importlib.util, json, random, re
parser = argparse.ArgumentParser(description='Verify packed A64 storage against the preceding base64 projection')
parser.add_argument('--baseline', type=Path, required=True)
parser.add_argument('--candidate', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = args.candidate.resolve()
args.output.parent.mkdir(parents=True, exist_ok=True)
script=root/'src/buster/lib/compiler/assembly/tools/arm_a64_semantic_generate.py'
spec=importlib.util.spec_from_file_location('a64gen',script)
gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
def decode(text,offsets,width,size):
    out=bytearray()
    if offsets[0]!=0 or offsets[-1]!=len(text):raise ValueError('offset bounds')
    for first,last in zip(offsets,offsets[1:]):
        raw=base64.b64decode(text[first:last],validate=True)
        plane=bytearray();i=0
        while i<len(raw):
            tag=raw[i];i+=1
            if tag<128:
                n=tag+1
                if i+n>len(raw):raise ValueError('literal bounds')
                plane.extend(raw[i:i+n]);i+=n
            else:
                n=tag-128+3
                if i==len(raw):raise ValueError('run bounds')
                plane.extend([raw[i]]*n);i+=1
        block_size=min(size-len(out),width*32)
        if len(plane)!=block_size:raise ValueError('decoded bounds')
        rows=block_size//width
        out.extend(plane[col*rows+row] for row in range(rows) for col in range(width))
    if len(out)!=size:raise ValueError('total bounds')
    return bytes(out)
rng=random.Random(4201);cases=0
for width in [1,2,4,12,16,20,54]:
    for rows in [0,1,2,3,31,32,33,63,64,65,129]:
        n=width*rows
        patterns=[bytes(n),bytes([255])*n,bytes(i%256 for i in range(n)),bytes(rng.randrange(256) for _ in range(n)),bytes((i//130)%3 for i in range(n))]
        for data in patterns:
            text,offsets=gen.packed_blob(data,width)
            assert decode(text,offsets,width,n)==data,(width,rows)
            cases+=1
old=(args.baseline/'src/buster/lib/compiler/assembly/generated/arm-a64-semantic.generated.h').read_text()
new=(root/'src/buster/lib/compiler/assembly/generated/arm-a64-semantic.generated.h').read_text()
results=[]
for name in ['segment','field','operand','value_atom','value_entry']:
    stem='buster_a64_semantic_'+name+'_blob'
    oldtext=re.search(r'\b'+stem+r'\[\]\s*=\s*(.*?);',old,re.S)[1]
    data=base64.b64decode(''.join(re.findall(r'"([A-Za-z0-9+/=]*)"',oldtext)))
    text=re.search(r'\b'+stem+r'_data\[\]\s*=\s*(.*?);',new,re.S)[1]
    text=''.join(re.findall(r'"([A-Za-z0-9+/=]*)"',text))
    offsets=[int(x) for x in re.findall(r'(\d+)u',re.search(r'\b'+stem+r'_blocks\[\]\s*=\s*\{(.*?)\}',new,re.S)[1])]
    width=int(re.search(r'#define BUSTER_AARCH64_SEMANTIC_'+name.upper()+r'_RECORD_BYTES (\d+)u',new)[1])
    restored=decode(text,offsets,width,len(data))
    assert restored==data,name
    results.append(dict(name=name,decoded_bytes=len(data),old_base64_bytes=len(base64.b64encode(data)),packed_base64_bytes=len(text),index_bytes=4*len(offsets),sha256=hashlib.sha256(data).hexdigest()))
g=root/'src/buster/lib/compiler/assembly/generated'
rows=[json.loads(x) for x in (g/'arm-a64-semantic.generated.jsonl').read_text().splitlines()]
report=json.loads((g/'arm-a64-semantic-report.json').read_text())
regen=args.output.with_suffix('.reemitted.h')
gen.generate_header(rows,report,regen)
assert regen.read_bytes()==(g/'arm-a64-semantic.generated.h').read_bytes()
regen.unlink()
manifest=json.loads((g/'arm-a64-semantic-manifest.json').read_text())
oldmanifest=json.loads((args.baseline/'src/buster/lib/compiler/assembly/generated/arm-a64-semantic-manifest.json').read_text())
for key in oldmanifest:
    if key!='artifacts':assert manifest[key]==oldmanifest[key],key
for kind in ['jsonl','report']:assert manifest['artifacts'][kind]==oldmanifest['artifacts'][kind]
for kind in ['header','jsonl','report']:
    artifact=manifest['artifacts'][kind]
    payload=(g/artifact['file']).read_bytes()
    assert len(payload)==artifact['bytes'],kind
    assert hashlib.sha256(payload).hexdigest()==artifact['sha256'],kind
result=dict(cases=cases,payloads=results,regeneration_identical=True,semantic_provenance_unchanged=True)
args.output.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
