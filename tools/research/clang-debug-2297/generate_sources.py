#!/usr/bin/env python3
"""Generate the original #1948 first-party compile fixtures without vendoring."""
import argparse
from pathlib import Path


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    for count in (4, 32, 512, 4096):
        lines = ["typedef unsigned long long U64;"]
        for i in range(count):
            lines.append(f"U64 f{i}(U64 x,U64 y) {{ x=(x^y)*{(i%127)*2+3}ull; y=(y+x)>>{i%23+1}u; return (x+y)^(x<<{i%17+1}u); }}")
        (directory / f"scalar_{count}.c").write_text("\n".join(lines) + "\n")
    lines = ["typedef unsigned long long U64;", "#define CAT_(a,b) a##b", "#define CAT(a,b) CAT_(a,b)", "#define STEP(x,y,c) (((x)^(y))*(c)+(x))", "#define FN(i,c) U64 CAT(macro_,i)(U64 x,U64 y) { return STEP(STEP(STEP(x,y,c),y,c),x,c); }"]
    lines += [f"FN({i},{(i%127)*2+3}ull)" for i in range(1024)]
    (directory / "macro_1024.c").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--sources", required=True, type=Path)
    generate(parser.parse_args().sources)
