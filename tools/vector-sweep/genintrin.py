# WIP (#1834), not for merging (see gen.py). Intrinsic wrappers: one per header function of clang's immintrin.h and
# x86intrin.h (called with its own parameters) and per function-like macro (vector parameters by their casts' types, the
# rest the constant 1). Needs hdr.i and macros.txt in the current directory:
#   printf '#include <immintrin.h>\n#include <x86intrin.h>\n' > hdr.c
#   clang -E -P -march=x86-64 hdr.c -o hdr.i
#   clang -dM -E -march=x86-64 hdr.c | grep -E '^#define _(mm|m_|bit|rd|wr|cvt|load|store|tz|lz|bextr|blsi|pdep|pext|popcnt|xbegin|xend)[a-zA-Z0-9_]*\(' > macros.txt
# Usage: python3 -I genintrin.py intrin.txt   (6944 wrappers).
# Intrinsic wrappers: one per header function (called with its own parameters) and per function-like macro (vector
# parameters by their casts' types, the rest the constant 1). Writes NAME<TAB>code lines to the output.
import re, sys
text = open('hdr.i').read()
out = open(sys.argv[1], 'w')
def strip_attributes(s):
    r = ''
    i = 0
    while i < len(s):
        if s.startswith('__attribute__', i):
            j = s.index('(', i)
            depth = 0
            k = j
            while True:
                if s[k] == '(':
                    depth += 1
                elif s[k] == ')':
                    depth -= 1
                    if depth == 0:
                        break
                k += 1
            i = k + 1
        else:
            r += s[i]
            i += 1
    return r
seen = set()
count = 0
for m in re.finditer(r'static __inline__?\s', text):
    start = m.end()
    brace = text.find('{', start)
    semi = text.find(';', start)
    if brace < 0 or (0 <= semi < brace):
        continue
    decl = " ".join(strip_attributes(text[start:brace]).split())
    decl = re.sub(r'\b(__inline__|__inline|inline|extern|static)\b', '', decl)
    pm = re.match(r'^(.*?)\b([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)\s*$', decl, re.S)
    if not pm:
        continue
    ret, name, params = pm.group(1).strip(), pm.group(2), pm.group(3).strip()
    if not re.match(r'^_(mm|m_|bit|rd|wr|cvt|load|store|tz|lz|bextr|blsi|blsr|blsmsk|pdep|pext|popcnt|bzhi|andn|mulx|xbegin|xend|xabort|xtest|castu|cast)', name) or name in seen:
        continue
    seen.add(name)
    names = []
    if params not in ('', 'void'):
        for p in params.split(','):
            pn = re.findall(r'[A-Za-z_][A-Za-z0-9_]*', p)
            names.append(pn[-1] if pn else 'x')
    call = f"{name}({','.join(names)})"
    body = f"return {call};" if ret.replace(' ', '') != 'void' else f"{call};"
    params_text = params if params else 'void'
    out.write(f"F_{name}\t{ret} g({params_text}){{{body}}}\n")
    count += 1
vector = re.compile(r'^(__m\d+[a-z]*|__v\d+[a-z]+|__m\d+[a-z]*_u|__mmask\d+|__m64)$')
for line in open('macros.txt'):
    mm = re.match(r'^#define ([A-Za-z_][A-Za-z0-9_]*)\(([^)]*)\) (.*)$', line.rstrip('\n'))
    if not mm:
        continue
    name, plist, body = mm.group(1), [p.strip() for p in mm.group(2).split(',') if p.strip()], mm.group(3)
    if name in seen:
        continue
    seen.add(name)
    params = []
    args = []
    for k, p in enumerate(plist):
        casts = re.findall(r'\(\s*([A-Za-z_][A-Za-z0-9_ ]*\**)\s*\)\s*\(\s*' + re.escape(p) + r'\s*\)', body)
        t = casts[0].strip() if casts else None
        if t and vector.match(t) and not t.startswith('__mmask'):
            params.append(f"{t} p{k}")
            args.append(f"p{k}")
        elif t and t.endswith('*'):
            params.append(f"void* p{k}")
            args.append(f"p{k}")
        else:
            args.append('1')
    call = f"{name}({','.join(args)})"
    out.write(f"M_{name}\t__typeof__({call}) g({','.join(params) if params else 'void'}){{return {call};}}\n")
    count += 1
print(count)
