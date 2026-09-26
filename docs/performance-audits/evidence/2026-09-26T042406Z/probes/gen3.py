import sys
n = int(sys.argv[1]); v = sys.argv[2]
o = ["long g(long);", "long f(long *p, long depth) {", "    long acc = depth, t0 = 1, t1 = 2;", "    while (p) {"]
if v.startswith("sw"):
    o.append("        switch (*p) {")
for i in range(n):
    if v == "sw_break_inside":
        o.append(f"        case {i}: {{ acc += p[1] * {i+3}; t0 ^= acc; break; }}")
    elif v == "sw_break_outside":
        o.append(f"        case {i}: {{ acc += p[1] * {i+3}; t0 ^= acc; }} break;")
    elif v == "sw_braced_fallthrough":
        o.append(f"        case {i}: {{ acc += p[1] * {i+3}; t0 ^= acc; }}")
    elif v == "sw_break_nested_if":
        o.append(f"        case {i}: if (p[1]) {{ acc += {i}; break; }} t0 ^= acc; break;")
    elif v == "loop_if_break":
        o.append(f"        if (p[1] == {i}) {{ acc += {i}; break; }}")
    elif v == "loop_if_continue":
        o.append(f"        if (p[1] == {i}) {{ acc += {i}; p = (long*)p[2]; continue; }}")
if v.startswith("sw"):
    o.append("        default: return acc;")
    o.append("        }")
o += ["        p = (long *)p[2];", "    }", "    return acc + t0 + t1;", "}"]
print("\n".join(o))
