#!/usr/bin/env python3
"""Deterministic single-TU C populations for the #531 lowering census.

Writes tiny.c, many_small.c and few_large.c into the directory given as the
first argument. No randomness: the same script always writes the same bytes,
so the SHA-256 of each file is the frozen input identity.
"""
import os
import sys


def tiny():
    return """\
struct Pair { int a; int b; };
static int add(int x, int y) { return x + y; }
int sum_pair(struct Pair p) { return add(p.a, p.b); }
int main(void) { struct Pair p = {1, 2}; return sum_pair(p) - 3; }
"""


def many_small(count):
    out = ["struct Node { int value; struct Node *next; };\n",
           "int accumulate(int seed);\n"]
    for index in range(count):
        out.append(
            f"int f{index}(struct Node *n, int k)\n"
            "{\n"
            f"    int total = k * {index % 7 + 1};\n"
            "    while (n)\n"
            "    {\n"
            f"        total += n->value ^ {index % 13};\n"
            "        n = n->next;\n"
            "    }\n"
            f"    if (total > {index % 101})\n"
            "    {\n"
            "        total = accumulate(total);\n"
            "    }\n"
            "    return total;\n"
            "}\n")
    out.append("int accumulate(int seed) { return seed * 2 + 1; }\n")
    out.append("int main(void)\n{\n    struct Node n = {1, 0};\n    int total = 0;\n")
    for index in range(count):
        out.append(f"    total += f{index}(&n, {index});\n")
    out.append("    return total & 0xff;\n}\n")
    return "".join(out)


def few_large(count, statements):
    out = ["struct State { int regs[16]; unsigned long long acc; int pc; };\n"]
    for index in range(count):
        out.append(f"int big{index}(struct State *s, int n)\n{{\n    int i;\n    for (i = 0; i < n; i += 1)\n    {{\n")
        for step in range(statements):
            reg = step % 16
            other = (step * 7 + index) % 16
            arm = step % 5
            if arm == 0:
                out.append(f"        s->regs[{reg}] += s->regs[{other}] * {step % 11 + 1};\n")
            elif arm == 1:
                out.append(f"        if (s->regs[{reg}] > {step % 977}) s->acc ^= (unsigned long long)s->regs[{other}] << {step % 31};\n")
            elif arm == 2:
                out.append(f"        switch (s->regs[{other}] & 3) {{ case 0: s->pc += {step % 5}; break; case 1: s->pc -= 1; break; default: s->pc ^= {step % 9}; break; }}\n")
            elif arm == 3:
                out.append(f"        s->regs[{reg}] = (s->regs[{reg}] >> 1) | (s->regs[{other}] << {step % 7 + 1});\n")
            else:
                out.append(f"        s->acc += (unsigned long long)(s->regs[{reg}] - s->regs[{other}]) + {step};\n")
        out.append("    }\n    return s->pc;\n}\n")
    out.append("int main(void)\n{\n    struct State s = {{0}, 0, 0};\n    int total = 0;\n")
    for index in range(count):
        out.append(f"    total += big{index}(&s, 3);\n")
    out.append("    return total & 0xff;\n}\n")
    return "".join(out)


def main():
    directory = sys.argv[1]
    os.makedirs(directory, exist_ok=True)
    files = {
        "tiny.c": tiny(),
        "many_small.c": many_small(6000),
        "few_large.c": few_large(4, 6000),
    }
    for name, text in files.items():
        with open(os.path.join(directory, name), "w", newline="\n") as handle:
            handle.write(text)


if __name__ == "__main__":
    main()
