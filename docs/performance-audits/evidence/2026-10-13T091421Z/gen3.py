import sys
kind, d = sys.argv[1], int(sys.argv[2])
o = ["int f(int x) {"]
for i in range(d):
    if kind == "label": o.append(f"L{i}: for (; x > {i%7};) {{")
    elif kind == "goto": o.append(f"for (; x > {i%7};) {{ if (x == {i}) goto L{i}; L{i}: ;")
    elif kind == "decl": o.append(f"for (; x > {i%7};) {{ int y{i} = x;")
o.append("x -= 1;")
o += ["}"] * d
o.append("return x; }")
print("\n".join(o))
