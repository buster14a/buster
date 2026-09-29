#!/usr/bin/env python3
"""Regression fixtures for the temporary #1162 hosted ancestor preflight.

Executes the production ANCESTORS block against a disposable proc/cgroup tree.
Only path lookup is redirected; no Docker, live cgroup, or service is touched.
The optional harness path supports failure-first reproduction on an old head.
"""

import contextlib
import io
import json
import sys
import tempfile
from pathlib import Path
from unittest.mock import patch


def main():
    harness = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name(
        "issue1162_exact_systemd_slice.sh")
    source = harness.read_text().split("<<'ANCESTORS'", 1)[1].split("\n", 1)[1]
    source = source.split("\nANCESTORS\n", 1)[0]
    compiled = compile(source, str(harness) + ":ANCESTORS", "exec")
    with tempfile.TemporaryDirectory(prefix="issue1162-ancestors-") as temporary:
        fixture = Path(temporary)
        root = fixture / "sys/fs/cgroup"
        ancestor = root / "docker.slice"
        boundary = ancestor / "guest.scope"
        leaf = boundary / "init.scope"
        leaf.mkdir(parents=True)
        proc = fixture / "proc/42"
        proc.mkdir(parents=True)
        (proc / "stat").write_text("42 (systemd) " + " ".join(["0"] * 19 + ["123"]) + "\n")
        cgroup = proc / "cgroup"
        cgroup.write_text("0::/docker.slice/guest.scope/init.scope\n")
        for directory in (root, ancestor, boundary):
            (directory / "cpuset.cpus.effective").write_text("0-3\n")
        for directory in (ancestor, boundary):
            (directory / "memory.max").write_text("8589934592\n")
            (directory / "pids.max").write_text("256\n")
        # The actual failure: init.scope has no controller files. It is not
        # an ancestor of the future service slice under the namespace root.
        identity = (boundary.stat().st_dev, boundary.stat().st_ino)

        def mapped_path(value):
            raw = str(value)
            assert raw == "/sys/fs/cgroup" or raw == "/proc/42", raw
            return fixture / raw.lstrip("/")

        def run_case(name, accepted, expected_identity=identity):
            output = io.StringIO()
            failure = None
            arguments = [str(harness), "42", *map(str, expected_identity)]
            with patch("pathlib.Path", mapped_path), patch.object(sys, "argv", arguments):
                try:
                    with contextlib.redirect_stdout(output):
                        exec(compiled, {"__name__": "__main__"})
                except (AssertionError, OSError, ValueError) as error:
                    failure = error
            assert accepted == (failure is None), (name, accepted, str(failure))
            if accepted:
                record = json.loads(output.getvalue().splitlines()[0])
                assert [row["path"] for row in record["ancestors"]] == [
                    str(root), str(ancestor), str(boundary)]
                assert "HOST_ANCESTOR_PREFLIGHT_PASS" in output.getvalue()
            print("PASS " + name)

        run_case("namespace-root-before-init-scope", True)
        (ancestor / "cpuset.cpus.effective").write_text("0-1\n")
        run_case("excluded-cpu", False)
        (ancestor / "cpuset.cpus.effective").write_text("0-3\n")
        (ancestor / "memory.max").write_text("8589934591\n")
        run_case("ancestor-memory-budget", False)
        (ancestor / "memory.max").write_text("8589934592\n")
        (boundary / "pids.max").write_text("255\n")
        run_case("namespace-pids-budget", False)
        (boundary / "pids.max").write_text("256\n")
        (boundary / "memory.max").unlink()
        run_case("missing-nonroot-controller", False)
        (boundary / "memory.max").write_text("max\n")
        run_case("unlimited-memory", True)
        run_case("namespace-root-not-in-ancestry", False, (identity[0], 0))
        run_case("nonprivate-host-root", False, (root.stat().st_dev, root.stat().st_ino))
        cgroup.write_text("0::/docker.slice/../guest.scope/init.scope\n")
        run_case("path-traversal", False)
    print("ANCESTOR_PREFLIGHT_SELF_TEST_PASS cases=9")


if __name__ == "__main__":
    main()
