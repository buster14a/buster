#!/usr/bin/env python3
"""Validate native desktop phase journals and rank observed tree critical paths.

No scheduling or console-log inference. The driver owns plans, admissions and
child statuses; this consumer joins those records to the existing coverage
contract. analyze() validates first, rank() and predict() operate only on complete
observations. collect() retains errors and integrates with ci_summary.py.
"""
from collections import Counter, defaultdict
import itertools
import json
import os
from pathlib import Path
import re
import subprocess

SCHEMA = "buster-desktop-phases-v1"
PHASES = {"configure", "build", "validation", "test", "post_test", "self_host", "scheduler", "clean", "census", "evidence"}
ID = re.compile(r"[A-Za-z0-9_-]+\Z")
MAX_BYTES = 4 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def integer(value):
    return type(value) is int and value >= 0


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"duplicate JSON key: {key}")
        result[key] = value
    return result


def read(path):
    require(not path.is_symlink() and path.is_file(), f"missing/nonregular evidence: {path.name}")
    require(path.stat().st_size <= MAX_BYTES, f"oversized evidence: {path.name}")
    data = path.read_bytes()
    require(data.endswith(b"\n"), f"interrupted publication: {path.name}")
    value = json.loads(data, object_pairs_hook=unique_pairs)
    require(isinstance(value, dict), f"nonobject evidence: {path.name}")
    return value


def index(items, label):
    require(isinstance(items, list), f"missing {label}")
    result = {}
    for item in items:
        require(isinstance(item, dict) and isinstance(item.get("id"), str), f"malformed {label}")
        key = item["id"]
        require(key not in result and ID.fullmatch(key), f"duplicate/invalid {label} identity: {key}")
        result[key] = item
    return result


def task_id(tree, phase, config=""):
    return f"{tree}-{phase}-{config or 'all'}"


def validate_plan(plan, coverage, environment):
    require(plan.get("schema") == SCHEMA, "unknown phase schema")
    require(integer(plan.get("epoch_us")) and plan["epoch_us"] > 0, "invalid monotonic origin")
    identity = plan.get("identity")
    require(isinstance(identity, dict) and isinstance(coverage, dict), "missing source/coverage identity")
    for key in ("lane_id", "source_revision", "source_hash", "driver_hash", "repository", "run_id", "run_attempt", "platform", "architecture", "shard"):
        require(identity.get(key) == coverage.get("identity", {}).get(key) and bool(identity.get(key)), f"coverage identity mismatch: {key}")
    require(re.fullmatch(r"[0-9a-f]{40}", identity.get("source_tree", "")), "missing exact source tree")
    for key, env in (("source_revision", "GITHUB_SHA"), ("run_id", "GITHUB_RUN_ID"), ("run_attempt", "GITHUB_RUN_ATTEMPT"),
                     ("repository", "GITHUB_REPOSITORY"), ("workflow", "GITHUB_WORKFLOW"), ("job", "GITHUB_JOB")):
        require(bool(identity.get(key)), f"missing {key}")
        if env in environment:
            require(identity[key] == environment[env], f"current job mismatch: {key}")
    require(plan.get("scheduler") in ("direct", "pooled"), "unknown scheduler")
    for key in ("outer_jobs", "logical_cpus", "cpu_budget"):
        require(integer(plan.get(key)) and plan[key] > 0, f"invalid quota: {key}")
    require(plan.get("cpu_time") == plan.get("peak_rss") == "unknown", "unmeasured resources must be unknown")
    require(coverage.get("phase") == "complete", "coverage is incomplete")
    trees = index(plan.get("trees"), "tree")
    tasks = index(plan.get("tasks"), "task")
    require(0 < len(trees) <= 8 and len(tasks) <= 96, "invalid tree/task cardinality")
    expected_rows = coverage.get("expected", [])
    required = {row["id"]: row for row in expected_rows if row.get("state") == "required" and
                (identity["shard"] == "combinations" or row.get("owner_shard") == identity["shard"])}
    detected = {row["id"]: row for row in coverage.get("detected", [])}
    owned = []
    expected_tasks = {task_id("matrix", "evidence", "coverage")}
    canonical = None
    for tree_id, tree in trees.items():
        rows = tree.get("rows")
        require(isinstance(rows, list) and 0 < len(rows) <= 2 and len(set(rows)) == len(rows), f"missing/duplicate rows: {tree_id}")
        require(all(row in required for row in rows), f"unknown/excluded rows: {tree_id}")
        owned.extend(rows)
        require(isinstance(tree.get("build_directory"), str) and tree["build_directory"], "missing build directory")
        require(tree.get("generator") == "Ninja Multi-Config" and tree.get("lto") is False, "changed generator/LTO policy")
        configs = []
        expected_tasks.add(task_id(tree_id, "configure"))
        if plan["scheduler"] == "pooled":
            expected_tasks.add(task_id(tree_id, "build"))
        for row_id in rows:
            row, cap = required[row_id], detected.get(row_id, {})
            configs.append(row["configuration"])
            for tree_key, cap_key in (("compiler", "compiler"), ("compiler_path", "path"), ("compiler_sha256", "path_hash"),
                                      ("compiler_identity", "identity"), ("compiler_version", "version"), ("target", "target")):
                require(tree.get(tree_key) == cap.get(cap_key) and bool(tree.get(tree_key)), f"compiler mismatch: {tree_id}/{tree_key}")
            require(tree.get("sanitize") == int(row["sanitize"]), "sanitizer policy mismatch")
            if row["compiler"] == "clang":
                expected_tasks.update((task_id(tree_id, "test", row["configuration"]), task_id(tree_id, "validation", row["configuration"])))
                if row["unity"]:
                    canonical = tree_id
                    if plan["scheduler"] == "direct":
                        expected_tasks.add(task_id(tree_id, "build", row["configuration"]))
            elif plan["scheduler"] == "direct":
                expected_tasks.add(task_id(tree_id, "build", row["configuration"]))
        require(isinstance(tree.get("configurations"), str) and tree["configurations"].split(";") == configs, "configuration identity mismatch")
    require(Counter(owned) == Counter(required.keys()), "tree rows are not uniquely owned/exhaustive")
    require(len({t["build_directory"] for t in trees.values()}) == len(trees), "duplicate build directory")
    obligations = coverage.get("obligations", {})
    for key, phase in (("unity_analysis", "post_test"), ("self_host", "self_host")):
        if obligations.get(key, {}).get("state") in ("scheduled", "success"):
            require(canonical is not None, f"missing {key} owner")
            expected_tasks.add(task_id(canonical, phase, "Release"))
    if obligations.get("self_host", {}).get("state") in ("scheduled", "success"):
        expected_tasks.update((task_id(canonical, "clean", "Release"), task_id(canonical, "evidence", "capture"), task_id(canonical, "evidence", "clean")))
        if identity["platform"] == "linux" and identity["architecture"] == "x86_64":
            expected_tasks.update((task_id(canonical, "census", "Release"), task_id(canonical, "evidence", "census_validate"), task_id(canonical, "evidence", "census_prepare")))
    if plan["scheduler"] == "pooled":
        expected_tasks.add(task_id("matrix", "scheduler"))
    require(set(tasks) == expected_tasks, f"missing/unknown task identities: {sorted(set(tasks) ^ expected_tasks)}")
    budget = plan["cpu_budget"]
    expected_outer = 1 if plan["scheduler"] == "direct" else min(len(trees), budget if budget <= 4 else max(1, budget // 2))
    require(plan["outer_jobs"] == expected_outer, "outer quota differs from native policy")
    for name, task in tasks.items():
        require(task.get("phase") in PHASES, f"unknown phase: {name}")
        require(name == task_id(task.get("tree"), task["phase"], task.get("configuration", "")), f"mismatched task identity: {name}")
        require(task.get("inner_jobs") == "unknown" or integer(task.get("inner_jobs")) and task["inner_jobs"] > 0, "invalid inner quota")
        require(isinstance(task.get("argv"), list) and all(isinstance(x, str) for x in task["argv"]), "malformed command identity")
        dep = task.get("dependency")
        require(dep in ("ready", "nested", "scheduler") or dep in tasks, f"unknown dependency: {name}")
        require((dep == "nested") == (task["phase"] == "test"), "nested test identity mismatch")
        require(isinstance(task.get("pool_edge"), str), "missing pool-edge identity")
        expected_pool, expected_dep = "", "nested" if task["phase"] == "test" else "ready"
        if plan["scheduler"] == "pooled" and task["tree"] in trees:
            tree_id = task["tree"]
            if task["phase"] == "build":
                expected_pool, expected_dep = f"build-{tree_id}", "scheduler"
            elif task["phase"] == "validation":
                configs = trees[tree_id]["configurations"].split(";")
                at = configs.index(task["configuration"])
                expected_pool = f"validation-{tree_id}"
                expected_dep = task_id(tree_id, "validation", configs[at - 1]) if at else task_id(tree_id, "build")
            elif task["phase"] == "post_test":
                expected_pool, expected_dep = f"validation-{tree_id}", task_id(tree_id, "validation", "Release")
            elif task["phase"] == "self_host":
                expected_pool, expected_dep = "self-host", task_id(tree_id, "build")
        require(task["pool_edge"] == expected_pool and dep == expected_dep, f"admission/dependency identity mismatch: {name}")
        if task["pool_edge"]:
            require(plan["scheduler"] == "pooled" and task["phase"] in ("build", "validation", "post_test", "self_host"), "impossible pooled phase")
    return trees, tasks


def analyze(root, coverage, environment=None):
    root = Path(root)
    require(not root.is_symlink() and root.is_dir(), "missing phase evidence directory")
    environment = environment or {}
    plan = read(root / "plan.json")
    trees, tasks = validate_plan(plan, coverage, environment)
    epoch = plan["epoch_us"]
    records = {}
    consumed = {"plan.json", "terminal.json", "summary.json", "summary.md"}
    for name, task in tasks.items():
        starts = sorted(root.glob(f"{name}.*.start.json"))
        ends = sorted(root.glob(f"{name}.*.end.json"))
        require(len(starts) == 1, f"never admitted or admitted twice: {name}")
        require(len(ends) == 1, f"failed/cancelled/interrupted publication: {name}")
        start, end = read(starts[0]), read(ends[0])
        consumed.update((starts[0].name, ends[0].name))
        for key in ("id", "epoch_us", "pid", "start_us", "argv"):
            require(key in start and start[key] == end.get(key), f"start/end mismatch: {name}/{key}")
        require(start["id"] == name and start["epoch_us"] == epoch and integer(start["pid"]) and start["pid"] > 0, f"stale/mismatched task: {name}")
        require(starts[0].name == f"{name}.{start['pid']}.start.json" and ends[0].name == f"{name}.{start['pid']}.end.json", "filename/process mismatch")
        require(start.get("state") == "running" and end.get("state") == "success", f"unsuccessful phase: {name}/{end.get('state')}")
        if task["phase"] == "evidence":
            require(start.get("authority") == end.get("authority") == "driver_callback" and type(end.get("result")) is int and end["result"] == 0,
                    f"callback exit authority failed: {name}")
        else:
            require(end.get("authority", "process") == "process" and all(type(end.get(k)) is int and end[k] == 0 for k in ("result", "platform_status", "timed_out", "termination_requested", "forcibly_terminated")) and type(end.get("spawned")) is int and end["spawned"] == 1,
                    f"child exit authority failed: {name}")
        times = [epoch] + [end.get(k) for k in ("start_us", "child_start_us", "end_us", "publication_start_us")]
        require(all(integer(t) for t in times) and times == sorted(times), f"non-monotonic events: {name}")
        require(isinstance(end["argv"], list) and end["argv"] and all(isinstance(a, str) for a in end["argv"]), "missing child command")
        require(not task["argv"] or task["argv"] == end["argv"], f"changed child command: {name}")
        require(end.get("cpu_time") == end.get("peak_rss") == "unknown", "unavailable resources must remain unknown")
        if task["dependency"] == "ready":
            ready_path = root / f"{name}.ready.json"
            ready = read(ready_path)
            consumed.add(ready_path.name)
            require(ready.get("epoch_us") == epoch and integer(ready.get("ready_us")) and epoch <= ready["ready_us"] <= end["start_us"], f"invalid enqueue/admission: {name}")
            end["ready_us"] = ready["ready_us"]
        if task["phase"] == "validation" and plan["scheduler"] == "pooled":
            require(end.get("test_jobs") == str(task["inner_jobs"]), f"test-worker quota mismatch: {name}")
        tree = trees.get(task["tree"])
        if tree:
            def same_path(a, b):
                return os.path.normcase(os.path.abspath(a)) == os.path.normcase(os.path.abspath(b))
            argv = end["argv"]
            if task["phase"] == "test":
                executable = "ide.exe" if plan["identity"]["platform"] == "windows" else "ide"
                expected = Path(tree["build_directory"]) / task["configuration"] / executable
                require(same_path(argv[0], expected) and len(argv) > 1 and argv[1] == "test", f"test executable/tree mismatch: {name}")
                if plan["scheduler"] == "pooled":
                    parent = tasks[task_id(task["tree"], "validation", task["configuration"])]
                    require(end.get("test_jobs") == str(parent["inner_jobs"]), f"nested test-worker quota mismatch: {name}")
            elif task["phase"] == "census":
                expected = Path(tree["build_directory"]) / "Release" / "ide"
                require(same_path(argv[0], expected) and len(argv) > 1 and argv[1] == "x86_64_completion_census", f"census executable/tree mismatch: {name}")
            elif task["phase"] in ("build", "validation", "post_test", "self_host", "clean"):
                option = "--build-directory" if task["phase"] == "self_host" else "--build"
                if option in argv:
                    at = argv.index(option)
                    require(at + 1 < len(argv) and same_path(argv[at + 1], tree["build_directory"]), f"child build/tree mismatch: {name}")
                else:
                    require(task["phase"] == "post_test" and len(argv) > 2 and argv[1] == "clang_analyze" and same_path(argv[2], tree["build_directory"]), f"missing tree command: {name}")
                if task["phase"] == "build" and plan["scheduler"] == "pooled":
                    require("--parallel" in argv and argv.index("--parallel") + 1 < len(argv) and argv[argv.index("--parallel") + 1] == str(task["inner_jobs"]), f"Ninja quota mismatch: {name}")
        records[name] = end
    for name, task in tasks.items():
        record = records[name]
        dep = task["dependency"]
        if dep == "nested":
            parent = records[task_id(task["tree"], "validation", task["configuration"])]
            require(parent["child_start_us"] <= record["start_us"] <= record["end_us"] <= parent["end_us"], f"impossible test overlap: {name}")
            record["ready_us"] = record["start_us"]
        elif dep == "scheduler":
            record["ready_us"] = records[task_id("matrix", "scheduler")]["child_start_us"]
        elif dep != "ready":
            record["ready_us"] = records[dep]["end_us"]
        require(record["ready_us"] <= record["start_us"], f"dependency overlap: {name}")
        if task["phase"] != "configure" and task["tree"] in trees:
            require(records[task_id(task["tree"], "configure")]["end_us"] <= record["start_us"], f"build before configure: {name}")
    terminal = read(root / "terminal.json")
    require(terminal.get("epoch_us") == epoch and terminal.get("result") == 0 and integer(terminal.get("terminal_us")), "failed/incomplete driver publication")
    require(max(record["publication_start_us"] for record in records.values()) <= terminal["terminal_us"], "terminal before child publication")
    unexpected = {p.name for p in root.iterdir()} - consumed
    require(not unexpected, f"unknown/partial/duplicated evidence: {sorted(unexpected)}")
    # Configure batches use the driver's existing host-CPU cap. The direct
    # backend then executes one tree command at a time; nested tests are not
    # another admission slot.
    for group, limit in (("configure", plan["logical_cpus"]), ("direct", 1)):
        admitted = []
        for name, task in tasks.items():
            selected = task["phase"] == "configure" if group == "configure" else plan["scheduler"] == "direct" and task["phase"] not in ("configure", "test")
            if selected:
                admitted.extend(((records[name]["start_us"], 1), (records[name]["end_us"], -1)))
        occupancy = 0
        for _, delta in sorted(admitted):
            occupancy += delta
            require(0 <= occupancy <= limit, f"impossible {group} overlap/quota")
    pool = defaultdict(list)
    for name, task in tasks.items():
        if task["pool_edge"]:
            pool[task["pool_edge"]].append(name)
    edges = []
    for key, names in pool.items():
        ordered = sorted(names, key=lambda name: records[name]["start_us"])
        for before, after in zip(ordered, ordered[1:]):
            require(records[before]["end_us"] <= records[after]["start_us"], f"overlap inside one pool edge: {key}")
        edges.append({"id": key, "tree": tasks[names[0]]["tree"], "phase": tasks[names[0]]["phase"],
                      "start_us": min(records[n]["start_us"] for n in names), "end_us": max(records[n]["end_us"] for n in names)})
    active, maximum, events, idle = 0, 0, [], []
    for edge in edges:
        events.extend(((edge["start_us"], 1), (edge["end_us"], -1)))
    previous = None
    for timestamp, delta in sorted(events):
        if previous is not None and timestamp > previous and active == 0:
            idle.append([previous, timestamp])
        active += delta
        require(0 <= active <= plan["outer_jobs"], "impossible pool overlap/quota")
        maximum = max(maximum, active)
        previous = timestamp
    ranking = rank(plan, trees, tasks, records)
    return {"schema": SCHEMA, "complete": True, "identity": plan["identity"], "scheduler": plan["scheduler"],
            "runner": {k: environment.get(k, environment.get(k.upper(), "unknown")) for k in ("RUNNER_OS", "RUNNER_ARCH", "RUNNER_NAME", "ImageOS", "ImageVersion", "BUSTER_CI_RUNNER", "BUSTER_CI_ZIG_CACHE_HIT")},
            "outer_jobs": plan["outer_jobs"], "logical_cpus": plan["logical_cpus"], "cpu_budget": plan["cpu_budget"],
            "max_observed_pool_overlap": maximum, "idle_gaps_us": idle, "trees": ranking,
            "timeline": sorted([dict(task=name, tree=tasks[name]["tree"], phase=tasks[name]["phase"], event=event, time_us=record[field], order=order)
                                for name, record in records.items() for order, (event, field) in enumerate((("enqueue", "ready_us"), ("admission", "start_us"), ("start", "child_start_us"), ("end", "end_us"), ("publication", "publication_start_us")))],
                               key=lambda e: (e["time_us"], e["order"], e["task"])),
            "events": sorted([dict(record, phase=tasks[name]["phase"], tree=tasks[name]["tree"]) for name, record in records.items()], key=lambda r: (r["start_us"], r["id"])),
            "predictions": predict(plan, edges), "errors": []}


def rank(plan, trees, tasks, records):
    output = []
    for tree_id, tree in trees.items():
        entries = {name: records[name] for name, task in tasks.items() if task["tree"] == tree_id}
        elapsed = dict.fromkeys(("configure", "build", "test", "post_test", "self_host", "evidence"), 0)
        for name, event in entries.items():
            task = tasks[name]
            phase = task["phase"]
            if phase == "validation":
                test = records[task_id(tree_id, "test", task["configuration"])]
                elapsed["build"] += test["start_us"] - event["child_start_us"]
                elapsed["post_test"] += event["end_us"] - test["end_us"]
            else:
                elapsed[{"clean": "build", "census": "post_test"}.get(phase, phase)] += event["end_us"] - event["child_start_us"]
        builds = [name for name in entries if tasks[name]["phase"] in ("build", "validation")]
        first = min(builds, key=lambda n: (entries[n]["start_us"], n))
        last = max(entries, key=lambda n: (entries[n]["end_us"], n))
        output.append(dict(tree, elapsed_us=elapsed, admission_us=entries[first]["start_us"],
                           enqueue_us=entries[first]["ready_us"], wait_us=entries[first]["start_us"] - entries[first]["ready_us"],
                           completion_us=entries[last]["end_us"], terminal_phase={"validation": "post_test", "census": "post_test", "clean": "build"}.get(tasks[last]["phase"], tasks[last]["phase"]),
                           largest_phase=max(elapsed, key=elapsed.get),
                           terminal_task=last, cpu_time="unknown", peak_rss="unknown"))
    for order, tree in enumerate(sorted(output, key=lambda t: (t["admission_us"], t["id"])), 1):
        tree["admission_order"] = order
    for order, tree in enumerate(sorted(output, key=lambda t: (t["completion_us"], t["id"])), 1):
        tree["completion_order"] = order
        tree["critical"] = order == len(output)
    return sorted(output, key=lambda t: (-t["completion_us"], t["id"]))


def predict(plan, edges):
    """Bounded, fixed-duration pool replay; predictions are never acceptance."""
    if plan["scheduler"] == "direct":
        return {"model": "serial direct tree execution; fixed durations; no predicted order benefit", "acceptance": False}
    tree_order = list(dict.fromkeys(edge["tree"] for edge in edges if edge["phase"] == "build"))
    require(len(tree_order) <= 8, "unbounded scheduling prediction")
    def replay(order):
        priorities = {tree: i for i, tree in enumerate(order)}
        waiting = sorted(edges, key=lambda e: (priorities[e["tree"]], e["phase"] != "build", e["id"]))
        running, built, now = [], set(), 0
        while waiting or running:
            ready = [edge for edge in waiting if edge["phase"] == "build" or edge["tree"] in built]
            for edge in ready[:max(0, plan["outer_jobs"] - len(running))]:
                waiting.remove(edge)
                running.append((now + edge["end_us"] - edge["start_us"], edge))
            require(bool(running), "prediction dependency deadlock")
            now = min(end for end, _ in running)
            finished = [edge for end, edge in running if end == now]
            running = [(end, edge) for end, edge in running if end != now]
            built.update(edge["tree"] for edge in finished if edge["phase"] == "build")
        return now
    # At most seven desktop compiler trees today; cap alternatives explicitly.
    alternatives = [(replay(order), list(order)) for order in itertools.islice(itertools.permutations(tree_order), 5040)]
    best = min(alternatives)
    return {"model": "fixed measured edge durations, dependency-ready declaration priority, unchanged pool/inner quotas",
            "acceptance": False, "current_order": tree_order, "current_model_us": replay(tree_order),
            "best_order": best[1], "best_model_us": best[0], "orders_evaluated": len(alternatives),
            "observed_pool_span_us": max(e["end_us"] for e in edges) - min(e["start_us"] for e in edges)}


def markdown(report):
    lines = ["## Desktop tree phases", "", "Complete: " + str(report["complete"]), ""]
    if not report["complete"]:
        lines += ["- " + error for error in report["errors"]]
    else:
        lines += ["| Tree | Configure s | Build s | Test s | Post-test s | Evidence s | Wait s | Admit / finish | Critical |", "|---|---:|---:|---:|---:|---:|---:|---|---|"]
        for tree in report["trees"]:
            duration = tree["elapsed_us"]
            values = " | ".join(f"{duration[p] / 1e6:.6f}" for p in ("configure", "build", "test", "post_test", "evidence"))
            lines.append(f"| {tree['id']} ({tree['compiler']}, {tree['configurations']}) | {values} | {tree['wait_us'] / 1e6:.6f} | {tree['admission_order']} / {tree['completion_order']} | {tree['terminal_phase'] if tree['critical'] else ''} |")
        lines += ["", "Launch-order predictions hold measured durations and quotas fixed; they are not performance acceptance.",
                  "CPU time and peak RSS: unknown. Post-test time includes measured native validation/command teardown after the test child."]
    return "\n".join(lines) + "\n"


def collect(environment, coverage):
    root = Path(environment["BUSTER_MATRIX_PHASE_OUTPUT"])
    report = {"schema": SCHEMA, "complete": False, "errors": []}
    try:
        report = analyze(root, coverage, environment)
        source = subprocess.check_output(["git", "rev-parse", "HEAD", "HEAD^{tree}"], text=True).splitlines()
        require(source == [report["identity"]["source_revision"], report["identity"]["source_tree"]], "checkout commit/tree mismatch")
    except (OSError, ValueError, KeyError, TypeError, AttributeError, subprocess.SubprocessError) as error:
        report = {"schema": SCHEMA, "complete": False, "errors": [str(error)]}
        print("MATRIX_PHASE_EVIDENCE_ERROR " + str(error))
    root.mkdir(parents=True, exist_ok=True)
    (root / "summary.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (root / "summary.md").write_text(markdown(report), encoding="utf-8")
    return report
