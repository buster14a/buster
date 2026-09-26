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
    debug_refs = [name for name in ("dbgseed", "dbgval") if name in refs]
    if debug_refs:
        grid = ([["funcs", n] for n in (500, 1000, 2000, 4000, 8000, 16000)] +
                [["locals", n] for n in (25, 50, 100, 200, 400, 800)] +
                [["locals_split", n] for n in (200, 800)])
        experiments.append({"name": "debug", "generator": "gen_debug.py", "refs": ["base", "census"] + debug_refs, "grid": grid,
                            "flag_sets": [["-g", "-c"]],
                            "show": ["census_dbg_seed_scan_visits", "census_dbgv_block_unresolved_visits", "debug_function_index_rows",
                                     "debug_function_seed_scan_rows", "debug_value_blocks", "debug_value_local_visits"]})
    if "defidx" in refs:
        grid = ([["defs", t, 1000] for t in (0, 1000, 4000, 16000)] + [["defs", 4000, q] for q in (250, 4000)] +
                [["defs_multi", 4000, 1000], ["local", 4000, 1000], ["local", 4000, 4000]])
        experiments.append({"name": "defidx", "generator": "gen_defs.py", "refs": ["base", "census", "defidx"], "grid": grid,
                            "show": ["census_core_step_def_visits", "census_bind_agg_visits", "census_tnp_anon_visits",
                                     "c_definition_scans", "c_definition_index_probes", "c_definition_scan_rows"]})
    plan = {"refs": refs, "census_ref": "base", "variants": ["count", "plain"], "build_jobs": 3, "timing_repeats": 3,
            "workload_source": "base", "experiments": experiments, "workloads": workloads}
    json.dump(plan, open("plan.json", "w"), indent=1)


if __name__ == "__main__":
    main()
