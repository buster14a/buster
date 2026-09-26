#!/usr/bin/env python3
"""Run a bounded initializer-root diagnostic against frozen Buster compilers.

The supplied workflow builds the pristine Release ide before this runner
starts. This runner saves that binary, applies the sibling probe.patch, builds
and saves the instrumented ide, then restores the source patch before compiling
any inputs. Every compiler invocation uses the same source path and compile
flags for its baseline/probe pair. This records identities and counters only;
it does not measure or claim performance.
"""

import argparse
import atexit
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


PINNED_COMMIT = "ade6ac4b6ecb21f30b61b656439bac476c145e2f"
PINNED_TREE = "4c5306221fdb22fccc929b55e333163742de17d0"
PACKET_PATHS = {
    ".github/workflows/initializer-root-census.yml",
    "tools/research/initializer-roots/README.md",
    "tools/research/initializer-roots/probe.patch",
    "tools/research/initializer-roots/run_probe.py",
}
PACKET_PATHS = {
    ".github/workflows/initializer-root-census.yml",
    "tools/research/initializer-roots/README.md",
    "tools/research/initializer-roots/probe.patch",
    "tools/research/initializer-roots/run_probe.py",
}


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_metrics(path):
    values = {}
    if not path.is_file():
        return values
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key] = value
    return values


def parse_init_root_summary(stderr_text):
    rows = [line for line in stderr_text.splitlines() if line.startswith("INIT_ROOT_SUMMARY\t")]
    if len(rows) != 1:
        return None, f"expected exactly one INIT_ROOT_SUMMARY row, found {len(rows)}"
    fields = {}
    for item in rows[0].split("\t")[2:]:
        if "=" not in item:
            continue
        key, value = item.split("=", 1)
        if key in fields:
            return None, f"duplicate summary key {key}"
        try:
            fields[key] = int(value)
        except ValueError:
            return None, f"non-integer summary value for {key}"
    required = {
        "member_hits", "member_entities", "member_only_entities", "hits_suppressed",
        "current_functions", "replay_functions", "current_body_tokens", "replay_body_tokens",
        "newly_dead", "dead_rows_suppressed", "replay_extra",
    }
    missing = sorted(required - fields.keys())
    if missing:
        return None, f"missing summary keys: {', '.join(missing)}"
    return fields, None


def safe_name(value):
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_") or "command"


class Runner:
    def __init__(self, repo, out):
        self.repo = repo
        self.out = out
        self.logs = out / "command-logs"
        self.logs.mkdir(parents=True, exist_ok=True)
        self.commands = []
        self.errors = []
        self.probe_rows = []

    def command(self, label, argv, cwd=None, env=None, expect_success=True):
        number = len(self.commands) + 1
        stem = f"{number:04d}-{safe_name(label)}"
        stdout_path = self.logs / f"{stem}.stdout"
        stderr_path = self.logs / f"{stem}.stderr"
        args = [str(item) for item in argv]
        working_directory = Path(cwd or self.repo).resolve()
        environment = None if env is None else dict(env)
        stdout_text = ""
        stderr_text = ""
        returncode = None
        exception = None
        print(f"[RUN] {label}: {' '.join(args)}", flush=True)
        try:
            process = subprocess.run(
                args,
                cwd=working_directory,
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                errors="replace",
                check=False,
                timeout=900,
            )
            returncode = process.returncode
            stdout_text = process.stdout
            stderr_text = process.stderr
        except OSError as error:
            exception = f"{type(error).__name__}: {error}"
            stderr_text = exception + "\n"
        except subprocess.TimeoutExpired as error:
            exception = "TimeoutExpired: command exceeded 900 seconds"
            stdout_text = error.stdout.decode("utf-8", "replace") if isinstance(error.stdout, bytes) else (error.stdout or "")
            stderr_text = error.stderr.decode("utf-8", "replace") if isinstance(error.stderr, bytes) else (error.stderr or "")
            stderr_text += exception + "\n"

        stdout_path.write_text(stdout_text, encoding="utf-8")
        stderr_path.write_text(stderr_text, encoding="utf-8")
        entry = {
            "id": number,
            "label": label,
            "argv": args,
            "cwd": str(working_directory),
            "environment_overrides": environment or {},
            "returncode": returncode,
            "exception": exception,
            "stdout_path": str(stdout_path.relative_to(self.out)),
            "stderr_path": str(stderr_path.relative_to(self.out)),
            "stdout_sha256": sha256(stdout_path),
            "stderr_sha256": sha256(stderr_path),
        }
        self.commands.append(entry)
        if expect_success and returncode != 0:
            self.errors.append(f"{label} exited {returncode}: {args}")
        print(f"[DONE] {label}: exit={returncode}", flush=True)
        return entry, stdout_text, stderr_text

    def hash_file(self, label, path):
        path = Path(path)
        if not path.is_file():
            return {"path": str(path), "exists": False}
        blob, out, _ = self.command(label, ["git", "hash-object", str(path)], cwd=self.repo)
        return {
            "path": str(path),
            "exists": True,
            "sha256": sha256(path),
            "git_blob": out.strip() if blob["returncode"] == 0 else None,
        }

    def compile_pair(self, label, source, flags, baseline, probe, nm_path=None,
                     expected_functions=None, expected_summary=None, runtime=False, linker=None):
        source = Path(source).resolve()
        case_dir = self.out / "cases" / safe_name(label)
        case_dir.mkdir(parents=True, exist_ok=True)
        source_hash = sha256(source) if source.is_file() else None
        pair = {
            "label": label,
            "source": str(source),
            "source_sha256": source_hash,
            "flags": list(flags),
            "expected_emitted_functions": expected_functions,
            "baseline": {},
            "probe": {},
        }
        if not source.is_file():
            self.errors.append(f"missing source for {label}: {source}")
            pair["error"] = "missing source"
            return pair

        for flavor, compiler in (("baseline", baseline), ("probe", probe)):
            object_path = case_dir / f"{flavor}.o"
            metrics_path = case_dir / f"{flavor}.metrics"
            for stale in (object_path, metrics_path):
                if stale.exists():
                    stale.unlink()
            argv = [str(compiler), "cc", *flags,
                    f"-fsource-metrics={metrics_path}", "-c", str(source), "-o", str(object_path)]
            entry, _, stderr_text = self.command(f"{label}-{flavor}-compile", argv, cwd=self.repo)
            if flavor == "baseline" and "INIT_ROOT_" in stderr_text:
                self.errors.append(f"{label}: baseline unexpectedly contains probe records")
            if flavor == "probe":
                marker_lines = [line for line in stderr_text.splitlines() if line.startswith("INIT_ROOT_")]
                summary_lines = [line for line in marker_lines if line.startswith("INIT_ROOT_SUMMARY")]
                summary, summary_error = parse_init_root_summary(stderr_text)
                self.probe_rows.append({
                    "case": label,
                    "command_id": entry["id"],
                    "stderr_path": entry["stderr_path"],
                    "rows": marker_lines,
                })
                if not marker_lines:
                    self.errors.append(f"{label}: probe compiler emitted no INIT_ROOT_ stderr rows")
                if not summary_lines:
                    self.errors.append(f"{label}: probe compiler emitted no INIT_ROOT_SUMMARY footer")
                if summary_error:
                    self.errors.append(f"{label}: {summary_error}")
                elif summary is not None:
                    required_mark_rows = min(summary["member_hits"], 128)
                    required_dead_rows = min(summary["newly_dead"], 128)
                    mark_rows = [line for line in marker_lines if line.startswith("INIT_ROOT_MARK\t")]
                    dead_rows = [line for line in marker_lines if line.startswith("INIT_ROOT_DEAD\t")]
                    if len(mark_rows) != required_mark_rows:
                        self.errors.append(f"{label}: INIT_ROOT_MARK rows do not match uncapped hit count")
                    if len(dead_rows) != required_dead_rows:
                        self.errors.append(f"{label}: INIT_ROOT_DEAD rows do not match uncapped dead count")
                    if summary["hits_suppressed"] != max(0, summary["member_hits"] - 128):
                        self.errors.append(f"{label}: hits_suppressed is inconsistent")
                    if summary["dead_rows_suppressed"] != max(0, summary["newly_dead"] - 128):
                        self.errors.append(f"{label}: dead_rows_suppressed is inconsistent")
                    if summary["current_functions"] - summary["replay_functions"] != summary["newly_dead"] - summary["replay_extra"]:
                        self.errors.append(f"{label}: summary function totals are inconsistent")
                    if summary["current_body_tokens"] < summary["replay_body_tokens"]:
                        self.errors.append(f"{label}: replay body-token total exceeds current total")
                    if summary["replay_extra"] != 0:
                        self.errors.append(f"{label}: diagnostic replay retained functions absent from current roots")
                    if summary["member_hits"] <= 128:
                        marks = [line.split("\t") for line in mark_rows]
                        entities = {fields[4] for fields in marks if len(fields) >= 9}
                        member_only = {fields[4] for fields in marks if len(fields) >= 9 and fields[6] == "0"}
                        if len(entities) != summary["member_entities"]:
                            self.errors.append(f"{label}: member_entities disagrees with MARK rows")
                        if len(member_only) != summary["member_only_entities"]:
                            self.errors.append(f"{label}: member_only_entities disagrees with MARK rows")
                    if expected_summary:
                        for key, expected in expected_summary.items():
                            if summary.get(key) != expected:
                                self.errors.append(f"{label}: {key}={summary.get(key)}; expected {expected}")
            output = {
                "command_id": entry["id"],
                "returncode": entry["returncode"],
                "object": str(object_path),
                "object_sha256": sha256(object_path) if object_path.is_file() else None,
                "metrics": str(metrics_path),
                "metrics_sha256": sha256(metrics_path) if metrics_path.is_file() else None,
                "source_metrics": parse_metrics(metrics_path),
            }
            if flavor == "probe" and summary_error is None and summary is not None:
                output["init_root_summary"] = summary
            pair[flavor] = output
            if entry["returncode"] != 0 or not object_path.is_file():
                self.errors.append(f"{label}: {flavor} compile did not produce an object")
            if not metrics_path.is_file() or not output["source_metrics"]:
                self.errors.append(f"{label}: {flavor} compile did not produce source metrics")
            if flavor == "baseline" and any(line.startswith("INIT_ROOT_") for line in stderr_text.splitlines()):
                self.errors.append(f"{label}: baseline stderr unexpectedly contains INIT_ROOT_ rows")

        base_hash = pair["baseline"].get("object_sha256")
        probe_hash = pair["probe"].get("object_sha256")
        pair["object_sha256_identical"] = bool(base_hash and probe_hash and base_hash == probe_hash)
        base_metrics = pair["baseline"].get("source_metrics", {})
        probe_metrics = pair["probe"].get("source_metrics", {})
        pair["source_metrics_identical"] = bool(base_metrics and probe_metrics and base_metrics == probe_metrics)
        if not pair["object_sha256_identical"]:
            self.errors.append(f"{label}: baseline/probe object SHA256 mismatch")
        if not pair["source_metrics_identical"]:
            self.errors.append(f"{label}: baseline/probe source metrics differ")

        if expected_functions is not None and nm_path:
            for flavor in ("baseline", "probe"):
                object_path = Path(pair[flavor]["object"])
                if not object_path.is_file():
                    pair[flavor]["emitted_functions"] = None
                    continue
                entry, stdout_text, _ = self.command(
                    f"{label}-{flavor}-nm",
                    [str(nm_path), "--defined-only", "--format=posix", "-S", str(object_path)],
                    cwd=self.repo,
                )
                if entry["returncode"] != 0:
                    self.errors.append(f"{label}: nm failed for {flavor} object")
                    pair[flavor]["emitted_functions"] = None
                    continue
                names = []
                for line in stdout_text.splitlines():
                    fields = line.split()
                    if len(fields) < 2 or fields[1] not in ("t", "T"):
                        continue
                    name = fields[0].lstrip("_")
                    if name == "trigger" or re.fullmatch(r"chain_[0-9]+", name):
                        names.append(name)
                names.sort()
                pair[flavor]["emitted_functions"] = {
                    "count": len(names),
                    "names": names,
                    "expected_count": expected_functions,
                    "matches_expected": len(names) == expected_functions,
                    "nm_command_id": entry["id"],
                }
                if len(names) != expected_functions:
                    self.errors.append(
                        f"{label}: {flavor} emitted {len(names)} trigger/chain functions; expected {expected_functions}"
                    )

        if runtime and linker:
            for flavor in ("baseline", "probe"):
                object_path = Path(pair[flavor]["object"])
                if not object_path.is_file():
                    self.errors.append(f"{label}: {flavor} object unavailable for native execution")
                    continue
                exe_path = case_dir / f"{flavor}.exe"
                if exe_path.exists():
                    exe_path.unlink()
                link_entry, _, _ = self.command(
                    f"{label}-{flavor}-link",
                    [str(linker), "-no-pie", str(object_path), "-o", str(exe_path)],
                    cwd=self.repo,
                )
                execution = None
                if link_entry["returncode"] == 0 and exe_path.is_file():
                    execution, _, _ = self.command(f"{label}-{flavor}-run", [str(exe_path)], cwd=self.repo)
                    if execution["returncode"] != 0:
                        self.errors.append(f"{label}: {flavor} native executable returned {execution['returncode']}")
                else:
                    self.errors.append(f"{label}: {flavor} native link failed")
                pair[flavor]["runtime"] = {
                    "link_command_id": link_entry["id"],
                    "executable": str(exe_path),
                    "executable_sha256": sha256(exe_path) if exe_path.is_file() else None,
                    "run_command_id": execution["id"] if execution else None,
                    "returncode": execution["returncode"] if execution else None,
                    "expected_returncode": 0,
                }
        elif runtime:
            pair["runtime"] = {"status": "unavailable", "reason": "host C linker unavailable"}
        return pair


def chain_fixture(length, mode):
    rows = ["/* Deterministic initializer-root fixture; no external headers. */"]
    rows.extend(f"static int chain_{index}(int value);" for index in range(length))
    for index in range(length):
        if index + 1 < length:
            rows.append(f"static int chain_{index}(int value) {{ return (chain_{index + 1}(value) + 3) - 3; }}")
        else:
            rows.append(f"static int chain_{index}(int value) {{ return ((value * 3) + 5) - 5; }}")
    rows.append("static int trigger(int value) { return chain_0(value); }")

    if mode == "field_trigger":
        rows += ["struct Record { int trigger; };", "struct Record live = {.trigger = 7};", "int main(void) { return live.trigger != 7; }"]
    elif mode == "field_payload":
        rows += ["struct Record { int payload; };", "struct Record live = {.payload = 7};", "int main(void) { return live.payload != 7; }"]
    elif mode == "function_pointer":
        rows += ["int (*live)(int) = trigger;", "int main(void) { return live(0); }"]
    elif mode == "constructor":
        rows += [
            "int (*live)(int);",
            "__attribute__((constructor)) static void initialize(void) { live = trigger; }",
            "int main(void) { return live(0); }",
        ]
    elif mode == "alias":
        rows += [
            'int root_alias(int value) __attribute__((alias("trigger")));',
            "int (*live)(int) = root_alias;",
            "int main(void) { return live(0); }",
        ]
    else:
        raise ValueError(f"unknown fixture mode: {mode}")
    return "\n".join(rows) + "\n"


def resolve_driver(value, repo):
    supplied = Path(value).expanduser()
    if supplied.is_absolute():
        return supplied.resolve()
    repo_candidate = (repo / supplied).resolve()
    if repo_candidate.exists():
        return repo_candidate
    cwd_candidate = supplied.resolve()
    if cwd_candidate.exists():
        return cwd_candidate
    found = shutil.which(value)
    return Path(found).resolve() if found else repo_candidate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", required=True, help="native build driver path")
    parser.add_argument("--out", required=True, help="fresh or reusable evidence directory")
    parser.add_argument("--sqlite", help="extracted SQLite amalgamation directory")
    parser.add_argument("--cjson", help="pinned cJSON source directory")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[3]
    out = Path(args.out).expanduser().resolve()
    out.mkdir(parents=True, exist_ok=True)
    runner = Runner(repo, out)
    result = {
        "schema": "initializer-roots-probe-v1",
        "scope": "observational identities and counters only; no timings or performance claims",
        "repo": str(repo),
        "pinned_commit": PINNED_COMMIT,
        "pinned_tree": PINNED_TREE,
        "driver": None,
        "baseline_compiler": None,
        "probe_compiler": None,
        "patch": None,
        "source_cases": [],
        "probe_log": "probe.log",
        "errors": runner.errors,
        "commands": runner.commands,
        "status": "incomplete",
    }
    def checkpoint():
        (out / "results.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    atexit.register(checkpoint)
    probe_patch = Path(__file__).resolve().with_name("probe.patch")
    baseline_source = repo / "build" / "Release" / "ide"
    baseline_frozen = out / "binaries" / "baseline-ide"
    probe_frozen = out / "binaries" / "probe-ide"
    patch_applied = False
    patch_restored = False

    try:
        (out / "binaries").mkdir(parents=True, exist_ok=True)
        driver = resolve_driver(args.driver, repo)
        result["driver"] = {
            "path": str(driver),
            "sha256": sha256(driver) if driver.is_file() else None,
        }
        if not driver.is_file():
            runner.errors.append(f"native driver does not exist: {driver}")

        head, head_out, _ = runner.command("git-head", ["git", "rev-parse", "HEAD"])
        tree, tree_out, _ = runner.command("git-tree", ["git", "rev-parse", "HEAD^{tree}"])
        status, status_out, _ = runner.command("git-status", ["git", "status", "--short", "--branch"])
        result["git"] = {
            "commit": head_out.strip() if head["returncode"] == 0 else None,
            "tree": tree_out.strip() if tree["returncode"] == 0 else None,
            "status_before_probe": status_out,
        }
        runner.command("base-is-ancestor", ["git", "merge-base", "--is-ancestor", PINNED_COMMIT, "HEAD"])
        _, base_tree, _ = runner.command("pinned-tree", ["git", "rev-parse", PINNED_COMMIT + "^{tree}"])
        _, changed, _ = runner.command("packet-only-diff", ["git", "diff", "--name-only", PINNED_COMMIT])
        if base_tree.strip() != PINNED_TREE or set(changed.splitlines()) - PACKET_PATHS:
            runner.errors.append("diagnostic checkout differs from the pinned production source")
        if runner.errors:
            raise RuntimeError("source/driver identity preflight failed")
        if not baseline_source.is_file():
            runner.errors.append(f"baseline IDE is missing; build it first: {baseline_source}")
        else:
            shutil.copy2(baseline_source, baseline_frozen)
            result["baseline_compiler"] = {
                "path": str(baseline_frozen),
                "sha256": sha256(baseline_frozen),
                "source_revision": result["git"]["commit"],
            }
        if not probe_patch.is_file():
            runner.errors.append(f"probe patch is missing: {probe_patch}")
        else:
            result["patch"] = {"path": str(probe_patch), "sha256": sha256(probe_patch)}
            patch_text = probe_patch.read_text(encoding="utf-8", errors="replace")
            patch_targets = sorted({
                line[len("+++ b/"):].strip()
                for line in patch_text.splitlines()
                if line.startswith("+++ b/") and line[len("+++ b/"):].strip() != "/dev/null"
            })
            result["patch"]["targets"] = patch_targets
            result["patch"]["target_blobs_before"] = [runner.hash_file(f"patch-target-before-{index}", repo / path)
                                                           for index, path in enumerate(patch_targets)]
            if baseline_frozen.is_file() and driver.is_file():
                check, _, _ = runner.command("probe-patch-check", ["git", "apply", "--check", str(probe_patch)])
                if check["returncode"] == 0:
                    apply_entry, _, _ = runner.command("probe-patch-apply", ["git", "apply", str(probe_patch)])
                    patch_applied = apply_entry["returncode"] == 0
                if patch_applied:
                    build, _, _ = runner.command(
                        "probe-incremental-release-build",
                        [str(driver), "build", "--config", "Release", "-t", "ide"],
                    )
                    built = repo / "build" / "Release" / "ide"
                    if build["returncode"] == 0 and built.is_file():
                        shutil.copy2(built, probe_frozen)
                        result["probe_compiler"] = {
                            "path": str(probe_frozen),
                            "sha256": sha256(probe_frozen),
                            "source_revision": result["git"]["commit"],
                            "patch_sha256": result["patch"]["sha256"],
                        }
                    else:
                        runner.errors.append("incremental probe build did not produce build/Release/ide")
            else:
                runner.errors.append("probe build prerequisites are unavailable")
    except Exception as error:
        runner.errors.append(f"setup exception: {type(error).__name__}: {error}")
    finally:
        if patch_applied:
            restore, _, _ = runner.command("probe-patch-restore", ["git", "apply", "-R", str(probe_patch)])
            patch_restored = restore["returncode"] == 0
            if not patch_restored:
                runner.errors.append("failed to restore probe.patch in finally block")
        else:
            patch_restored = True

    result["patch_restored"] = patch_restored
    if probe_patch.is_file() and result.get("patch"):
        targets = result["patch"].get("targets", [])
        result["patch"]["target_blobs_after"] = [runner.hash_file(f"patch-target-after-{index}", repo / path)
                                                          for index, path in enumerate(targets)]
        before = result["patch"].get("target_blobs_before", [])
        after = result["patch"].get("target_blobs_after", [])
        result["patch"]["targets_restored"] = all(
            left.get("sha256") == right.get("sha256") and left.get("git_blob") == right.get("git_blob")
            for left, right in zip(before, after)
        ) and len(before) == len(after)
        if not result["patch"]["targets_restored"]:
            runner.errors.append("probe patch targets differ from their pre-probe file/blob hashes")

    if patch_restored and baseline_frozen.is_file() and probe_frozen.is_file():
        versions = {}
        for label, compiler in (("baseline", baseline_frozen), ("probe", probe_frozen)):
            help_entry, help_out, help_err = runner.command(f"{label}-compiler-help", [str(compiler), "--help"], expect_success=False)
            versions[label] = {
                "revision": PINNED_COMMIT,
                "binary_sha256": sha256(compiler),
                "help_command_id": help_entry["id"],
                "help_stdout_sha256": help_entry["stdout_sha256"],
                "help_stderr_sha256": help_entry["stderr_sha256"],
                "help_exit_code": help_entry["returncode"],
            }
        result["compiler_versions"] = versions

        linker_name = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
        linker = Path(linker_name).resolve() if linker_name else None
        if linker:
            version_entry, _, _ = runner.command("host-linker-version", [str(linker), "--version"], expect_success=False)
            result["host_linker"] = {
                "path": str(linker),
                "sha256": sha256(linker),
                "version_command_id": version_entry["id"],
            }
        else:
            result["host_linker"] = {"available": False}
            runner.errors.append("host linker is required for runtime controls")

        nm_name = shutil.which("nm")
        nm_path = Path(nm_name).resolve() if nm_name else None
        if nm_path:
            nm_version, _, _ = runner.command("nm-version", [str(nm_path), "--version"], expect_success=False)
            result["nm"] = {"path": str(nm_path), "version_command_id": nm_version["id"]}
        else:
            result["nm"] = {"available": False}
            runner.errors.append("nm is required for independent emitted-function controls")

        fixture_root = out / "fixtures"
        fixture_root.mkdir(parents=True, exist_ok=True)
        modes = ("field_trigger", "field_payload", "function_pointer", "constructor", "alias")
        expected_by_mode = {
            "field_trigger": lambda n: n + 1,
            "field_payload": lambda n: 0,
            "function_pointer": lambda n: n + 1,
            "constructor": lambda n: n + 1,
            "alias": lambda n: n + 1,
        }
        for count in (1, 32, 256):
            for mode in modes:
                source = fixture_root / f"chain-{count}-{mode}.c"
                source.write_text(chain_fixture(count, mode), encoding="utf-8")
                result["source_cases"].append(runner.compile_pair(
                    f"chain-{count}-{mode}", source, ["-g0"], baseline_frozen, probe_frozen,
                    nm_path=nm_path, expected_functions=expected_by_mode[mode](count),
                    expected_summary={
                        "member_hits": int(mode == "field_trigger"),
                        "current_functions": 1 if mode == "field_payload" else count + (3 if mode == "constructor" else 2),
                        "replay_functions": 1 if mode in ("field_trigger", "field_payload") else count + (3 if mode == "constructor" else 2),
                        "newly_dead": count + 1 if mode == "field_trigger" else 0,
                    },
                    runtime=mode in ("field_trigger", "field_payload", "function_pointer", "constructor", "alias"),
                    linker=linker,
                ))

        fixed_cases = [
            ("ide-unity", repo / "src/buster/apps/ide/ide.c",
             ["-Isrc", "-Ibuild/generated", "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0", "-g"]),
            ("basic_c_operations", repo / "tests/basic_c_operations.c", ["-g"]),
        ]
        c_abi = repo / "tests/c_abi_cfuncs.c"
        if c_abi.is_file():
            fixed_cases.append(("c_abi_cfuncs", c_abi, ["-g"]))
        if args.sqlite:
            sqlite_dir = Path(args.sqlite).expanduser().resolve()
            fixed_cases.append(("sqlite3", sqlite_dir / "sqlite3.c", [
                "-g0", "-O2", "-I" + str(sqlite_dir), "-DSQLITE_THREADSAFE=1",
                "-DSQLITE_ENABLE_MATH_FUNCTIONS", "-DSQLITE_ENABLE_COLUMN_METADATA",
            ]))
        if args.cjson:
            cjson_dir = Path(args.cjson).expanduser().resolve()
            fixed_cases.append(("cJSON", cjson_dir / "cJSON.c", ["-g0", "-I" + str(cjson_dir)]))
        for label, source, flags in fixed_cases:
            result["source_cases"].append(runner.compile_pair(
                label, source, flags, baseline_frozen, probe_frozen,
            ))
    else:
        runner.errors.append("source comparisons skipped because binaries are unavailable or patch restore failed")

    probe_log = out / "probe.log"
    with probe_log.open("w", encoding="utf-8") as handle:
        for block in runner.probe_rows:
            handle.write(f"=== {block['case']} command={block['command_id']} ===\n")
            stderr_path = out / block["stderr_path"]
            if stderr_path.is_file():
                handle.write(stderr_path.read_text(encoding="utf-8", errors="replace"))
            handle.write("\n")
    result["probe_rows"] = runner.probe_rows
    result["probe_log_sha256"] = sha256(probe_log)
    result["commands"] = runner.commands
    result["errors"] = runner.errors
    result["status"] = "pass" if not runner.errors else "fail"
    for case in result["source_cases"]:
        print("INIT_ROOT_RESULT " + json.dumps({
            "case": case["label"],
            "objects_identical": case.get("object_sha256_identical"),
            "metrics_identical": case.get("source_metrics_identical"),
            "baseline_sha256": case.get("baseline", {}).get("object_sha256"),
            "probe_sha256": case.get("probe", {}).get("object_sha256"),
            "summary": case.get("probe", {}).get("init_root_summary"),
            "emitted": (case.get("baseline", {}).get("emitted_functions") or {}).get("count"),
        }, sort_keys=True), flush=True)
    print("INIT_ROOT_OUTCOME " + json.dumps({"status": result["status"], "errors": runner.errors}), flush=True)
    manifest = out / "results.json"
    manifest.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 1 if runner.errors else 0


if __name__ == "__main__":
    sys.exit(main())
