"""Temporary hosted bridge: actual C output through authenticated reader tests."""
import copy
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from github_ci_time_test import CompilerBenchmarkInventoryTests

reports = [json.loads(line) for line in Path(sys.argv[1]).read_text().splitlines()]
assert len(reports) == 18, len(reports)
for report in reports:
    test = CompilerBenchmarkInventoryTests()
    test.setUp()
    test.recovered_announcement("superseded")
    payload = json.dumps(report).replace("a4ea18116d1dd5d03ecf7287234fe61f4fb40267", test.HEAD)
    payload = payload.replace("38297937496", "38260981679").replace("38297977458", "38261019120")
    test.checks[-1].update(json.loads(payload))
    test.jobs[-1]["conclusion"] = report["conclusion"]
    summary = report["output"]["summary"]
    outcome = summary.split("finished with conclusion `", 1)[1].split("`", 1)[0]
    for suffix in ("", "/attempts/1"):
        test.benchmark_reads["actions/runs/38261019120" + suffix]["conclusion"] = outcome
    result = test.gate()
    assert result["success"], result["errors"]
    assert len(result["jobs"]) == 25
    assert result["reconciled_checks"][-1]["check"]["conclusion"] == report["conclusion"]
    # Every generated form still refuses absent controller authentication.
    test.benchmark_reads["actions/runs/38261177183"]["conclusion"] = "failure"
    assert not test.gate()["success"]
print("NATIVE_READER_COMPOSITION reports=18 accepted=18 forged_controller_refused=18")
