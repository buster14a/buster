#!/usr/bin/env python3
"""Writes plan.json for a fixture sweep: every tests/*.c of the base
checkout, compiled -g0 -c for x86-64 and AArch64 Linux by every pinned ref's
counting build, so object identity is checked at the exact commits."""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    refs = {"base": "ade6ac4b6ecb21f30b61b656439bac476c145e2f"}
    for item in sys.argv[1:]:
        name, _, commit = item.partition("=")
        refs[name] = commit
    fixtures = sorted(name for name in os.listdir(os.path.join(HERE, "..", "tests")) if name.endswith(".c"))
    workloads = []
    for target in ("x86_64-linux-gnu", "aarch64-linux-gnu"):
        for name in fixtures:
            workloads.append({"name": f"fixture-{target}-{name[:-2]}",
                              "argv": ["-target", target, "-g0", "-c", f"$BASE_SRC/tests/{name}"], "refs": list(refs)})
    plan = {"refs": refs, "variants": ["count"], "build_jobs": 3, "timing_repeats": 0,
            "workload_source": "base", "experiments": [], "workloads": workloads}
    json.dump(plan, open(os.path.join(HERE, "plan.json"), "w"), indent=1)
    print(len(workloads), "workloads,", len(refs), "refs")


if __name__ == "__main__":
    main()
