#!/usr/bin/env python3
"""Writes plan.json: pinned refs, generated growth grids and real workloads."""
import json
import sys

BASE = "ade6ac4b6ecb21f30b61b656439bac476c145e2f"
LUA = ["lapi.c", "lauxlib.c", "lbaselib.c", "lcode.c", "lcorolib.c", "lctype.c", "ldblib.c", "ldebug.c", "ldo.c",
       "ldump.c", "lfunc.c", "lgc.c", "linit.c", "liolib.c", "llex.c", "lmathlib.c", "lmem.c", "loadlib.c",
       "lobject.c", "lopcodes.c", "loslib.c", "lparser.c", "lstate.c", "lstring.c", "lstrlib.c", "ltable.c",
       "ltablib.c", "ltm.c", "lua.c", "luac.c", "lundump.c", "lutf8lib.c", "lvm.c", "lzio.c"]
SQLITE = ["-O2", "-I$INPUTS/sqlite-amalgamation-3530400", "-DSQLITE_THREADSAFE=1", "-DSQLITE_ENABLE_MATH_FUNCTIONS",
          "-DSQLITE_ENABLE_COLUMN_METADATA", "-c"]
SELF = ["-I$BASE_SRC/src", "-I$BASE_SRC/build/generated", "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0", "-c",
        "$BASE_SRC/src/buster/apps/ide/ide.c"]


def main():
    refs = {"base": BASE}
    for item in sys.argv[1:]:
        name, _, commit = item.partition("=")
        refs[name] = commit
    workload_refs = list(refs) + ["census"]
    workloads = [
        {"name": "selfhost-g", "argv": ["-g"] + SELF, "refs": workload_refs},
        {"name": "selfhost-g0", "argv": ["-g0"] + SELF, "refs": workload_refs},
        {"name": "sqlite3-g0", "argv": ["-g0"] + SQLITE + ["$INPUTS/sqlite-amalgamation-3530400/sqlite3.c"], "refs": workload_refs},
        {"name": "sqlite3-g", "argv": ["-g"] + SQLITE + ["$INPUTS/sqlite-amalgamation-3530400/sqlite3.c"], "refs": workload_refs},
        {"name": "sqlite-shell-g0", "argv": ["-g0"] + SQLITE + ["$INPUTS/sqlite-amalgamation-3530400/shell.c"], "refs": workload_refs},
        {"name": "cjson-g0", "argv": ["-g0", "-O2", "-I$INPUTS/cjson", "-c", "$INPUTS/cjson/cJSON.c"], "refs": workload_refs},
        {"name": "cjson-utils-g0", "argv": ["-g0", "-O2", "-I$INPUTS/cjson", "-c", "$INPUTS/cjson/cJSON_Utils.c"], "refs": workload_refs},
    ]
    for source in LUA:
        workloads.append({"name": f"lua-{source[:-2]}-g0", "argv": ["-g0", "-O2", "-I$INPUTS/lua-5.4.8/src", "-DLUA_USE_LINUX", "-c",
                                                                  f"$INPUTS/lua-5.4.8/src/{source}"], "refs": workload_refs, "timing_repeats": 1})
    reloc_grid = ([[family, r, 1] for family in ("ptr", "str", "struct", "reverse", "shuffle", "control")
                   for r in (500, 1000, 2000, 4000, 8000, 16000, 32000)] +
                  [[family, 2000, g] for family in ("ptr", "shuffle") for g in (2, 4, 8, 16)])
    experiments = []
    if "reloc" in refs:
        experiments.append({"name": "reloc", "generator": "gen_reloc.py", "refs": ["base", "reloc"], "grid": reloc_grid,
                            "show": ["validation_global_relocations", "validation_global_relocation_pairs",
                                     "validation_global_relocation_sorts", "validation_global_relocation_sort_rows"]})
    plan = {"refs": refs, "census_ref": "base", "variants": ["count", "plain"], "build_jobs": 3, "timing_repeats": 3,
            "workload_source": "base", "experiments": experiments, "workloads": workloads}
    json.dump(plan, open("plan.json", "w"), indent=1)


if __name__ == "__main__":
    main()
