#!/usr/bin/env python3
"""Random constant initializers with designators, GNU ranges, unions, packed
structs and positional overwrites, for object differential testing (#1450).
Positional elements are placed only where C gives them a slot."""
import random, sys


def gen(seed):
    r = random.Random(seed)
    n_globals = 24
    out = [f"int g{i};" for i in range(n_globals)]
    out.append("struct P { int *a; long b; int *c; };")
    out.append("union U { int *p; long v; char s[12]; };")
    out.append("struct __attribute__((packed)) K { char t; int *p; short h; int *q; };")
    out.append("struct N { struct P p[3]; union U u; int *z; };")

    def ptr():
        return f"&g{r.randrange(n_globals)}" if r.random() < 0.85 else "0"

    def value(kind):
        if kind == "ptr":
            return ptr()
        if kind == "P":
            # Leading positional members, then designators.
            items = [["0", ptr()][r.randrange(2)] if slot != 1 else str(r.randrange(9)) for slot in range(r.randrange(0, 4))]
            items = [ptr() if slot != 1 else str(r.randrange(9)) for slot in range(len(items))]
            items += [r.choice([f".a = {ptr()}", f".c = {ptr()}", f".b = {r.randrange(99)}"]) for _ in range(r.randrange(0, 3))]
            return "{" + ", ".join(items or ["0"]) + "}"
        if kind == "U":
            c = r.random()
            return "{" + (f".p = {ptr()}" if c < 0.5 else f".v = {r.randrange(99)}" if c < 0.8 else '.s = "abc"') + "}"
        if kind == "K":
            return "{" + ", ".join(r.choice([f".p = {ptr()}", f".q = {ptr()}", f".t = {r.randrange(9)}"]) for _ in range(r.randrange(1, 4))) + "}"
        items = []
        for _ in range(r.randrange(1, 6)):
            c = r.random()
            if c < 0.3:
                items.append(f".p[{r.randrange(3)}] = {value('P')}")
            elif c < 0.45:
                items.append(f".p[{r.randrange(3)}].a = {ptr()}")
            elif c < 0.6:
                items.append(f".u = {value('U')}")
            elif c < 0.75:
                items.append(f".u.p = {ptr()}")
            else:
                items.append(f".z = {ptr()}")
        return "{" + ", ".join(items) + "}"

    for table, kind in enumerate(["ptr", "P", "U", "K", "N"]):
        ctype = {"ptr": "int *", "P": "struct P", "U": "union U", "K": "struct K", "N": "struct N"}[kind]
        count = r.randrange(4, 48)
        items = []
        following = 0
        for _ in range(r.randrange(1, 3 * count)):
            c = r.random()
            if c < 0.45:
                index = r.randrange(count)
                items.append(f"[{index}] = {value(kind)}")
                following = index + 1
            elif c < 0.55:
                low = r.randrange(count)
                high = r.randrange(low, count)
                items.append(f"[{low} ... {high}] = {value(kind)}")
                following = high + 1
            elif c < 0.65 and kind != "ptr":
                member = {"P": ["a", "c"], "U": ["p"], "K": ["p", "q"], "N": ["z"]}[kind]
                index = r.randrange(count)
                items.append(f"[{index}].{r.choice(member)} = {ptr()}")
                # A positional element would continue inside [index].
                following = count
            elif following < count:
                items.append(value(kind))
                following += 1
        out.append(f"{ctype} t{table}[{count}] = {{ {', '.join(items) or '0'} }};")
    out.append("int main(void) { return 0; }")
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    sys.stdout.write(gen(int(sys.argv[1])))
