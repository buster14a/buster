import subprocess, sys, os, collections
sp, base, cand = sys.argv[1:4]
os.chdir(os.path.join(sp, 'frozen'))
out = os.path.join(sp, 'corpus-census'); os.makedirs(out, exist_ok=True)
tot = {'base': collections.Counter(), 'cand': collections.Counter()}
per = []
files = sorted(f for f in os.listdir('tests') if f.endswith('.c'))
ok = 0
for f in files:
    row = {}
    for tag, exe in (('base', base), ('cand', cand)):
        m = os.path.join(out, f'{f}.{tag}.metrics')
        r = subprocess.run([exe, 'cc', '-c', '-g0', '-Isrc', '-Itests', f'-fsource-metrics={m}', 'tests/' + f, '-o', os.path.join(out, f'{f}.{tag}.o')], capture_output=True)
        c = collections.Counter()
        if r.returncode == 0 and os.path.exists(m):
            for line in open(m):
                if line.startswith('ir_construction.ssa_simplify_') or line.startswith('ir_construction.ssa_pending_visits'):
                    k, v = line.strip().split('='); c[k.split('.',1)[1]] = int(v)
        row[tag] = (r.returncode, c)
    if row['base'][0] == 0 and row['cand'][0] == 0:
        ok += 1
        for tag in ('base', 'cand'): tot[tag].update(row[tag][1])
        per.append((f, row['base'][1]['ssa_simplify_parameter_visits'], row['cand'][1]['ssa_simplify_parameter_visits'], row['base'][1]['ssa_pending_visits']))
print(f'files={len(files)} compiled_by_both={ok}')
for k in sorted(set(tot['base']) | set(tot['cand'])):
    print(f"{k:40s} base={tot['base'][k]:>10d} cand={tot['cand'][k]:>10d}")
changed = [p for p in per if p[1] != p[2]]
print(f'files_with_fewer_evaluations={len(changed)} of {len(per)}')
per.sort(key=lambda p: p[1]-p[2], reverse=True)
for p in per[:8]: print('top', p)
