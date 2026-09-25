#!/usr/bin/env python3
"""Integrator runner: subject compiler vs independent scope oracle + clang + gcc.

For each (family, seed) the oracle renders a C17 program and its expected
label=value lines from its own scope/layout model. The subject compiler (buster
`ide cc`, optionally a seeded-fault mutant selected by BUSTER_SEED_FAULT) builds
and runs it under several configurations. A row is a DEFECT candidate only when
clang and gcc both reproduce the oracle exactly and the subject does not.

Usage:
  run.py --oracle scope_oracle.py --subject IDE --families D1,D2 --seeds 0-99
         --work DIR [--fault K] [--configs default,nossa,none,quality] [--jobs N]
"""
import argparse, concurrent.futures, json, os, subprocess, sys

REF_FLAGS = ["-std=c17", "-pedantic-errors", "-w"]
CONFIGS = {
    "default": [],
    "nossa": ["-fno-frontend-ssa"],
    "none": ["-fregister-allocator=none"],
    "mirstack": ["-fregister-allocator=mir-stack"],
    "quality": ["-fregister-allocator=quality"],
    "nofast": ["-fno-canonical-fast"],
    "O2": ["-O2"],
}


def run(cmd, env=None, timeout=60):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=timeout)
        return r.returncode, r.stdout, r.stderr
    except subprocess.TimeoutExpired:
        return 124, "", "timeout"


def parse_lines(text):
    out = {}
    for line in text.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def compile_run(cc, flags, src, exe, env=None):
    rc, _, err = run(cc + flags + [src, "-o", exe], env=env)
    if rc:
        return {"phase": "compile", "rc": rc, "stderr": err.strip()[-400:]}
    rc, out, err = run([exe], env=env)
    if rc:
        return {"phase": "run", "rc": rc, "stderr": err.strip()[-400:], "stdout": out}
    return {"phase": "ok", "stdout": out}


def one(args, family, seed):
    wd = os.path.join(args.work, f"{family}-{seed}")
    os.makedirs(wd, exist_ok=True)
    src, exp = os.path.join(wd, "prog.c"), os.path.join(wd, "prog.expect")
    rc, _, err = run([sys.executable, args.oracle, "render", "--family", family, "--seed", str(seed), "--out", src, "--expect", exp])
    if rc:
        return {"family": family, "seed": seed, "status": "ORACLE_ERROR", "detail": err[-400:]}
    expected = parse_lines(open(exp).read())
    refs = {}
    for name, cc in (("clang", ["clang"]), ("gcc", ["gcc"])):
        r = compile_run(cc, REF_FLAGS, src, os.path.join(wd, f"ref.{name}"))
        refs[name] = parse_lines(r["stdout"]) if r["phase"] == "ok" else None
    refs_ok = all(refs[n] == expected for n in refs)
    env = dict(os.environ)
    env.pop("BUSTER_SEED_FAULT", None)
    if args.fault is not None:
        env["BUSTER_SEED_FAULT"] = str(args.fault)
    rows = {}
    for config in args.configs:
        r = compile_run([args.subject, "cc"], CONFIGS[config], src, os.path.join(wd, f"sub.{config}"), env=env)
        if r["phase"] != "ok":
            rows[config] = {"status": r["phase"].upper() + "_FAIL", "detail": r.get("stderr", "")}
            continue
        got = parse_lines(r["stdout"])
        diff = {k: (expected.get(k), got.get(k)) for k in set(expected) | set(got) if expected.get(k) != got.get(k)}
        rows[config] = {"status": "MISMATCH" if diff else "MATCH", "diff": diff}
    status = "REFERENCE_DISAGREES" if not refs_ok else ("DEFECT" if any(v["status"] != "MATCH" for v in rows.values()) else "PASS")
    return {"family": family, "seed": seed, "status": status, "configs": rows}


def seeds(spec):
    out = []
    for part in spec.split(","):
        if "-" in part:
            a, b = part.split("-")
            out.extend(range(int(a), int(b) + 1))
        else:
            out.append(int(part))
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--oracle", required=True)
    p.add_argument("--subject", required=True)
    p.add_argument("--families", required=True)
    p.add_argument("--seeds", required=True)
    p.add_argument("--work", required=True)
    p.add_argument("--fault", type=int)
    p.add_argument("--configs", default="default,nossa,none,quality")
    p.add_argument("--jobs", type=int, default=4)
    p.add_argument("--json", help="write all rows to this file")
    args = p.parse_args()
    args.configs = args.configs.split(",")
    os.makedirs(args.work, exist_ok=True)
    work = [(f, s) for f in args.families.split(",") for s in seeds(args.seeds)]
    results = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for r in pool.map(lambda fs: one(args, *fs), work):
            results.append(r)
    summary = {}
    for r in results:
        key = r["family"]
        summary.setdefault(key, {}).setdefault(r["status"], 0)
        summary[key][r["status"]] += 1
    for fam, counts in sorted(summary.items()):
        print(f"FAMILY {fam} " + " ".join(f"{k}={v}" for k, v in sorted(counts.items())))
    labels = {}
    for r in results:
        for cfg, row in r.get("configs", {}).items():
            for label in row.get("diff", {}):
                parts = label.split("."); kind = parts[2] if len(parts) > 2 else label
                labels[(r["family"], cfg, kind)] = labels.get((r["family"], cfg, kind), 0) + 1
    for (fam, cfg, kind), n in sorted(labels.items()):
        print(f"MISMATCH_LABELS family={fam} config={cfg} label_kind={kind} rows={n}")
    if args.json:
        with open(args.json, "w") as f:
            json.dump(results, f, indent=1, sort_keys=True)


if __name__ == "__main__":
    main()
