#!/usr/bin/env python3
# Input variants used by the audit. Neither changes what the program computes.
# usage: prepare_inputs.py <sqlite3.c truncated at "End of sqlite3.c"> <lua54 dir>
#  - sqlite3_fast.c: spells the six function-pointer-returning DlSym
#    declarators through a typedef so canonical validation passes and FAST runs
#    on every function (the #1601 shape; fixed by #1678, not yet on main).
#  - ltablib.c: rewrites the two `while (g(), h())` loops that main still
#    rejects (#1421 family) as `for (;;) { g(); if (!h()) break; ...`.
import os, re, sys
src, lua = sys.argv[1], sys.argv[2]
s = open(src, encoding="latin-1").read()
pat = re.compile(r'^((?:SQLITE_PRIVATE|static) )void \(\*((?:sqlite3Os|unix|win|memdb)DlSym)\(([^()]*)\)\)\(void\)', re.M)
s, n = pat.subn(lambda m: f"{m.group(1)}buster_census_dlsym_fn {m.group(2)}({m.group(3)})", s)
assert n == 6, n
open(os.path.join(os.path.dirname(src), "sqlite3_fast.c"), "w", encoding="latin-1").write("typedef void (*buster_census_dlsym_fn)(void);\n" + s)
p = os.path.join(lua, "ltablib.c"); t = open(p).read()
t = t.replace("while ((void)lua_geti(L, 1, ++i), sort_comp(L, -1, -2)) {", "for (;;) { (void)lua_geti(L, 1, ++i); if (!sort_comp(L, -1, -2)) break;")
t = t.replace("while ((void)lua_geti(L, 1, --j), sort_comp(L, -3, -1)) {", "for (;;) { (void)lua_geti(L, 1, --j); if (!sort_comp(L, -3, -1)) break;")
open(p, "w").write(t)
