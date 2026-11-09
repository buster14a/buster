# WIP (#1834), a transfer vehicle for the cloud session, not for merging: AGENTS.md wants harnesses in
# buster-performance and tools in C. Generates the generic vector sweep: one C source per case (operators,
# __builtin_elementwise_*, __builtin_reduce_*, shuffles, conversions, nontemporal, masked memory, scalar elementwise).
# Usage: python3 -I gen.py CASEDIR   (3389 cases; clang 23.1.5 -O0 -c accepts 3198 of them at each of x86-64,
# x86-64-v2, x86-64-v3 and native).
# Sweep generator: one C source per case, compiled by clang -O0 and by buster at each -march.
import os, sys, itertools
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
E = [('c','signed char',1,'i'),('uc','unsigned char',1,'u'),('s','short',2,'i'),('us','unsigned short',2,'u'),
     ('i','int',4,'i'),('ui','unsigned int',4,'u'),('l','long long',8,'i'),('ul','unsigned long long',8,'u'),
     ('f','float',4,'f'),('d','double',8,'f')]
SIGNED = {1:'signed char',2:'short',4:'int',8:'long long'}
cases = []
def vt(name, ctype, width):
    return f"typedef {ctype} {name} __attribute__((vector_size({width})));"
def ext(name, ctype, lanes):
    return f"typedef {ctype} {name} __attribute__((ext_vector_type({lanes})));"
def add(name, src):
    cases.append((name, src))
for (en, ct, sz, kind) in E:
    for W in (4, 8, 16, 32, 64):
        L = W // sz
        if L < 2: continue
        V = f"V"
        head = vt('V', ct, W) + vt('M', SIGNED[sz], W) + ext('X', ct, L) + "\n"
        tag = f"{en}{W}"
        def fn(op, body, ret='V', params='V a,V b,V c,int n'):
            add(f"{op}.{tag}", head + f"{ret} f({params}){{{body}}}\n")
        integer = kind != 'f'
        bits = sz * 8
        # operators
        fn('neg', 'return -a;')
        fn('add', 'return a+b;'); fn('sub', 'return a-b;'); fn('mul', 'return a*b;'); fn('div', 'return a/b;')
        fn('inc', 'V r=a;++r;return r;'); fn('postdec', 'V r=a;r--;return r;')
        fn('addsplat', 'return a+(' + ct + ')n;')
        for o, s in (('lt','<'),('le','<='),('gt','>'),('ge','>='),('eq','=='),('ne','!=')):
            fn('cmp'+o, f'return a{s}b;', ret='M')
        fn('cond', 'return a?b:c;', ret='X', params='X a,X b,X c,int n')
        fn('lnot', 'return !a;', ret='__typeof__(!(X){0})', params='X a,X b,X c,int n')
        fn('land', 'return a&&b;', ret='__typeof__(!(X){0})', params='X a,X b,X c,int n')
        if integer:
            fn('rem', 'return a%b;'); fn('and', 'return a&b;'); fn('or', 'return a|b;'); fn('xor', 'return a^b;'); fn('not', 'return ~a;')
            fn('shl', f'return a<<(b&{bits-1});'); fn('shr', f'return a>>(b&{bits-1});')
            fn('shlc', 'return a<<3;'); fn('shrc', 'return a>>3;'); fn('shln', 'return a<<n;'); fn('shrn', 'return a>>n;')
            for b in ('abs','popcount','bitreverse','clzg','ctzg'):
                fn('ew_'+b, f'return __builtin_elementwise_{b}(a);')
            for b in ('clzg','ctzg'):
                fn('ew_'+b+'2', f'return __builtin_elementwise_{b}(a,b);')
            for b in ('max','min','add_sat','sub_sat'):
                fn('ew_'+b, f'return __builtin_elementwise_{b}(a,b);')
            for b in ('fshl','fshr'):
                fn('ew_'+b, f'return __builtin_elementwise_{b}(a,b,c);')
            for b in ('add','mul','and','or','xor','max','min'):
                fn('red_'+b, f'return __builtin_reduce_{b}(a);', ret=ct)
        else:
            for b in ('abs','sqrt','ceil','floor','trunc','round','rint','nearbyint','roundeven','canonicalize',
                      'exp','exp2','exp10','log','log2','log10','sin','cos','tan','asin','acos','atan','sinh','cosh','tanh'):
                fn('ew_'+b, f'return __builtin_elementwise_{b}(a);')
            for b in ('max','min','maximum','minimum','maxnum','minnum','maximumnum','minimumnum','copysign','pow','fmod','atan2'):
                fn('ew_'+b, f'return __builtin_elementwise_{b}(a,b);')
            fn('ew_fma', 'return __builtin_elementwise_fma(a,b,c);')
            fn('ew_ldexp', 'return __builtin_elementwise_ldexp(a,(M)n);')
            for b in ('max','min','maximum','minimum','assoc_fadd','in_order_fadd'):
                extra = ',(' + ct + ')0' if b in ('assoc_fadd','in_order_fadd') else ''
                fn('red_'+b, f'return __builtin_reduce_{b}(a{extra});', ret=ct)
        fn('ntload', 'return __builtin_nontemporal_load((V*)&a);')
        fn('ntstore', '__builtin_nontemporal_store(b,(V*)&a);return a;')
        # shuffles
        idx = list(range(L))
        pats = {
            'rev': [L-1-i for i in idx],
            'bcast': [0]*L,
            'bcast1': [1]*L,
            'unpacklo': [ (i//2) + (L if i%2 else 0) for i in idx],
            'unpackhi': [ L//2 + (i//2) + (L if i%2 else 0) for i in idx],
            'even': [ (2*i) % (2*L) for i in idx],
            'odd': [ (2*i+1) % (2*L) for i in idx],
            'rot1': [ (i+1) % L for i in idx],
            'alignr': [ i+1 for i in idx],
            'mix': [ ((i*7+3) % (2*L)) for i in idx],
            'mixu': [ (-1 if i%3==2 else (i*5+1) % (2*L)) for i in idx],
            'swappairs': [ i^1 for i in idx],
        }
        for p, ix in pats.items():
            fn('shuf_'+p, 'return __builtin_shufflevector(a,b,' + ','.join(map(str,ix)) + ');')
        if 2*W <= 64:
            add(f"shuf_concat.{tag}", head + vt('W', ct, 2*W) + "W f(V a,V b,V c,int n){return __builtin_shufflevector(a,b," + ','.join(map(str, range(2*L))) + ");}\n")
        if L >= 4:
            add(f"shuf_lowhalf.{tag}", head + vt('H', ct, W//2) + "H f(V a,V b,V c,int n){return __builtin_shufflevector(a,b," + ','.join(map(str, range(L//2))) + ");}\n")
            add(f"shuf_highhalf.{tag}", head + vt('H', ct, W//2) + "H f(V a,V b,V c,int n){return __builtin_shufflevector(a,b," + ','.join(map(str, range(L//2, L))) + ");}\n")
        # conversions to every other element type with the same lanes
        for (en2, ct2, sz2, kind2) in E:
            W2 = L * sz2
            if (en2, ct2) == (en, ct) or W2 > 64 or W2 < 4: continue
            add(f"cvt_{en2}.{tag}", head + vt('R', ct2, W2) + "R f(V a,V b,V c,int n){return __builtin_convertvector(a,R);}\n")
# scalar elementwise builtins
for (en, ct, sz, kind) in E:
    if kind == 'f':
        for b in ('abs','sqrt','ceil','floor','trunc','round','rint','nearbyint','roundeven','canonicalize','exp','log','sin'):
            add(f"sew_{b}.{en}", f"{ct} f({ct} a,{ct} b,{ct} c){{return __builtin_elementwise_{b}(a);}}\n")
        for b in ('max','min','maximum','minimum','maxnum','minnum','maximumnum','minimumnum','copysign','pow','fmod'):
            add(f"sew_{b}.{en}", f"{ct} f({ct} a,{ct} b,{ct} c){{return __builtin_elementwise_{b}(a,b);}}\n")
        add(f"sew_fma.{en}", f"{ct} f({ct} a,{ct} b,{ct} c){{return __builtin_elementwise_fma(a,b,c);}}\n")
    else:
        for b in ('abs','popcount','bitreverse','clzg','ctzg'):
            add(f"sew_{b}.{en}", f"{ct} f({ct} a,{ct} b,{ct} c){{return __builtin_elementwise_{b}(a);}}\n")
        for b in ('max','min','add_sat','sub_sat'):
            add(f"sew_{b}.{en}", f"{ct} f({ct} a,{ct} b,{ct} c){{return __builtin_elementwise_{b}(a,b);}}\n")
        for b in ('fshl','fshr'):
            add(f"sew_{b}.{en}", f"{ct} f({ct} a,{ct} b,{ct} c){{return __builtin_elementwise_{b}(a,b,c);}}\n")
# masked memory builtins
for (en, ct, sz, kind) in E:
    for L in (2, 4, 8, 16):
        if L*sz > 64: continue
        head = ext('X', ct, L) + ext('B', '_Bool', L) + ext('I', 'int', L) + "\n"
        tag = f"{en}x{L}"
        add(f"mload.{tag}", head + "X f(B m,X* p,X w){return __builtin_masked_load(m,p,w);}\n")
        add(f"mstore.{tag}", head + "void f(B m,X v,X* p){__builtin_masked_store(m,v,p);}\n")
        add(f"mexpand.{tag}", head + f"X f(B m,{ct}* p,X w){{return __builtin_masked_expand_load(m,p,w);}}\n")
        add(f"mcompress.{tag}", head + f"void f(B m,X v,{ct}* p){{__builtin_masked_compress_store(m,v,p);}}\n")
        add(f"mgather.{tag}", head + f"X f(B m,I i,{ct}* p,X w){{return __builtin_masked_gather(m,i,p,w);}}\n")
        add(f"mscatter.{tag}", head + f"void f(B m,I i,X v,{ct}* p){{__builtin_masked_scatter(m,i,v,p);}}\n")
for name, src in cases:
    with open(os.path.join(out, name + '.c'), 'w') as f:
        f.write(src)
print(len(cases))
