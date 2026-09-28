import sys
n = int(sys.argv[1])  # opcode count in the evaluator
o = []
o.append("#include <stdio.h>")
o.append("#include <stdlib.h>")
o.append("typedef struct Node { int op; long a, b; struct Node *kid; } Node;")
o.append("static long sink;")
o.append("long eval(Node *n, long depth) {")
o.append("    long acc = depth, t0 = 1, t1 = 2, t2 = 3;")
o.append("    while (n) {")
o.append("        switch (n->op) {")
for i in range(n):
    k = i % 5
    o.append(f"        case {i}: {{")
    o.append(f"            long x = n->a * {i+3} + t0, y = n->b ^ (acc + {i});")
    if k == 0:
        o.append(f"            if (x > y) {{ acc += x - y; t1 ^= x; }} else {{ acc -= y; t2 += y; }}")
    elif k == 1:
        o.append(f"            for (int j = 0; j < (int)(x & 3); j++) t0 += y + j;")
    elif k == 2:
        o.append(f"            t2 = (t2 * {i+1}) ^ (x | y); acc += t2 & 15;")
    elif k == 3:
        o.append(f"            if (n->kid) acc += eval(n->kid, depth + 1) + x - y;")
    else:
        o.append(f"            t1 += (x * y) >> 3; acc ^= t1;")
    o.append(f"            break; }}")
o.append("        default: return acc + t0 + t1 + t2;")
o.append("        }")
o.append("        n = n->op == 3 ? 0 : n->kid;")
o.append("    }")
o.append("    return acc + t0 + t1 + t2;")
o.append("}")
o.append("int main(int argc, char **argv) {")
o.append("    long depth = argc > 1 ? atol(argv[1]) : 1000;")
o.append("    Node *nodes = calloc((size_t)depth + 1, sizeof(Node));")
o.append("    for (long i = 0; i < depth; i++) { nodes[i].op = 3; nodes[i].a = i; nodes[i].b = i * 7; nodes[i].kid = i + 1 < depth ? &nodes[i + 1] : 0; }")
o.append("    long r = eval(nodes, 0);")
o.append("    printf(\"depth=%ld result=%ld\\n\", depth, r);")
o.append("    return 0;")
o.append("}")
print("\n".join(o))
