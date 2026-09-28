import sys
n = int(sys.argv[1])  # number of opcode cases
out = []
out.append("typedef struct Vm { long regs[64]; unsigned char *pc; long acc; struct Vm *next; } Vm;")
out.append("extern long ext_call(long, long);")
out.append("long interp(Vm *vm, long limit) {")
out.append("    long acc = 0, t0 = 1, t1 = 2, t2 = 3;")
out.append("    for (long step = 0; step < limit; step++) {")
out.append("        unsigned op = *vm->pc++;")
out.append("        switch (op) {")
for i in range(n):
    a, b, c = i % 64, (i * 7 + 3) % 64, (i * 13 + 5) % 64
    k = i % 6
    out.append(f"        case {i}: {{")
    out.append(f"            long x{i} = vm->regs[{a}] + {i};")
    out.append(f"            long y{i} = vm->regs[{b}] * (x{i} | {i+1});")
    if k == 0:
        out.append(f"            if (x{i} > y{i}) {{ acc += ext_call(x{i}, y{i}); }} else {{ acc -= y{i}; }}")
    elif k == 1:
        out.append(f"            for (int j = 0; j < (int)(x{i} & 7); j++) t0 += vm->regs[(j + {c}) & 63];")
    elif k == 2:
        out.append(f"            long buf{i}[4] = {{x{i}, y{i}, t1, t2}}; acc += buf{i}[(unsigned)x{i} & 3];")
    elif k == 3:
        out.append(f"            vm->regs[{c}] = x{i} ^ y{i}; t1 += x{i};")
    elif k == 4:
        out.append(f"            if (vm->next) {{ vm = vm->next; t2 ^= y{i}; }}")
    else:
        out.append(f"            acc = ext_call(acc, x{i} - y{i});")
    out.append(f"            break; }}")
out.append("        default: return acc + t0 + t1 + t2;")
out.append("        }")
out.append("    }")
out.append("    return acc + t0 + t1 + t2;")
out.append("}")
print("\n".join(out))
