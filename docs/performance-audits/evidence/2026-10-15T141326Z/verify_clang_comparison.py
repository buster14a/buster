import argparse
import json
from pathlib import Path
import re
import shlex
import sys

base = Path(__file__).parent
parser = argparse.ArgumentParser(description="Independently replay the two hosted Clang comparison artifacts.")
parser.add_argument("--repo-root", type=Path)
parser.add_argument("artifacts", nargs="+", type=Path)
arguments = parser.parse_args()
candidates = [arguments.repo_root] if arguments.repo_root else [base / "buster", *Path(__file__).resolve().parents]
repository = next(root for root in candidates if (root / "tools/analyzer_worker_budget.py").is_file())
sys.path.insert(0, str(repository / "tools"))
from analyzer_worker_budget import Record, digest, fields, inventory, regular, require

OPTIONAL_WARNINGS = {
    "-Wexperimental-lifetime-safety", "-Whigher-precision-for-complex-division",
    "-Wimplicit-void-ptr-cast", "-Wshift-bool", "-Wthread-safety", "-Wunique-object-duplication",
}


def read_trial(folder):
    evidence = next(folder.rglob("driver-provenance.json")).parent
    provenance = json.loads(regular(evidence / "driver-provenance.json"))
    require(provenance["complete"], "incomplete driver provenance")
    require(not regular(evidence / "source-status-before.txt"), "dirty source before run")
    require(not regular(evidence / "source-status-after.txt"), "dirty source after run")
    order = regular(evidence / "order.txt").decode().splitlines()
    require(order in (["image", "stable"], ["stable", "image"]), "invalid role order")
    roles = {}
    for role in ("image", "stable"):
        control = regular(evidence / f"{role}-self-test.log").decode()
        verdicts = [fields(line, "ANALYZE_SELF_TEST_RESULT") for line in control.splitlines()
                    if line.startswith("ANALYZE_SELF_TEST_RESULT ")]
        require(len(verdicts) == 1 and verdicts[0]["status"] == "pass" and verdicts[0]["failures"] == "0",
                f"failed/missing {role} control verdict")
        require(regular(evidence / f"{role}-exit.txt") == b"analysis=0\naggregate=0\n", f"{role} execution failed")
        arm = evidence / role
        data, _, _, result_root, selected = inventory(arm / "manifest.txt")
        config, clang, shards, timeout, count, excluded, database, units = selected
        require((config, shards, timeout, count, excluded) == (b"Release", 8, 600, 118, 118), "changed analysis scope")
        compiler_record = regular(evidence / f"{role}-clang.sha256").decode().strip().split(maxsplit=1)
        require(len(compiler_record) == 2 and compiler_record[1].strip() == clang.decode(), "compiler identity/path mismatch")
        require(all(unit[3][0] == clang for unit in units), "a TU used the wrong analyzer compiler")
        logs = {}
        rows = []
        for shard in range(shards):
            directory = arm / f"shard-{shard}"
            report = Record(regular(directory / "result.txt"))
            require(report.line() == b"BUSTER_CLANG_ANALYZE_RESULT_V1", "wrong result schema")
            require(report.line().decode() == digest(data) and report.number() == shard, "wrong manifest/shard binding")
            row_count, wall, rss = (report.number() for _ in range(3))
            expected = {i for i, unit in enumerate(units) if unit[0] == shard}
            seen = set()
            for _ in range(row_count):
                unit, status, elapsed = (report.number() for _ in range(3))
                log_hash = report.line().decode()
                require(unit in expected and unit not in seen and status == 0, "failed/duplicate/unexpected TU")
                diagnostic = regular(directory / f"unit-{unit}.log")
                require(digest(diagnostic) == log_hash, "diagnostic hash mismatch")
                require(not diagnostic, "unexpected nonempty passing diagnostic")
                seen.add(unit)
                logs[unit] = log_hash
                rows.append({"unit": unit, "elapsed_us": elapsed, "source": units[unit][2].decode()})
            report.done()
            require(seen == expected, "omitted TU")
            require({p.name for p in directory.iterdir()} == {"result.txt"} | {f"unit-{i}.log" for i in expected},
                    "unexpected shard evidence")
        analysis = regular(evidence / f"{role}-analysis.log").decode()
        runs = [fields(line, "ANALYZE_RUN") for line in analysis.splitlines() if line.startswith("ANALYZE_RUN ")]
        require(len(runs) == 1, "missing/duplicate full run")
        run = runs[0]
        require(run["status"] == "pass" and run["jobs"] == "2" and run["results"] == result_root.decode(), "invalid run")
        require(run["process_tree_status"] == "complete", "incomplete process-tree sampling")
        aggregate = regular(evidence / f"{role}-aggregate.log").decode()
        aggregates = [fields(line, "ANALYZE_AGGREGATE") for line in aggregate.splitlines()
                      if line.startswith("ANALYZE_AGGREGATE ")]
        require(len(aggregates) == 1 and aggregates[0]["status"] == "pass" and
                aggregates[0]["eligible"] == aggregates[0]["checked"] == str(count), "failed independent aggregate")
        normalized = []
        warning_sets = []
        for shard, module, source, command in units:
            arguments = [a.decode().replace(f"build/analyzer-{role}/", "build/analyzer-<role>/") for a in command]
            warning_sets.append({a for a in arguments if a in OPTIONAL_WARNINGS})
            normalized.append((shard, module.decode(), source.decode(), tuple(
                "<clang>" if i == 0 else a for i, a in enumerate(arguments)
                if not a.startswith(("-DBUSTER_HOST_C_COMPILER=", "-DBUSTER_HOST_C_RESOURCE_INCLUDE="))
                and a not in OPTIONAL_WARNINGS)))
        require(all(s == warning_sets[0] for s in warning_sets), "optional warnings differ within a compiler's inventory")
        cpu = regular(evidence / f"{role}-time.txt").decode()
        def time_value(label):
            match = re.search(r"^\s*" + re.escape(label) + r": (.+)$", cpu, re.MULTILINE)
            require(match is not None, "missing GNU time field " + label)
            return match.group(1)
        require(time_value("Exit status") == "0", "time wrapper records failed run")
        roles[role] = {
            "elapsed_seconds": int(run["elapsed_us"]) / 1e6,
            "cpu_seconds": float(time_value("User time (seconds)")) + float(time_value("System time (seconds)")),
            "max_rss_kib": int(time_value("Maximum resident set size (kbytes)")),
            "sampled_tree_rss_bytes": int(run["sampled_peak_tree_rss_bytes"]),
            "version": regular(evidence / f"{role}-clang.txt").decode().strip(),
            "compiler_hash": compiler_record[0],
            "default_checkers": regular(evidence / f"{role}-default-checkers.txt").decode(),
            "optional_warnings": sorted(warning_sets[0]),
            "control_checks": int(verdicts[0]["checks"]),
            "units": sorted(rows, key=lambda r: r["unit"]),
            "normalized_commands": normalized,
        }
        checker_commands = [line for line in roles[role]["default_checkers"].splitlines() if '"-cc1"' in line]
        require(len(checker_commands) == 1, "missing/duplicate checker command")
        roles[role]["checker_groups"] = [arg for arg in shlex.split(checker_commands[0]) if arg.startswith("-analyzer-checker=")]
    require(roles["image"]["normalized_commands"] == roles["stable"]["normalized_commands"], "semantic command mismatch")
    require(roles["image"]["checker_groups"] == roles["stable"]["checker_groups"], "default checker groups changed")
    for role in roles:
        del roles[role]["normalized_commands"]
    return {
        "source": provenance["revision"], "tree": provenance["tree"], "order": order,
        "host_before": json.loads(regular(evidence / "host-before.json")),
        "host_after": json.loads(regular(evidence / "host-after.json")),
        "roles": roles,
        "analysis_improvement_percent": 100 * (1 - roles["stable"]["elapsed_seconds"] / roles["image"]["elapsed_seconds"]),
    }


if __name__ == "__main__":
    result = [read_trial(folder) for folder in arguments.artifacts]
    if len(result) == 2:
        require(result[0]["source"] == result[1]["source"] and result[0]["tree"] == result[1]["tree"],
                "trials used different source identities")
        require(result[0]["order"] == list(reversed(result[1]["order"])), "trial orders are not opposite")
        for role in ("image", "stable"):
            require(result[0]["roles"][role]["compiler_hash"] == result[1]["roles"][role]["compiler_hash"],
                    "compiler binary changed between trials")
            require(result[0]["roles"][role]["version"] == result[1]["roles"][role]["version"],
                    "compiler version changed between trials")
        require(result[0]["roles"]["image"]["compiler_hash"] != result[0]["roles"]["stable"]["compiler_hash"],
                "both roles used the same compiler")
    print(json.dumps(result, indent=2))
