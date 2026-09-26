#!/usr/bin/env python3
# Disposable diagnostic runner: callgrind cache simulation over a fixed job list.
# Each job: (label, binary, cwd, args, ll_bytes). Writes raw callgrind output,
# stdout/stderr, the compiled artifact hash and extracted counters to JSON lines.
import json, os, re, subprocess, sys, hashlib
from concurrent.futures import ThreadPoolExecutor
S = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(S, 'cg')
os.makedirs(OUT, exist_ok=True)
FUNCS = ['ir_prepare_canonical_module', 'ir_validate_canonical_module', 'codegen_generate_canonical_module_with_trace',
         'c_lower_to_ir_with_options', 'compiler_driver_execute_c_single']

def inclusive(path):
    out = subprocess.run(['callgrind_annotate', '--inclusive=yes', '--show=Ir,DLmr,DLmw', '--threshold=100', path],
                         capture_output=True, text=True).stdout
    res = {}
    for l in out.split('\n'):
        m = re.match(r'\s*([\d,]+)\s+\([^)]*\)\s+([\d,]+|\.)\s+(?:\([^)]*\))?\s*([\d,]+|\.)\s+(?:\([^)]*\))?\s+(\S+):(\S+)', l)
        if m and m.group(5) in FUNCS and m.group(5) not in res:
            res[m.group(5)] = [int(x.replace(',', '')) if x != '.' else 0 for x in m.group(1, 2, 3)]
    return res

def run(job):
    label, binary, cwd, args, ll = job
    stem = os.path.join(OUT, label)
    if os.path.exists(stem + '.json'):
        return json.load(open(stem + '.json'))
    try:
        os.close(os.open(stem + '.lock', os.O_CREAT | os.O_EXCL | os.O_WRONLY))
    except FileExistsError:
        return {'label': label, 'rc': 'locked', 'totals': [], 'inclusive': {}, 'artifact_sha256': None}
    artifact = stem + '.artifact'
    argv = ['valgrind', '--tool=callgrind', '--cache-sim=yes', '--D1=49152,12,64', '--I1=32768,8,64',
            f'--LL={ll},16,64', f'--callgrind-out-file={stem}.cgout', binary] + [a.replace('@OUT@', artifact) for a in args]
    with open(stem + '.log', 'w') as log:
        rc = subprocess.run(argv, cwd=cwd, stdout=log, stderr=subprocess.STDOUT).returncode
    text = open(stem + '.log', errors='replace').read()
    m = re.search(r'Collected : ([\d ]+)', text)
    totals = [int(x) for x in m.group(1).split()] if m else []
    h = hashlib.sha256(open(artifact, 'rb').read()).hexdigest() if os.path.exists(artifact) else None
    record = {'label': label, 'binary': binary, 'binary_sha256': hashlib.sha256(open(binary, 'rb').read()).hexdigest(),
              'cwd': cwd, 'argv': argv, 'rc': rc, 'll': ll,
              'events': 'Ir Dr Dw I1mr D1mr D1mw ILmr DLmr DLmw', 'totals': totals,
              'artifact_sha256': h, 'inclusive': inclusive(stem + '.cgout') if rc == 0 else {}}
    json.dump(record, open(stem + '.json', 'w'), indent=1)
    return record

if __name__ == '__main__':
    jobs = json.load(open(sys.argv[1]))
    with ThreadPoolExecutor(int(sys.argv[2]) if len(sys.argv) > 2 else 4) as pool:
        for r in pool.map(run, jobs):
            p = r['inclusive'].get('ir_prepare_canonical_module', [0, 0, 0])
            t = r['totals']
            print(f"{r['label']:48s} rc={r['rc']} Ir={t[0] if t else 0:>14,} DLmr={t[7] if t else 0:>11,} prepIr={p[0]:>13,} prepDLmr={p[1]:>11,} out={str(r['artifact_sha256'])[:12]}", flush=True)
