import sys
kind, d = sys.argv[1], int(sys.argv[2])
o = ["int f(int x) {"]
for i in range(d):
    if kind == "bswitch": o.append("switch (x)")
    elif kind == "bwhile": o.append("while (x > 0)")
    elif kind == "bif": o.append("if (x > 0)")
    elif kind == "bfor": o.append("for (; x > 0;)")
o.append("x -= 1;")
o.append("return x; }")
print("\n".join(o))
