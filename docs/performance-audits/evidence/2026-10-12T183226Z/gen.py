import sys
kind, d = sys.argv[1], int(sys.argv[2])
o = ["int f(int x) {"]
for i in range(d):
    if kind == "while": o.append(f"while (x > {i%7}) {{")
    elif kind == "if": o.append(f"if (x > {i%7}) {{")
    elif kind == "switch": o.append(f"switch (x) {{ case {i%7}: {{")
    elif kind == "for": o.append(f"for (; x > {i%7};) {{")
o.append("x -= 1;")
for i in range(d):
    o.append("}}" if kind == "switch" else "}")
o.append("return x; }")
print("\n".join(o))
