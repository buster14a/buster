import sys, re
annot, src, start_pat, end_pat = sys.argv[1:5]
text = open(annot).read().split('\n')
# locate c_gen.c section
i = next(k for k, l in enumerate(text) if l.startswith('-- Auto-annotated source: src/buster/lib/compiler/frontend/c/c_gen.c'))
src_lines = open(src).read().split('\n')
start = next(k for k, l in enumerate(src_lines) if start_pat in l)
end = next(k for k, l in enumerate(src_lines) if k > start and end_pat in l)
# Walk annotated lines: each source line appears once in order; call lines start with "=>" after the count.
line_no = -1; self_ir = 0; call_ir = 0; calls = {}
for l in text[i+1:]:
    if l.startswith('-- Auto-annotated source:') or l.startswith('--------------------------------------------------------------------------------'):
        if line_no > 0: pass
    m = re.match(r'^\s*([\d,]+|\.)\s(.*)$', l)
    if not m: 
        if l.startswith('-- '): 
            if 'Auto-annotated' in l: break
        continue
    count, rest = m.groups()
    if rest.lstrip().startswith('=>'):
        if start <= line_no < end and count != '.':
            v = int(count.replace(',', '')); call_ir += v
            callee = rest.strip().split()[1]; calls[callee] = calls.get(callee, 0) + v
        continue
    line_no += 1
    if start <= line_no < end and count != '.':
        self_ir += int(count.replace(',', ''))
print(f'lines {start+1}-{end}: self_ir={self_ir:,} call_ir={call_ir:,} total={self_ir+call_ir:,}')
for k, v in sorted(calls.items(), key=lambda kv: -kv[1])[:6]: print('   call', k, f'{v:,}')
