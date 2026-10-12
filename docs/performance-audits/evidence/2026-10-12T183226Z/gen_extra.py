# Generator used for the bdo<D>.c and bwif<D>.c inputs (run as a one-off in the session).
import sys
d = int(sys.argv[2])
kind = sys.argv[1]
if kind == "bdo":
    print("int f(int x) {\n" + "do\n" * d + "x -= 1;\n" + "while (x > 0);\n" * d + "return x; }")
elif kind == "bwif":
    print("int f(int x) {\n" + "while (x > 0) if (x > 1)\n" * d + "x -= 1;\nreturn x; }")
