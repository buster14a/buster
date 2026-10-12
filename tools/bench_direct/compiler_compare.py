#!/usr/bin/env python3
"""Compare a candidate's compiler with its base's on the 9700X (#2752, #2769).

Run from trusted `main` by `.github/workflows/9700x-direct-bench.yml` in one
of two modes (compiler_receipt.MODES), after its hosted authorization:
    main   (`compare` job) a commit after it landed on main against the
           authorized baseline on its first-parent chain: its first parent,
           or the nearest measured ancestor of a range (authorize_compiler);
           the checkout holds its history without blobs
    pull   (`compare-pull` job) an owner pull request's head against its merge
           base, requested by benchmarks/9700x/compiler-compare.request
The checkout has no persisted credentials. This harness, the build commands,
the lab and the frozen PROFILE come from `main`.

It follows the documented A/B recipe of docs/agents/benchmarking.md in one
tree: a tests-off Clang Release `ide` of the base (main baseline or merge base),
then of the head, then the base again so the frozen workload has its generated
closure; `tools/uarch_lab.py compare` then times both compilers on that same
base source. Builds are preparation and are timed separately. Then the
native throughput corpus (THROUGHPUT_PROFILE, #2761) runs on the same two
binaries through the base revision's `./build.sh bench_throughput`, and its
own summary and metadata join the evidence. In pull mode, when the pull
request also adds or changes SCALING_REQUEST (#424), each SCALING_PROFILE
series then runs `./build.sh bench_throughput scale` on the candidate binary
alone, from the same base checkout, and its bundle joins the evidence. In pull
mode, a fresh exact THROUGHPUT_REAL_SOURCE_REQUEST_LINE adds the real-source
leg (#2761): throughput_real_source.stage fetches the pinned inputs outside
timing, then `./build.sh bench_throughput run --real-source` times each
descriptor's compile-link on the same two binaries. A fresh exact
PRODUCTION_PROFILE_REQUEST_LINE adds the PGO/LTO production-profile leg
(#2761): the head's own `./build.sh production_profile` builds its compilers in
an isolated clone of the head, then the base revision's harness times that
run's Release compiler against its PGO+LTO compiler on the corpus. A fresh exact
FRONTEND_SELECTOR_REQUEST_LINE adds the frontend and selector probes (#2761):
the base revision's benchmark_frontend_scaling.py and selection_benchmark.py
run on the same two binaries, then `ide bench` runs in alternating fresh
processes of both.
A fresh exact COMPILER_PROBES_REQUEST_LINE adds the compiler-probes leg (#2761): the
trusted native compiler_probes.c harness builds the FAST register-allocation and
scheduler line-mark probes against both revisions in isolated clones and times
them as pairs.

The receipt records identities, the profile, toolchain versions, binary hashes
and timings, and is written even when a step fails. The candidate's build runs
as the runner account before measurement, so the receipt is evidence produced
under the direct path's owner-only trust boundary, not a sealed result.

Map: queue_head (pull-mode supersession), build (one ide), toolchain, export_tree (bounded evidence export),
read_exported_json (classification reads the export, #2929), collect_evidence,
measure_throughput (corpus leg), scaling_requested and measure_scaling (scaling leg),
ast_pilot_request_selection and measure_ast_pilot (#3397 opt-in flag pair),
real_source_request_selection and measure_real_source (#2761 real-source leg),
production_profile_request_selection and measure_production_profile (#2761 PGO/LTO leg),
compiler_probes_request_selection and measure_compiler_probes (#2761 probe leg),
frontend_selector_request_selection and measure_frontend_selector (#2761 probes), main. Validity rules live in compiler_receipt.classify.
"""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import os
import platform
import re
import shutil
import signal
import socket
import stat
import subprocess
import sys
import tarfile
import time
import zipfile
from pathlib import Path

from compiler_github import ARTIFACT_LIMIT, RECONCILE_DEPTH
from compiler_receipt import (validate_closure, IDENTITY_KEYS, INLINE_ACCEPTANCE_PROFILE, INLINE_ACCEPTANCE_REQUEST_LINE,
                              AST_PILOT_ACCEPTANCE_PROFILE, AST_PILOT_ACCEPTANCE_REQUEST_LINE, validate_ast_pilot_acceptance,
                              THROUGHPUT_REAL_SOURCE_PROFILE, THROUGHPUT_REAL_SOURCE_REQUEST_LINE,
                              throughput_real_source_digest, validate_throughput_real_source,
                              PRODUCTION_PROFILE_PROFILE, PRODUCTION_PROFILE_REQUEST_LINE, validate_production_profile,
                              COMPILER_PROBES_PROFILE, COMPILER_PROBES_REQUEST_LINE, COMPILER_PROBES_ROLES,
                              COMPILER_PROBES_HARNESS, COMPILER_PROBES_HARNESS_FLAGS,
                              compiler_probes_digest, validate_compiler_probes,
                              FRONTEND_SELECTOR_PROFILE, FRONTEND_SELECTOR_REQUEST_LINE, FRONTEND_SELECTOR_SCHEMA,
                              frontend_selector_digest, validate_frontend_selector,
                              INLINE_ACCEPTANCE_SCHEMA, ANALYZER_PROFILE_BY_LINE, ANALYZER_REQUEST_LINE,
                              ANALYZER_REQUEST_LINES,
                              ANALYZER_REQUEST_PATH, ANALYZER_REQUIRED_FILES, MODES, PROFILE, MAIN40_PROFILE, MAIN_PROFILES, named_main_profile, RECEIPT_SCHEMA, SCALING_PROFILE,
                              SCALING_REQUEST, SHA, THROUGHPUT_PROFILE, classify, classify_scaling,
                              classify_throughput, classify_throughput_exit, dumps, host_problem, inline_acceptance_requested,
                              render, scaling_digest, throughput_digest, validate_inline_acceptance,
                              analyzer_bootstrap_provenance, analyzer_profile_summary)
from compiler_receipt import observed_cpu_model as cpu_model
import throughput_real_source

BUILD_TIMEOUT_SECONDS = 1800
LAB_TIMEOUT_SECONDS = 3000
THROUGHPUT_TIMEOUT_SECONDS = 1800
SCALING_TIMEOUT_SECONDS = 1200
INLINE_ACCEPTANCE_TIMEOUT_SECONDS = 3 * 60 * 60
AST_PILOT_ACCEPTANCE_TIMEOUT_SECONDS = 2 * 60 * 60
# Measured: the whole leg (42 passes over its six cells) took 508 s on a hosted
# 2.8 GHz Xeon; this bound keeps it inside the unchanged compare-pull budget.
REAL_SOURCE_TIMEOUT_SECONDS = 1800
# Measured: the whole --no-benchmark build (seven trees, two trainings,
# inspection and test_all) took 1200 s with three lanes on a hosted 4-vCPU
# 2.8 GHz Xeon. Twice that keeps the leg, its corpus run and the ordinary
# comparison inside the unchanged 90-minute compare-pull job.
PRODUCTION_PROFILE_BUILD_TIMEOUT_SECONDS = 40 * 60
# Measured: the whole compiler-probes leg (the native harness build, two
# clones, four probe builds, 12 cases of 2 + 30 pairs) took 241 s on a hosted
# 4-vCPU 2.8 GHz Xeon; this bound keeps it inside the unchanged compare-pull budget.
COMPILER_PROBES_TIMEOUT_SECONDS = 20 * 60
# Measured: the whole frontend-selector leg took 57 s on a hosted 2.1 GHz Xeon
# (frontend scaling 45 s, selection 6 s, 40 ide bench runs 7 s). Each bound is
# far above its tool's time and the sum stays inside the unchanged compare-pull job.
FRONTEND_SCALING_TIMEOUT_SECONDS = 20 * 60
SELECTION_TIMEOUT_SECONDS = 10 * 60
IDE_BENCH_TIMEOUT_SECONDS = 30
ANALYZER_DRIVER_BUILD_TIMEOUT_SECONDS = 600
ANALYZER_GENERATE_TIMEOUT_SECONDS = 900
ANALYZER_HELPER_TIMEOUT_SECONDS = 76 * 60
ANALYZER_SETUP_BUDGET_SECONDS = 8 * 60
ANALYZER_PROFILE_BUDGET_SECONDS = 85 * 60
ANALYZER_POSTPROCESS_RESERVE_SECONDS = 2 * 60
ANALYZER_RESOURCE_FILE_LIMIT = 16384
ANALYZER_RESOURCE_BYTE_LIMIT = 256 * 1024 * 1024
GIT_TIMEOUT_SECONDS = 120
EVIDENCE_FILE_LIMIT = 32 * 1024 * 1024
# A required JSON member must stay within the publisher's per-member read limit
# (compiler_publish.MEMBER_LIMIT; compiler_test keeps them equal), and the whole
# evidence directory within three quarters of the 64 MiB artifact limit.
EVIDENCE_MEMBER_LIMIT = 8 * 1024 * 1024
EVIDENCE_TOTAL_LIMIT = ARTIFACT_LIMIT * 3 // 4
EVIDENCE_FILE_COUNT = 4096
OMISSIONS_SHOWN = 50
LAB_REQUIRED = ("summary.json",)
THROUGHPUT_REQUIRED = ("summary.json", "metadata.json")
REAL_SOURCE_REQUIRED = ("summary.json", "metadata.json", "staging.json", "cpuinfo.txt")
PRODUCTION_PROFILE_REQUIRED = ("summary.txt", "evidence/phases.tsv")
COMPILER_PROBES_REQUIRED = ("summary.json",)
# The same LLVM tools production-profile.yml ensures; the LTO lanes link with -fuse-ld=lld.
PRODUCTION_PROFILE_HOST_TOOLS = ("clang", "ld.lld", "llvm-profdata", "llvm-readobj")
SCALING_REQUIRED = ("scaling.json", "scaling-metadata.json")
FRONTEND_SCALING_REQUIRED = ("provenance.json", "results.json")
SELECTION_REQUIRED = ("report.json",)
# Raw evidence without compiled outputs, per-run binary copies or perf data.
EVIDENCE_IGNORE = ("*.exe", "instances", "*.data", "*.data.old", "*.o", "*.obj")
# A scaling bundle keeps its reports, raw samples and per-sample logs; its
# generated inputs are reproducible from the recorded seed and hashes.
SCALING_IGNORE = (*EVIDENCE_IGNORE, "inputs", "*.metrics")
# The real-source leg keeps its reports, raw samples and per-sample logs; its
# metrics files repeat samples.csv, and its executables are not evidence.
REAL_SOURCE_IGNORE = SCALING_IGNORE
# production_profile keeps its summary, phase ledger, command logs and profile
# manifests; its seven build trees and raw profiles are rebuilt from the head.
PRODUCTION_PROFILE_IGNORE = (*EVIDENCE_IGNORE, "release", "g0", "lto", "pgo", "pgo-lto", "instrumented",
                             "instrumented-lto", "*.profraw", "*.profdata")
# The probe leg keeps its summary and logs; its clones and binaries are not evidence.
COMPILER_PROBES_IGNORE = (*EVIDENCE_IGNORE, "bin")
# Frontend scaling keeps its JSON records; its generated sources (up to a few
# MB, identified by hash in results.json) and objects are not evidence.
FRONTEND_SCALING_IGNORE = (*EVIDENCE_IGNORE, "*.c")
TOOLS = (("clang", "--version"), ("cmake", "--version"), ("ninja", "--version"), ("tcc", "-v"),
         ("perf", "--version"), ("taskset", "--version"), ("git", "--version"))


CLEANUP_STATUS = 125
CLEANUP_SECONDS = 10.0


def group_members(group: int) -> bool:
    """Whether any process of the group is still running (a zombie awaiting reaping is not)."""
    alive = False
    try:
        os.killpg(group, 0)
        alive = True
    except (ProcessLookupError, PermissionError):
        alive = False
    if alive and sys.platform.startswith("linux"):
        alive = False
        for entry in Path("/proc").glob("[0-9]*/stat"):
            try:
                fields = entry.read_text(encoding="utf-8", errors="replace").rpartition(")")[2].split()
            except OSError:
                continue
            if len(fields) > 2 and fields[0] != "Z" and fields[2] == str(group):
                alive = True
                break
    return alive


def reap_group(group: int) -> bool:
    """Kill every process of this attempt's own process group and wait until none runs; False if unproven.

    Only the group created for one command is signalled, never by executable name. Descendants that
    left the group (setsid or a new process group) are outside what this attempt owns and cannot be
    reached; they are not claimed as cleaned.
    """
    deadline = time.monotonic() + CLEANUP_SECONDS
    while group_members(group):
        try:
            os.killpg(group, signal.SIGKILL)
        except ProcessLookupError:
            break
        except OSError:
            pass
        if time.monotonic() >= deadline:
            break
        time.sleep(0.01)
    return not group_members(group)



OWNED_PHASE_CONTEXT = None


class OwnedPhaseFailed(RuntimeError):
    """A contained snapshot phase failed; stop all later physical or destructive work."""


class NativePhaseContext:
    """Minimal direct bridge to the trusted native owner; process-tree policy stays in C."""
    def __init__(self, driver: Path, work: Path, evidence: Path, receipt: dict, *, utility: bool = False, owned_preflight: bool = False, main_owned: bool = False):
        from compiler_owned_phase import POPULATION_SCHEMA, UTILITY_POPULATION_SCHEMA, MAIN_POPULATION_SCHEMA
        if type(utility) is not bool or type(owned_preflight) is not bool or type(main_owned) is not bool:
            raise ValueError("native ownership route flags must be boolean")
        if utility and main_owned:
            raise ValueError("native ownership routes are mutually exclusive")
        if main_owned and receipt.get("mode") != "main":
            raise ValueError("supported native ownership requires main mode")
        self.utility, self.main_owned = utility, main_owned
        owned_preflight = owned_preflight or utility or main_owned
        self.owned_preflight = owned_preflight
        self.driver = driver.resolve(strict=True)
        if not driver.is_absolute() or driver != self.driver or not self.driver.is_relative_to(TRUSTED_ROOT) or \
                not stat.S_ISREG(self.driver.lstat().st_mode) or not os.access(self.driver, os.X_OK):
            raise ValueError("snapshot native driver must be a canonical executable in the trusted checkout")
        relative = self.driver.relative_to(TRUSTED_ROOT).parts
        if len(relative) != 5 or relative[:3] != (".cache", "bootstrap-driver", "posix") or \
                not re.fullmatch(r"[a-f0-9]{64}", relative[3]) or not re.fullmatch(r"build-[A-Za-z0-9-]+", relative[4]):
            raise ValueError("snapshot native driver must be the exact trusted POSIX bootstrap cache artifact")
        self.driver_hash = sha256(self.driver)
        self.bootstrap_marker = Path(str(self.driver) + ".complete")
        from compiler_owned_phase import sha
        self.bootstrap_hash = sha(bounded_owned_member(self.bootstrap_marker))
        self.work, self.evidence, self.receipt = work, evidence, receipt
        self.directory = evidence / "owned-phases"
        self.directory.mkdir()
        self.stopped = False
        self.receipt["phase_ownership"] = {"schema": MAIN_POPULATION_SCHEMA if main_owned else UTILITY_POPULATION_SCHEMA if utility else POPULATION_SCHEMA, "state": "pending",
                                          "trusted_root": str(TRUSTED_ROOT), "directory": str(self.directory.resolve()),
                                          "trusted_revision": receipt["identity"]["trusted_revision"] if owned_preflight else git(TRUSTED_ROOT, "rev-parse", "HEAD"),
                                          "trusted_tree": None if owned_preflight else git(TRUSTED_ROOT, "rev-parse", "HEAD^{tree}"),
                                          "driver_sha256": self.driver_hash,
                                          "driver_path": str(self.driver), "bootstrap_marker_sha256": self.bootstrap_hash,
                                          "work_root": str(work.resolve()), "evidence_root": str(evidence.resolve()),
                                          "python_path": sys.executable, "owned_preflight": owned_preflight, "count": 0, "phases": []}
        if owned_preflight:
            # Even the context's own source observations are individually owned;
            # a failed initialization never reaches a later metadata/build child.
            revision = self.execute(["git", "-C", str(TRUSTED_ROOT), "rev-parse", "HEAD"],
                                    TRUSTED_ROOT, None, GIT_TIMEOUT_SECONDS, kind="capture").stdout.decode().strip()
            if revision != receipt["identity"]["trusted_revision"]:
                self.stopped = True
                self.receipt["phase_ownership"]["state"] = "failed"
                self.receipt["work_retained"] = str(work)
                raise OwnedPhaseFailed("Utility trusted source revision differs from the admitted identity")
            tree = self.execute(["git", "-C", str(TRUSTED_ROOT), "rev-parse", "HEAD^{tree}"],
                                TRUSTED_ROOT, None, GIT_TIMEOUT_SECONDS, kind="capture").stdout.decode().strip()
            if not re.fullmatch(r"[a-f0-9]{40}", tree):
                self.stopped = True
                self.receipt["phase_ownership"]["state"] = "failed"
                self.receipt["work_retained"] = str(work)
                raise OwnedPhaseFailed("Utility trusted source tree is malformed")
            self.receipt["phase_ownership"]["trusted_tree"] = tree

    def execute(self, argv: list[str], cwd: Path, log: Path | None, timeout: int,
                *, kind: str = "run", allow_exit_failure: bool = False,
                exit_policy: str = "zero") -> subprocess.CompletedProcess:
        # Prelaunch and proof/log publication failures latch just like native
        # failure: even callers that catch OSError cannot admit another child.
        try:
            result = self._execute(argv, cwd, log, timeout, kind=kind, allow_exit_failure=allow_exit_failure,
                                   exit_policy=exit_policy)
        except BaseException:
            self.stopped = True
            self.receipt["phase_ownership"]["state"] = "failed"
            self.receipt["work_retained"] = str(self.work)
            raise
        return result

    def _execute(self, argv: list[str], cwd: Path, log: Path | None, timeout: int,
                 *, kind: str = "run", allow_exit_failure: bool = False,
                 exit_policy: str = "zero") -> subprocess.CompletedProcess:
        from compiler_owned_phase import PHASE_LIMIT, read_record, sha, validate_record, validate_bootstrap
        if self.stopped:
            raise OwnedPhaseFailed("snapshot native owner already stopped; no later child is admitted")
        corpus = exit_policy == "corpus-report-only-v1"
        if exit_policy not in ("zero", "corpus-report-only-v1") or (corpus and
                (kind != "run" or allow_exit_failure or self.receipt.get("phase") != "throughput")):
            raise OwnedPhaseFailed("snapshot nonzero run policy is restricted to the ordinary corpus")
        if type(timeout) is not int or not 0 < timeout <= 10800 or not argv or \
                any(not isinstance(item, str) or not item or any(byte in item for byte in ("\0", "\n", "\r", "\t"))
                    for item in argv):
            self.stopped = True
            raise OwnedPhaseFailed("snapshot native phase argv/timeout unsupported")
        if sha256(self.driver) != self.driver_hash:
            self.stopped = True
            raise OwnedPhaseFailed("trusted native phase executable changed")
        if sha(bounded_owned_member(self.bootstrap_marker)) != self.bootstrap_hash:
            raise OwnedPhaseFailed("trusted native bootstrap marker changed")
        population = self.receipt["phase_ownership"]
        if getattr(self, "owned_preflight", False) and kind == "capture":
            from compiler_owned_phase import preflight_capture_recipe
            if preflight_capture_recipe(self.receipt, population, argv) != (timeout, allow_exit_failure) or \
                    str(cwd.resolve(strict=True)) != str(TRUSTED_ROOT) or self.receipt.get("phase", "preflight") != "preflight":
                raise OwnedPhaseFailed("owned preflight capture is not an exact read-only Git/version probe")
        ordinal = len(population["phases"]) + 1
        if ordinal > PHASE_LIMIT:
            self.stopped = True
            raise OwnedPhaseFailed("snapshot native phase count exceeds its fixed bound")
        root = cwd.resolve(strict=True)
        if corpus:
            candidate = population.get("candidate_root")
            bins = population.get("binaries_root")
            work = population.get("work_root")
            identity = self.receipt.get("identity", {})
            legacy_utility = (getattr(self, "utility", False) or getattr(self, "main_owned", False)) and self.receipt.get("preparation_policy") == "legacy-rebuild"
            prefix = ["./build.sh", "bench_throughput"] if legacy_utility else [str(candidate) + "/build/throughput-tools/throughput"]
            expected = [*prefix, "run",
                "--baseline", str(bins) + "/ide-base", "--candidate", str(bins) + "/ide-cand",
                "--output", str(work) + "/throughput", "--baseline-id", identity.get("base"),
                "--candidate-id", identity.get("head"), *THROUGHPUT_PROFILE["arguments"]]
            if argv != expected or str(root) != candidate or timeout != THROUGHPUT_TIMEOUT_SECONDS:
                raise OwnedPhaseFailed("ordinary corpus exit policy command/root/profile differs from the fixed route")
        path = self.directory / f"{ordinal:04d}.json"
        row = {"ordinal": ordinal, "file": path.name, "phase": self.receipt.get("phase", "preflight"),
               "kind": kind, "argv": list(argv), "cwd": str(root), "timeout": timeout,
               "allow_exit_failure": allow_exit_failure, "bridge_wall_us": 0, "receipt_sha256": ""}
        if corpus:
            row["exit_policy"] = exit_policy
        population["phases"].append(row)
        population["count"] = ordinal
        checkpoint_problem = checkpoint(self.receipt, self.evidence, row["phase"])
        if checkpoint_problem:
            raise OwnedPhaseFailed(checkpoint_problem)
        started = time.monotonic_ns()
        process = None
        interrupted = None
        uncertain = False
        wrapper_status = None
        try:
            process = subprocess.Popen([str(self.driver), "compiler_closure", "owned-phase", str(path), str(root),
                                        str(timeout), self.driver_hash, self.bootstrap_hash, "--", *argv], cwd=TRUSTED_ROOT,
                                       stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                       start_new_session=True)
            wrapper_status = process.wait(timeout=timeout + 120)
        except BaseException as error:
            interrupted = error
            if process is not None and process.poll() is None:
                # Popen retains the exact positive PID lease until wait(). Its
                # send_signal never targets a released group or unrelated PID.
                try:
                    process.send_signal(signal.SIGINT if isinstance(error, KeyboardInterrupt) else signal.SIGTERM)
                    wrapper_status = process.wait(timeout=120)
                except (OSError, subprocess.TimeoutExpired):
                    uncertain = True
            elif process is None:
                uncertain = True
        native, stdout, stderr, problem = None, b"", b"", ""
        try:
            raw = bounded_owned_member(path)
            stdout = bounded_owned_member(Path(str(path) + ".stdout"), empty=True)
            stderr = bounded_owned_member(Path(str(path) + ".stderr"), empty=True)
            command = bounded_owned_member(Path(str(path) + ".argv"))
            marker = bounded_owned_member(Path(str(path) + ".bootstrap.complete"))
            native = read_record(raw)
            row["receipt_sha256"] = sha(raw)
            from compiler_owned_phase import command_bytes
            if command != command_bytes(argv):
                raise ValueError("native phase saved argv differs from the trusted call")
            issues = validate_record(native, argv, str(root), timeout, self.driver_hash, stdout, stderr,
                                     nominal=not (allow_exit_failure or corpus), receipt_path=str(path))
            issues.extend(validate_bootstrap(native, marker, population))
            if allow_exit_failure or corpus:
                issues.extend(validate_owned_capture(native))
            if getattr(self, "owned_preflight", False) and kind == "capture" and (
                    not os.WIFEXITED(native.get("exit_status", 0)) or
                    os.waitstatus_to_exitcode(native["exit_status"]) not in (0, 1)):
                issues.append("owned preflight probe did not reach its expected clean exit 0/1")
            if corpus and (not os.WIFEXITED(native.get("exit_status", 0)) or
                    os.waitstatus_to_exitcode(native["exit_status"]) not in (0, 1)):
                issues.append("ordinary corpus did not reach exit 0 or report-only exit 1")
            problem = "; ".join(issues)
        except (OSError, ValueError, UnicodeError) as error:
            problem = str(error)
        if log is not None:
            with log.open("ab") as stream:
                stream.write(("$ " + " ".join(argv) + "\n").encode())
                stream.write(stdout)
                stream.write(stderr)
                stream.write(f"\nnative-wrapper-exit={wrapper_status}\n".encode())
        row["bridge_wall_us"] = (time.monotonic_ns() - started) // 1000
        cleanup = isinstance(native, dict) and native.get("cleanup_proven") is True and \
                  native.get("reservation_retained") == 0 and native.get("ownership_lost") == 0
        if uncertain or not cleanup:
            self.stopped = True
            population["state"] = "failed"
            self.receipt["cleanup_proven"] = False
            self.receipt["work_retained"] = str(self.work)
            (self.evidence / "cleanup-uncertain").write_text(
                "Native snapshot phase owner cleanup unproven; retain work and admit no next phase.\n", encoding="utf-8")
            checkpoint(self.receipt, self.evidence, row["phase"])
            raise ClosureCleanupUncertain("snapshot native phase cleanup missing or uncertain: " + problem)
        if interrupted is not None or problem or wrapper_status not in (0, 1) or \
                (not (allow_exit_failure or corpus) and wrapper_status != 0):
            self.stopped = True
            population["state"] = "failed"
            checkpoint(self.receipt, self.evidence, row["phase"])
            raise OwnedPhaseFailed("snapshot native phase failed/cancelled: " +
                                   (problem or repr(interrupted) or str(wrapper_status)))
        return subprocess.CompletedProcess(argv, os.waitstatus_to_exitcode(native["exit_status"]), stdout, stderr)

    def finish(self) -> None:
        population = self.receipt["phase_ownership"]
        population["state"] = "failed" if self.stopped else "complete"


def bounded_owned_member(path: Path, *, empty: bool = False) -> bytes:
    with os.fdopen(os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)), "rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_size > EVIDENCE_MEMBER_LIMIT or (not empty and before.st_size == 0):
            raise ValueError("native snapshot phase member missing, oversized or irregular")
        raw = stream.read(EVIDENCE_MEMBER_LIMIT + 1)
        after = os.fstat(stream.fileno())
        if len(raw) != before.st_size or before.st_size != after.st_size or before.st_mtime_ns != after.st_mtime_ns:
            raise ValueError("native snapshot phase member changed while being read")
        return raw


def validate_owned_capture(native: dict) -> list[str]:
    """A read-only probe may expect a nonzero exit; timeouts/cancellation/orphans always stop."""
    reasons = []
    status = native.get("exit_status")
    if type(status) is not int or not 0 <= status <= 65535 or not os.WIFEXITED(status) or \
            native.get("state") != ("complete" if status == 0 else "failed") or \
            any(native.get(key) != 0 for key in ("timed_out", "cancelled", "capture_failed", "output_truncated",
                                                "cleanup_signalled", "cleanup_reaped", "tree_cleanup_failed")):
        reasons.append("native snapshot probe did not reach a clean terminal exit")
    return reasons


def captured_run(argv: list[str], **options) -> subprocess.CompletedProcess:
    """Historical probes stay unchanged; snapshot measurement probes use the same native owner."""
    if OWNED_PHASE_CONTEXT is None:
        return subprocess.run(argv, **options)
    context = OWNED_PHASE_CONTEXT
    try:
        if any(key not in ("cwd", "capture_output", "text", "timeout", "check") for key in options):
            raise OwnedPhaseFailed("snapshot probe has unsupported process options")
        default_cwd = TRUSTED_ROOT if getattr(context, "owned_preflight", False) else os.getcwd()
        result = OWNED_PHASE_CONTEXT.execute(argv, Path(options.get("cwd") or default_cwd), None,
                                            options.get("timeout", GIT_TIMEOUT_SECONDS), kind="capture",
                                            allow_exit_failure=not options.get("check", False))
        if options.get("text"):
            result.stdout = result.stdout.decode()
            result.stderr = result.stderr.decode()
        if options.get("check") and result.returncode:
            raise subprocess.CalledProcessError(result.returncode, argv, result.stdout, result.stderr)
    except BaseException:
        context.stopped = True
        context.receipt["phase_ownership"]["state"] = "failed"
        context.receipt["work_retained"] = str(context.work)
        raise
    return result


def run(argv: list[str], cwd: Path, log: Path, timeout: int) -> int:
    """Run argv in its own process group with output appended to log; returns the exit status.

    On timeout (124), on any exception such as cancellation by SIGTERM, and after normal completion the
    whole group is killed and awaited before returning, so no descendant outlives the command or touches
    scratch/evidence during the next phase. The original status is logged first; if the group cannot be
    proven empty the status is CLEANUP_STATUS (125) so no later measurement begins.
    """
    if OWNED_PHASE_CONTEXT is not None:
        return OWNED_PHASE_CONTEXT.execute(argv, cwd, log, timeout).returncode
    with log.open("ab") as stream:
        stream.write(("$ " + " ".join(argv) + "\n").encode())
        stream.flush()
        status = 127
        process = None
        group = 0
        try:
            process = subprocess.Popen(argv, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT,
                                       stdin=subprocess.DEVNULL, start_new_session=True)
            group = process.pid
            try:
                status = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                status = 124
        except OSError as error:
            stream.write(f"{error}\n".encode())
            status = 127
        finally:
            if process is not None:
                cleaned = reap_group(group)
                try:
                    process.wait(timeout=CLEANUP_SECONDS)
                except subprocess.TimeoutExpired:
                    cleaned = False
                if not cleaned:
                    stream.write(f"cleanup of process group {group} could not be proven (original exit={status})\n".encode())
                    status = CLEANUP_STATUS
        stream.write(f"exit={status}\n".encode())
    return status


def git(candidate: Path, *arguments: str) -> str:
    return captured_run(["git", "-C", str(candidate), *arguments], check=True, capture_output=True,
                          text=True, timeout=GIT_TIMEOUT_SECONDS).stdout.strip()


def queue_head(repository: str, ref: str) -> str:
    """The ref's current head by an anonymous read; '' when gone, None when unknown."""
    environment = dict(os.environ, GIT_TERMINAL_PROMPT="0")
    head = None
    try:
        result = captured_run(["git", "ls-remote", f"https://github.com/{repository}.git", ref],
                                capture_output=True, text=True, timeout=GIT_TIMEOUT_SECONDS, env=environment,
                                check=False)
        if result.returncode == 0:
            fields = result.stdout.split()
            head = fields[0] if fields and SHA.fullmatch(fields[0]) else ""
    except (OSError, subprocess.TimeoutExpired):
        head = None
    return head


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def build(candidate: Path, commit: str, log: Path) -> tuple[str, float]:
    """Check out commit and build a tests-off Release ide; returns (problem, seconds)."""
    started = time.monotonic()
    problem = ""
    checkout = ["git", "-C", str(candidate), "checkout", "--quiet", "--detach", commit]
    if OWNED_PHASE_CONTEXT is not None:
        checkout[1:1] = ["-c", "gc.auto=0", "-c", "maintenance.auto=false", "-c", "core.hooksPath=/dev/null"]
    status = run(checkout, candidate, log,
                 GIT_TIMEOUT_SECONDS)
    for argv in (["./build.sh", "generate", "--cc", "clang", "--no-include-tests"],
                 ["./build.sh", "build", "--config", "Release", "-t", "ide"]):
        if status == 0:
            status = run(argv, candidate, log, BUILD_TIMEOUT_SECONDS)
    cache = candidate / "build" / "CMakeCache.txt"
    if status != 0:
        problem = f"build of {commit} failed with exit {status} (see {log.name})"
    elif "BUSTER_INCLUDE_TESTS:BOOL=OFF" not in (cache.read_text(encoding="utf-8", errors="replace").splitlines()
                                                 if cache.is_file() else []):
        problem = f"build of {commit} is not tests-off"
    elif not (candidate / "build" / "Release" / "ide").is_file():
        problem = f"build of {commit} produced no build/Release/ide"
    return problem, time.monotonic() - started


def toolchain() -> dict:
    versions = {}
    for tool, flag in TOOLS:
        if OWNED_PHASE_CONTEXT is not None and shutil.which(tool) is None:
            from compiler_owned_phase import MANDATORY_VERSION_TOOLS
            if tool in MANDATORY_VERSION_TOOLS:
                raise FileNotFoundError(f"owned preflight requires configured tool {tool}")
            versions[tool] = "NA (FileNotFoundError)"
            continue
        try:
            result = captured_run([tool, flag], capture_output=True, text=True, timeout=30, check=False)
            text = (result.stdout + result.stderr).strip().splitlines()
            versions[tool] = text[0] if text else "NA (no output)"
        except (OSError, subprocess.TimeoutExpired) as error:
            versions[tool] = f"NA ({error.__class__.__name__})"
    versions["python"] = platform.python_version()
    versions["kernel"] = platform.release()
    return versions


def evidence_usage(evidence: Path) -> tuple[int, int]:
    """(bytes, files) already under the evidence root, without following symlinks."""
    total = count = 0
    for directory, _, names in os.walk(evidence):
        for name in names:
            try:
                total += os.lstat(os.path.join(directory, name)).st_size
                count += 1
            except OSError:
                pass
    return total, count


def export_tree(source: Path, destination: Path, evidence: Path, ignore: tuple, required: tuple) -> tuple[list[str], list]:
    """Copy a measurement tree into the evidence under byte/file limits enforced before and during copying.

    Only regular files are admitted: symlinks and other types are never followed or copied. A file over
    its limit (EVIDENCE_MEMBER_LIMIT for JSON, else EVIDENCE_FILE_LIMIT), one that would exceed the
    aggregate budget, one that grows while being read, or one that cannot be read is omitted and listed.
    The tree is staged beside its destination and moved into place afterwards. Returns (problems,
    omissions); an omitted or absent required member is a problem, so the export is incomplete rather than
    quietly successful. The source is never modified.
    """
    problems: list[str] = []
    omissions: list = []
    staging = destination.with_name(destination.name + ".staging")
    used, files = evidence_usage(evidence)
    if destination.is_dir():  # a re-export replaces it
        replaced = evidence_usage(destination)
        used, files = used - replaced[0], files - replaced[1]
    present: set[str] = set()
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    try:
        for directory, names, leaves in os.walk(source, followlinks=False):
            relative = Path(directory).relative_to(source)
            kept = []
            for name in sorted(names):
                shown = (relative / name).as_posix()
                if any(fnmatch.fnmatch(name, pattern) for pattern in ignore):
                    continue
                if os.path.islink(os.path.join(directory, name)):
                    omissions.append({"path": shown, "reason": "symlink is not followed"})
                    continue
                kept.append(name)
                (staging / relative / name).mkdir(parents=True, exist_ok=True)
            names[:] = kept
            # Required members first, so an exhausted budget never starves them.
            for name in sorted(leaves, key=lambda leaf: ((relative / leaf).as_posix() not in required, leaf)):
                shown = (relative / name).as_posix()
                if any(fnmatch.fnmatch(name, pattern) for pattern in ignore):
                    continue
                reason = ""
                path = os.path.join(directory, name)
                limit = EVIDENCE_MEMBER_LIMIT if name.endswith(".json") else EVIDENCE_FILE_LIMIT
                try:
                    status = os.lstat(path)
                    if not stat.S_ISREG(status.st_mode):
                        reason = "not a regular file"
                    elif status.st_size > limit:
                        reason = f"{status.st_size} bytes exceeds the {limit} byte file limit"
                    elif used + status.st_size > EVIDENCE_TOTAL_LIMIT or files + 1 > EVIDENCE_FILE_COUNT:
                        reason = "evidence byte or file-count budget exhausted"
                    else:
                        target = staging / shown
                        copied = 0
                        descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
                        with os.fdopen(descriptor, "rb") as reader, target.open("wb") as writer:
                            for block in iter(lambda: reader.read(1 << 20), b""):
                                copied += len(block)
                                if copied > limit or used + copied > EVIDENCE_TOTAL_LIMIT:
                                    reason = "grew past its limit while being copied"
                                    break
                                writer.write(block)
                        if reason:
                            target.unlink()
                        else:
                            used += copied
                            files += 1
                            present.add(shown)
                except OSError as error:
                    reason = f"copy failed: {error}"
                if reason:
                    omissions.append({"path": shown, "reason": reason})
        for member in required:
            if member not in present:
                why = next((item["reason"] for item in omissions if item["path"] == member), "absent from the source")
                problems.append(f"required evidence {destination.name}/{member} not exported: {why}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists():
            shutil.rmtree(destination)
        os.replace(staging, destination)
    except OSError as error:
        problems.append(f"evidence export of {destination.name} failed: {error}")
        shutil.rmtree(staging, ignore_errors=True)
    return problems, omissions


def read_exported_json(path: Path) -> tuple[object, str]:
    """(document, "") from the exported copy, or (None, why) for anything but a bounded regular JSON file.

    Classification must see the bytes the publisher will see, not the measurement tree they were copied
    from (#2929), so this reads the evidence member without following a symlink and within
    EVIDENCE_MEMBER_LIMIT. Every failure is a returned reason, never an exception.
    """
    document: object = None
    problem = ""
    try:
        if not stat.S_ISREG(os.lstat(path).st_mode):
            problem = "not a regular file"
        else:
            with os.fdopen(os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)), "rb") as reader:
                data = reader.read(EVIDENCE_MEMBER_LIMIT + 1)
            if len(data) > EVIDENCE_MEMBER_LIMIT:
                problem = f"exceeds the {EVIDENCE_MEMBER_LIMIT} byte member limit"
            else:
                document = json.loads(data.decode("utf-8"))
    except (OSError, ValueError, RecursionError) as error:
        problem = str(error) or error.__class__.__name__
    return document, problem


def read_exported_pair(directory: Path, names: tuple[str, str], label: str) -> tuple[list, list[str]]:
    """The two named JSON members of an exported evidence directory, and a reason for each unreadable one."""
    documents: list = []
    reasons: list[str] = []
    for name in names:
        document, problem = read_exported_json(directory / name)
        documents.append(document)
        if problem:
            reasons.append(f"exported evidence {label}/{name} unreadable: {problem}")
    return documents, reasons


def note_omissions(omissions: dict, name: str, found: list) -> None:
    """Record a bounded omission list in the receipt's section so omissions are never silent."""
    if found:
        omissions[name] = {"count": len(found), "shown": found[:OMISSIONS_SHOWN]}


def collect_evidence(lab: Path, evidence: Path, omissions: dict) -> list[str]:
    problems: list[str] = []
    if lab.is_dir():
        problems, found = export_tree(lab, evidence / "lab", evidence, EVIDENCE_IGNORE, LAB_REQUIRED)
        note_omissions(omissions, "lab", found)
    return problems


def measure_throughput(candidate: Path, bins: Path, work: Path, evidence: Path, base: str, head: str,
                       binaries: dict, omissions: dict, harness: Path | None = None) -> tuple[list[str], dict]:
    """Run and validate the exported corpus before admitting a later snapshot child."""
    output = work / "throughput"
    argv = [*([str(harness)] if harness else ["./build.sh", "bench_throughput"]), "run",
            "--baseline", str(bins / "ide-base"), "--candidate", str(bins / "ide-cand"),
            "--output", str(output), "--baseline-id", base, "--candidate-id", head,
            *THROUGHPUT_PROFILE["arguments"]]
    context = OWNED_PHASE_CONTEXT
    try:
        if context is None:
            status = run(argv, candidate, evidence / "throughput.log", THROUGHPUT_TIMEOUT_SECONDS)
        else:
            status = context.execute(argv, candidate, evidence / "throughput.log", THROUGHPUT_TIMEOUT_SECONDS,
                                     exit_policy="corpus-report-only-v1").returncode
        reasons = []
        if output.is_dir():
            problems, found = export_tree(output, evidence / "throughput", evidence, EVIDENCE_IGNORE, THROUGHPUT_REQUIRED)
            reasons.extend(problems)
            note_omissions(omissions, "throughput", found)
        # Classify the exact exported bytes the hosted publisher will read.
        documents, unreadable = read_exported_pair(evidence / "throughput", THROUGHPUT_REQUIRED, "throughput")
        reasons.extend(unreadable)
        reasons.extend(classify_throughput_exit(status, documents[0], documents[1], binaries))
        section = dict(throughput_digest(documents[0]), exit=status, exit_policy="corpus-report-only-v1")
        if context is not None:
            context.receipt["throughput"] = section
            if reasons:
                raise OwnedPhaseFailed("ordinary corpus incomplete: " + "; ".join(reasons))
            row = context.receipt["phase_ownership"]["phases"][-1]
            row["corpus_summary_sha256"] = hashlib.sha256(bounded_owned_member(evidence / "throughput/summary.json")).hexdigest()
            row["corpus_metadata_sha256"] = hashlib.sha256(bounded_owned_member(evidence / "throughput/metadata.json")).hexdigest()
    except BaseException:
        if context is not None:
            context.stopped = True
            context.receipt["phase_ownership"]["state"] = "failed"
            context.receipt["work_retained"] = str(context.work)
        raise
    return reasons, section


SCRATCH_MARKER = ".buster-compiler-compare-scratch"
SCRATCH_PROTOCOL = "buster-compiler-compare-scratch-v1\n"
TRUSTED_ROOT = Path(__file__).resolve().parents[2]


class ScratchError(Exception):
    """The requested work/evidence directories are unsafe to create or clear."""


def overlaps(first: Path, second: Path) -> bool:
    """Whether two resolved paths are equal or one is an ancestor of the other."""
    return first == second or first in second.parents or second in first.parents


def owned_scratch(directory: Path) -> bool:
    marker = directory / SCRATCH_MARKER
    try:
        return marker.is_file() and not marker.is_symlink() and marker.read_text(encoding="utf-8") == SCRATCH_PROTOCOL
    except (OSError, ValueError):
        return False


def plan_scratch(candidate: Path, lab: Path, work: Path, evidence: Path) -> tuple[Path, Path]:
    """Validate the whole work/evidence plan without touching the filesystem; return resolved paths.

    Paths are compared after resolving symlinks, with path ancestry rather than string prefixes.
    A scratch directory may not equal, contain or sit inside the candidate checkout, the trusted
    tools or lab, or each other, nor contain the home directory or be the filesystem root. An
    existing scratch directory must be empty or carry the ownership marker.
    """
    work, evidence = work.resolve(), evidence.resolve()
    protected = {"candidate checkout": candidate.resolve(), "trusted repository": TRUSTED_ROOT,
                 "trusted lab": lab.resolve()}
    for label, directory in (("--work", work), ("--evidence", evidence)):
        if directory.parent == directory:
            raise ScratchError(f"{label} {directory} is a filesystem root")
        try:
            home = Path.home().resolve()
        except (OSError, RuntimeError):
            home = None
        if home is not None and (directory == home or directory in home.parents):
            raise ScratchError(f"{label} {directory} is the home directory or one of its ancestors")
        for name, other in protected.items():
            if overlaps(directory, other):
                raise ScratchError(f"{label} {directory} overlaps the {name} {other}")
    if overlaps(work, evidence):
        raise ScratchError(f"--work {work} and --evidence {evidence} are equal or nested")
    for label, directory in (("--work", work), ("--evidence", evidence)):
        if directory.exists():
            if not directory.is_dir():
                raise ScratchError(f"{label} {directory} exists and is not a directory")
            if not owned_scratch(directory) and any(directory.iterdir()):
                raise ScratchError(f"{label} {directory} has unrelated content and no {SCRATCH_MARKER} marker")
    return work, evidence


def prepare_scratch(directory: Path) -> None:
    """Recreate a planned scratch directory empty with its marker; only marked or empty ones are cleared."""
    if directory.exists() and owned_scratch(directory):
        shutil.rmtree(directory)
    directory.mkdir(parents=True, exist_ok=True)
    (directory / SCRATCH_MARKER).write_text(SCRATCH_PROTOCOL, encoding="utf-8")


def scaling_requested(candidate: Path, base: str, head: str) -> bool:
    """Whether the pull request adds or changes SCALING_REQUEST (it must remain in the head)."""
    changed = git(candidate, "diff", "--name-only", base, head, "--", SCALING_REQUEST)
    present = captured_run(["git", "-C", str(candidate), "cat-file", "-e", f"{head}:{SCALING_REQUEST}"],
                             capture_output=True, timeout=GIT_TIMEOUT_SECONDS, check=False).returncode == 0
    return bool(changed) and present


def request_selector_count(candidate: Path, revision: str, selector: str) -> int | None:
    """Selector occurrences in one committed request file; None means git could not prove the blob."""
    object_name = f"{revision}:{ANALYZER_REQUEST_PATH}"
    try:
        commit = captured_run(["git", "-C", str(candidate), "cat-file", "-e", f"{revision}^{{commit}}"],
                                capture_output=True, timeout=GIT_TIMEOUT_SECONDS, check=False)
        if commit.returncode:
            return None
        listing = captured_run(["git", "-C", str(candidate), "ls-tree", "-z", revision, "--",
                                  ANALYZER_REQUEST_PATH], capture_output=True, timeout=GIT_TIMEOUT_SECONDS,
                                 check=False)
        if listing.returncode:
            return None
        entries = listing.stdout.split(b"\0")
        if entries[-1:] == [b""]:
            entries.pop()
        if not entries:
            return 0
        if len(entries) != 1 or b"\t" not in entries[0]:
            return None
        metadata, listed_path = entries[0].split(b"\t", 1)
        fields = metadata.split()
        if len(fields) != 3 or fields[0] not in (b"100644", b"100755") or fields[1] != b"blob" or \
                not re.fullmatch(rb"[0-9a-f]{40,64}", fields[2]) or \
                listed_path != ANALYZER_REQUEST_PATH.encode("utf-8"):
            return None
        shown = captured_run(["git", "-C", str(candidate), "show", object_name],
                               capture_output=True, timeout=GIT_TIMEOUT_SECONDS, check=False)
        if shown.returncode:
            return None
        # Count exact UTF-8 selector lines from bytes so unrelated malformed
        # bytes cannot make a newly added request fall back to the legacy profile.
        return shown.stdout.splitlines().count(selector.encode("utf-8"))
    except (OSError, subprocess.TimeoutExpired):
        return None


def request_selector_parent_deltas(candidate: Path, head: str, selector: str) -> list[int] | None:
    """Return exact current-commit selector-count deltas against each parent, or None if unprovable."""
    try:
        parents_line = git(candidate, "rev-list", "--parents", "-n", "1", head).split()
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError):
        return None
    if not parents_line or parents_line[0] != head or not 1 <= len(parents_line[1:]) <= 2:
        return None
    head_count = request_selector_count(candidate, head, selector)
    if head_count is None:
        return None
    parent_counts = [request_selector_count(candidate, parent, selector) for parent in parents_line[1:]]
    if any(count is None for count in parent_counts):
        return None
    return [head_count - count for count in parent_counts if count is not None]


def request_selector_added_at_head(candidate: Path, head: str, selector: str) -> bool:
    """Require one new exact selector occurrence in the current commit versus every parent."""
    deltas = request_selector_parent_deltas(candidate, head, selector)
    return deltas is not None and all(delta == 1 for delta in deltas)


def analyzer_profile_requested(candidate: Path, head: str) -> bool:
    """Whether one versioned analyzer selector was freshly added at this head."""
    selected, _ = analyzer_profile_request_selection(candidate, head)
    return selected is not None


def analyzer_profile_request_selection(candidate: Path, head: str) -> tuple[str | None, str]:
    """Select one profile only when its exact line is the sole fresh selector at every parent."""
    deltas = {line: request_selector_parent_deltas(candidate, head, line) for line in ANALYZER_REQUEST_LINES}
    if any(value is None for value in deltas.values()):
        return None, "could not prove the analyzer profile selector counts against every head parent"
    positive = [line for line, values in deltas.items() if any(value > 0 for value in values)]
    if not positive:
        return None, ""
    if len(positive) != 1:
        return None, "only one versioned analyzer selector may be freshly added at the head"
    selected = positive[0]
    selected_deltas = deltas[selected]
    if selected_deltas is None or any(value != 1 for value in selected_deltas):
        return None, "analyzer selector must be added exactly once relative to every head parent"
    for line, values in deltas.items():
        if line != selected and (values is None or any(value != 0 for value in values)):
            return None, "the other recognized analyzer selector count must remain unchanged at every head parent"
    return selected, ""


def ast_pilot_request_selection(candidate: Path, head: str) -> tuple[bool, str]:
    """Select #3397's fixed profile only when its exact line is added once against every head parent."""
    deltas = request_selector_parent_deltas(candidate, head, AST_PILOT_ACCEPTANCE_REQUEST_LINE)
    if deltas is None:
        return False, "could not prove the ast-pilot acceptance selector count against every head parent"
    if not any(delta > 0 for delta in deltas):
        return False, ""
    if any(delta != 1 for delta in deltas):
        return False, "the ast-pilot acceptance selector must be added exactly once relative to every head parent"
    return True, ""


def real_source_request_selection(candidate: Path, head: str) -> tuple[bool, str]:
    """Select #2761's real-source profile only when its exact line is added once against every head parent."""
    deltas = request_selector_parent_deltas(candidate, head, THROUGHPUT_REAL_SOURCE_REQUEST_LINE)
    if deltas is None:
        return False, "could not prove the real-source throughput selector count against every head parent"
    if not any(delta > 0 for delta in deltas):
        return False, ""
    if any(delta != 1 for delta in deltas):
        return False, "the real-source throughput selector must be added exactly once relative to every head parent"
    return True, ""


def production_profile_request_selection(candidate: Path, head: str) -> tuple[bool, str]:
    """Select #2761's PGO/LTO profile only when its exact line is added once against every head parent."""
    deltas = request_selector_parent_deltas(candidate, head, PRODUCTION_PROFILE_REQUEST_LINE)
    if deltas is None:
        return False, "could not prove the production-profile selector count against every head parent"
    if not any(delta > 0 for delta in deltas):
        return False, ""
    if any(delta != 1 for delta in deltas):
        return False, "the production-profile selector must be added exactly once relative to every head parent"
    return True, ""


def compiler_probes_request_selection(candidate: Path, head: str) -> tuple[bool, str]:
    """Select #2761's probe profile only when its exact line is added once against every head parent."""
    deltas = request_selector_parent_deltas(candidate, head, COMPILER_PROBES_REQUEST_LINE)
    if deltas is None:
        return False, "could not prove the compiler-probes selector count against every head parent"
    if not any(delta > 0 for delta in deltas):
        return False, ""
    if any(delta != 1 for delta in deltas):
        return False, "the compiler-probes selector must be added exactly once relative to every head parent"
    return True, ""


def frontend_selector_request_selection(candidate: Path, head: str) -> tuple[bool, str]:
    """Select #2761's frontend and selector probes only when the exact line is added once against every head parent."""
    deltas = request_selector_parent_deltas(candidate, head, FRONTEND_SELECTOR_REQUEST_LINE)
    if deltas is None:
        return False, "could not prove the frontend-selector selector count against every head parent"
    if not any(delta > 0 for delta in deltas):
        return False, ""
    if any(delta != 1 for delta in deltas):
        return False, "the frontend-selector selector must be added exactly once relative to every head parent"
    return True, ""


def analyzer_profile_request_status(candidate: Path, head: str) -> tuple[bool, str]:
    """Compatibility boolean wrapper for callers interested only in selection state."""
    selected, problem = analyzer_profile_request_selection(candidate, head)
    return selected is not None, problem


def request_selector_increased_at_head(candidate: Path, head: str, selector: str) -> bool:
    """Detect any new selector occurrence in the current commit versus every parent."""
    deltas = request_selector_parent_deltas(candidate, head, selector)
    return deltas is not None and all(delta > 0 for delta in deltas)


def analyzer_budget_timeout(deadline: float, maximum: int) -> int:
    """Whole-second command timeout remaining before a fixed profile deadline."""
    return max(0, min(maximum, int(deadline - time.monotonic())))


def analyzer_source_immutability_problem(candidate: Path) -> str:
    """Reject tracked edits and non-ignored untracked files left by any analyzer arm."""
    try:
        result = captured_run(["git", "-C", str(candidate), "status", "--porcelain=v1",
                                "--untracked-files=all"], capture_output=True, timeout=GIT_TIMEOUT_SECONDS,
                               check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        return f"candidate source immutability could not be checked: {error}"
    if result.returncode:
        return f"candidate source immutability check exited {result.returncode}"
    if result.stdout:
        return "analyzer profile left tracked or non-ignored untracked candidate source files"
    return ""


def analyzer_clang_provenance() -> dict:
    """Bind the analyzer's resolved Clang executable and complete resource-file tree."""
    command = shutil.which("clang")
    if not command:
        raise RuntimeError("clang is not available on PATH")
    executable = Path(command).resolve(strict=True)
    if not stat.S_ISREG(os.lstat(executable).st_mode):
        raise RuntimeError("resolved clang executable is not a regular file")
    version = captured_run([str(executable), "--version"], capture_output=True, text=True,
                             timeout=30, check=False)
    resource = captured_run([str(executable), "-print-resource-dir"], capture_output=True, text=True,
                              timeout=30, check=False)
    if version.returncode != 0 or resource.returncode != 0:
        raise RuntimeError("clang version or resource-directory query failed")
    resource_directory = Path(resource.stdout.strip()).resolve(strict=True)
    if not resource_directory.is_dir() or not resource.stdout.strip():
        raise RuntimeError("clang resource directory is missing or not a directory")
    resource_digest = hashlib.sha256()
    resource_bytes = resource_files = 0
    for directory, names, leaves in os.walk(resource_directory, followlinks=False):
        base = Path(directory)
        for name in sorted(names):
            if (base / name).is_symlink():
                raise RuntimeError("clang resource directory contains a symlinked subdirectory")
        for name in sorted(leaves):
            path = base / name
            try:
                info = os.lstat(path)
                if not stat.S_ISREG(info.st_mode):
                    raise RuntimeError("clang resource directory contains a non-regular file")
                if path.is_symlink():
                    raise RuntimeError("clang resource directory contains a symlinked file")
                resource_bytes += info.st_size
                resource_files += 1
                if resource_bytes > ANALYZER_RESOURCE_BYTE_LIMIT or resource_files > ANALYZER_RESOURCE_FILE_LIMIT:
                    raise RuntimeError("clang resource directory exceeds its evidence bound")
                digest = sha256(path)
                resource_digest.update(path.relative_to(resource_directory).as_posix().encode("utf-8"))
                resource_digest.update(b"\0")
                resource_digest.update(str(info.st_size).encode("ascii"))
                resource_digest.update(b"\0")
                resource_digest.update(digest.encode("ascii"))
                resource_digest.update(b"\n")
            except OSError as error:
                raise RuntimeError(f"clang resource file could not be bound: {error}") from error
    if not resource_files:
        raise RuntimeError("clang resource directory has no regular files")
    return {"schema": "buster-analyzer-clang-provenance-v1", "path": str(executable),
            "sha256": sha256(executable), "size_bytes": os.lstat(executable).st_size,
            "version": (version.stdout + version.stderr).strip()[:4096],
            "version_sha256": hashlib.sha256((version.stdout + version.stderr).encode()).hexdigest(),
            "resource_directory": str(resource_directory), "resource_tree_sha256": resource_digest.hexdigest(),
            "resource_file_count": resource_files, "resource_total_bytes": resource_bytes}


def build_analyzer_driver(candidate: Path, commit: str, role: str, analyzer_root: Path,
                          evidence: Path, receipt: dict, setup_deadline: float) -> tuple[str, Path | None]:
    """Build and retain one revision's exact native build driver and bootstrap manifest."""
    started = time.monotonic()
    log = evidence / f"analyzer-driver-{role}.log"
    (analyzer_root / "drivers").mkdir(parents=True, exist_ok=True)
    checkout_timeout = analyzer_budget_timeout(setup_deadline, GIT_TIMEOUT_SECONDS)
    status = run(["git", "-C", str(candidate), "checkout", "--quiet", "--detach", commit], candidate, log,
                 checkout_timeout) if checkout_timeout else 124
    if status == 0:
        observed = git(candidate, "rev-parse", "HEAD")
        receipt.setdefault("analyzer_driver_checkouts", {})[role] = observed
        if observed != commit:
            status = 1
            receipt["reasons"].append(f"{role} analyzer build-driver checkout {observed} does not match {commit}")
        else:
            (analyzer_root / "drivers" / f"{role}.checkout").write_text(observed + "\n", encoding="ascii")
    if status == 0:
        driver_timeout = analyzer_budget_timeout(setup_deadline, ANALYZER_DRIVER_BUILD_TIMEOUT_SECONDS)
        status = run(["./build.sh", "clang_analyze_benchmark", "--print-executable"], candidate, log,
                     driver_timeout) if driver_timeout else 124
    path = None
    problem = ""
    if status != 0:
        problem = f"{role} native analyzer driver build exited {status} (see {log.name})"
    else:
        lines = [line.partition(" ")[2].strip() for line in log.read_text(encoding="utf-8", errors="replace").splitlines()
                 if line.startswith("BUSTER_ANALYZER_DRIVER ")]
        if len(lines) != 1 or not Path(lines[0]).is_absolute():
            problem = f"{role} build did not report one absolute native driver path"
        else:
            source = Path(lines[0])
            marker = Path(lines[0] + ".complete")
            try:
                source_stat = os.lstat(source)
                marker_stat = os.lstat(marker)
                if not stat.S_ISREG(source_stat.st_mode) or not stat.S_ISREG(marker_stat.st_mode):
                    raise OSError("driver or bootstrap marker is not a regular file")
                driver_bytes = source.read_bytes()
                marker_bytes = marker.read_bytes()
                provenance = analyzer_bootstrap_provenance(driver_bytes, marker_bytes)
                provenance["source_revision"] = observed
                target = analyzer_root / "drivers" / f"{role}.driver"
                target_marker = analyzer_root / "drivers" / f"{role}.complete"
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(driver_bytes)
                target.chmod(0o755)
                target_marker.write_bytes(marker_bytes)
                if hashlib.sha256(target.read_bytes()).hexdigest() != provenance["sha256"]:
                    raise OSError("copied native build driver hash changed")
                receipt.setdefault("analyzer_driver_provenance", {})[role] = provenance
                path = target
            except (OSError, ValueError, UnicodeDecodeError) as error:
                problem = f"{role} native build-driver provenance is incomplete: {error}"
    receipt.setdefault("timings", {}).setdefault("analyzer_setup_seconds", {})[f"build_driver_{role}"] = round(
        time.monotonic() - started, 3)
    if problem:
        receipt["reasons"].append(problem)
    return problem, path


def collect_analyzer_files(root: Path) -> dict[str, bytes]:
    """Read regular, non-symlink analyzer evidence files without following links."""
    files = {}
    for directory, names, leaves in os.walk(root, followlinks=False):
        base = Path(directory)
        kept = []
        for name in sorted(names):
            path = base / name
            if path.is_symlink():
                continue
            kept.append(name)
        names[:] = kept
        for name in sorted(leaves):
            path = base / name
            try:
                mode = os.lstat(path).st_mode
                if not stat.S_ISREG(mode):
                    continue
                files[path.relative_to(root).as_posix()] = path.read_bytes()
            except OSError:
                continue
    return files


def measure_analyzer_profile(arguments: argparse.Namespace, candidate: Path, work: Path, evidence: Path,
                             receipt: dict, summaries: list,
                             request_line: str = ANALYZER_REQUEST_LINE) -> None:
    """Run the selected analyzer profile from the trusted merge-base driver."""
    selected_profile = ANALYZER_PROFILE_BY_LINE[request_line]
    profile_started = time.monotonic()
    setup_deadline = profile_started + ANALYZER_SETUP_BUDGET_SECONDS
    profile_deadline = profile_started + ANALYZER_PROFILE_BUDGET_SECONDS
    reasons = receipt["reasons"]
    analyzer_root = work / "analyzer"
    analyzer_root.mkdir(parents=True, exist_ok=True)
    (analyzer_root / "profile").mkdir()
    receipt["analyzer_request_line"] = request_line
    receipt["analyzer_profile"] = selected_profile
    receipt["profile"] = selected_profile
    request_path = candidate / "benchmarks/9700x/compiler-compare.request"
    try:
        request_stat = os.lstat(request_path)
        if not stat.S_ISREG(request_stat.st_mode):
            raise OSError("request is not a regular file")
        request_bytes = request_path.read_bytes()
        (analyzer_root / "request.txt").write_bytes(request_bytes)
        receipt["analyzer_request_sha256"] = hashlib.sha256(request_bytes).hexdigest()
        try:
            request_bytes.decode("utf-8")
        except UnicodeDecodeError as error:
            reasons.append(f"candidate analyzer request is not UTF-8: {error}")
    except OSError as error:
        reasons.append(f"candidate analyzer request could not be retained: {error}")
    clang_provenance = None
    try:
        clang_provenance = analyzer_clang_provenance()
        receipt["analyzer_clang_provenance"] = clang_provenance
        (analyzer_root / "clang.json").write_text(dumps(clang_provenance) + "\n", encoding="utf-8")
    except (OSError, subprocess.SubprocessError, RuntimeError, ValueError) as error:
        reasons.append(f"trusted Clang provenance is incomplete: {error}")
    if arguments.mode != "pull":
        reasons.append(f"{selected_profile['name']} is supported only by the authorized pull-compare route")
    inline_selector_deltas = request_selector_parent_deltas(candidate, arguments.head,
                                                            INLINE_ACCEPTANCE_REQUEST_LINE)
    if inline_selector_deltas is None:
        reasons.append("could not prove the inline acceptance selector count against every head parent")
    elif any(delta > 0 for delta in inline_selector_deltas):
        reasons.append(f"{selected_profile['name']} cannot be combined with the inline acceptance selector")
    ast_problem = getattr(arguments, "ast_pilot_request_problem", "")
    if ast_problem:
        reasons.append(ast_problem)
    elif getattr(arguments, "ast_pilot_requested", False):
        reasons.append(f"{selected_profile['name']} cannot be combined with the ast-pilot acceptance selector")
    if scaling_requested(candidate, arguments.base, arguments.head):
        reasons.append(f"{selected_profile['name']} cannot be combined with a scaling.request profile")
    drivers: dict[str, Path] = {}
    if not reasons:
        for role, commit in (("baseline", arguments.base), ("candidate", arguments.head)):
            mark(receipt, evidence, f"analyzer-build-driver-{role}")
            problem, driver = build_analyzer_driver(candidate, commit, role, analyzer_root, evidence, receipt,
                                                    setup_deadline)
            if problem:
                break
            if driver is not None:
                drivers[role] = driver
    try:
        if git(candidate, "rev-parse", "HEAD") != arguments.head:
            mark(receipt, evidence, "analyzer-restore-candidate")
            restore_timeout = analyzer_budget_timeout(setup_deadline, GIT_TIMEOUT_SECONDS)
            checkout_status = run(["git", "-C", str(candidate), "checkout", "--quiet", "--detach", arguments.head],
                                  candidate, evidence / "analyzer-restore-candidate.log", restore_timeout) if restore_timeout else 124
            if checkout_status != 0:
                reasons.append(f"candidate checkout restore exited {checkout_status}")
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError) as error:
        reasons.append(f"candidate checkout restore failed: {error}")
    database_directory = work / "analyzer-build"
    if len(drivers) == 2 and not reasons and clang_provenance is not None:
        mark(receipt, evidence, "analyzer-generate-candidate-database")
        measured = time.monotonic()
        generate_timeout = analyzer_budget_timeout(setup_deadline, ANALYZER_GENERATE_TIMEOUT_SECONDS)
        status = run([str(drivers["baseline"]), "generate", "--build-directory", str(database_directory), "--ci",
                      "--no-sanitize", "--no-fuzz", "--no-lto", "--linker", "DEFAULT", "--",
                      "-DBUSTER_UNITY_BUILD=OFF"], candidate, evidence / "analyzer-generate.log",
                     generate_timeout) if generate_timeout else 124
        receipt["timings"].setdefault("analyzer_setup_seconds", {})["compile_database_seconds"] = round(
            time.monotonic() - measured, 3)
        if status != 0:
            reasons.append(f"trusted baseline driver analyzer database generation exited {status}")
        else:
            database = database_directory / "compile_commands.json"
            try:
                database_stat = os.lstat(database)
                if not stat.S_ISREG(database_stat.st_mode):
                    raise OSError("compile_commands.json is not a regular file")
                (analyzer_root / "compile_commands.json").write_bytes(database.read_bytes())
            except OSError as error:
                reasons.append(f"candidate analyzer compile database is unavailable: {error}")
    helper_status = None
    if len(drivers) == 2 and not reasons and clang_provenance is not None:
        mark(receipt, evidence, "analyzer-matched-full-runs")
        measured = time.monotonic()
        helper_timeout = min(ANALYZER_HELPER_TIMEOUT_SECONDS,
                             analyzer_budget_timeout(profile_deadline, ANALYZER_PROFILE_BUDGET_SECONDS) -
                             ANALYZER_POSTPROCESS_RESERVE_SECONDS)
        if helper_timeout <= 0:
            helper_timeout = 0
        helper_status = run([str(drivers["baseline"]), "clang_analyze_benchmark", "--baseline-driver",
                             str(drivers["baseline"]), "--candidate-driver", str(drivers["candidate"]),
                             "--clang", clang_provenance["path"],
                             "--database", str(database_directory), "--output", str(analyzer_root / "profile")],
                            candidate, analyzer_root / "profile" / "helper.log", helper_timeout) if helper_timeout else 124
        receipt["timings"]["analyzer_measurement_seconds"] = round(time.monotonic() - measured, 3)
        if helper_status != 0:
            reasons.append(f"native analyzer profile helper exited {helper_status}; incomplete trials are retained")
    if drivers and len(drivers) == 2:
        source_problem = analyzer_source_immutability_problem(candidate)
        if source_problem:
            reasons.append(source_problem)
    raw = collect_analyzer_files(analyzer_root)
    summary, validation = analyzer_profile_summary(raw, receipt.get("identity", {}), request_line)
    receipt["analyzer"] = summary
    reasons.extend(item for item in validation if item not in reasons)
    try:
        mark(receipt, evidence, "analyzer-evidence")
        (analyzer_root / "summary.json").write_text(dumps(summary) + "\n", encoding="utf-8")
        required = (*ANALYZER_REQUIRED_FILES, "summary.json")
        problems, found = export_tree(analyzer_root, evidence / "analyzer", evidence, EVIDENCE_IGNORE, required)
        reasons.extend(problems)
        if found:
            reasons.append(f"analyzer evidence export omitted {len(found)} raw members; full independent replay requires every row and log")
        note_omissions(receipt.setdefault("evidence_omissions", {}), "analyzer", found)
    except OSError as error:
        reasons.append(f"analyzer evidence summary could not be retained: {error}")
    summaries[:] = [summary]
    if receipt.get("state") != "superseded":
        receipt["state"] = "measured" if not reasons and summary.get("status") == "complete" else "failed"


def measure_scaling(candidate: Path, bins: Path, work: Path, evidence: Path,
                    binaries: dict, omissions: dict, harness: Path | None = None) -> tuple[list[str], dict]:
    """Run every scaling series on the candidate from the checked-out base; (reasons, digest)."""
    reasons: list[str] = []
    bundles: dict = {}
    for name, arguments in SCALING_PROFILE["series"].items():
        output = work / "scaling" / name
        output.parent.mkdir(parents=True, exist_ok=True)
        status = run([*([str(harness)] if harness else ["./build.sh", "bench_throughput"]), "scale", "--compiler", str(bins / "ide-cand"),
                      "--output", str(output), *arguments], candidate, evidence / f"scaling-{name}.log",
                     SCALING_TIMEOUT_SECONDS)
        if output.is_dir():
            problems, found = export_tree(output, evidence / "scaling" / name, evidence, SCALING_IGNORE,
                                          SCALING_REQUIRED)
            reasons.extend(problems)
            note_omissions(omissions, f"scaling/{name}", found)
        documents, unreadable = read_exported_pair(evidence / "scaling" / name, SCALING_REQUIRED, f"scaling/{name}")
        reasons.extend(unreadable)
        bundles[name] = {"summary": documents[0], "metadata": documents[1]}
        if status != 0:
            reasons.append(f"bench_throughput scale ({name}) exited {status} (see scaling-{name}.log)")
    reasons.extend(classify_scaling(bundles, binaries))
    return reasons, scaling_digest(bundles)


def parse(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--lab", type=Path, required=True, help="trusted tools/uarch_lab.py")
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--closure-policy", choices=("legacy-rebuild", "snapshot-v1"), default="legacy-rebuild",
                        help="snapshot-v1 is qualification-only until approved before/after and A/A evidence")
    parser.add_argument("--closure-driver", type=Path, help="canonical immutable trusted native driver; required by snapshot-v1")
    parser.add_argument("--utility-owned-phases", action="store_true",
                        help="explicit main-only Utility research: individually supervise every child in either preparation leg")
    parser.add_argument("--main-owned-phases", action="store_true",
                        help="supported automatic-main native ownership; requires an authenticated dormant policy")
    parser.add_argument("--main-profile", choices=sorted(MAIN_PROFILES),
                        help="fixed named main recipe; requires --main-owned-phases")
    parser.add_argument("--mode", choices=sorted(MODES), required=True)
    for name in IDENTITY_KEYS[1:]:
        parser.add_argument("--" + name.replace("_", "-"), required=True)
    arguments = parser.parse_args(argv)
    if arguments.utility_owned_phases and (arguments.mode != "main" or arguments.closure_driver is None):
        parser.error("--utility-owned-phases requires main mode and the trusted --closure-driver")
    if arguments.main_owned_phases and (arguments.utility_owned_phases or arguments.mode != "main" or arguments.closure_driver is None):
        parser.error("--main-owned-phases requires main mode and trusted driver, exclusively from Utility")
    if arguments.main_profile is not None and not arguments.main_owned_phases:
        parser.error("--main-profile requires --main-owned-phases")
    if arguments.main_owned_phases and arguments.main_profile is None:
        arguments.main_profile = PROFILE["name"]
    return arguments


def checkpoint(receipt: dict, evidence: Path, phase: str) -> str:
    """Atomically persist the attempt so far as an incomplete (failed) receipt; returns '' or the write error.

    A kill cannot be handled, so this runs before each expensive phase. A checkpoint is never
    `measured`: only the final write after validation can carry that state.
    """
    shown = dict(receipt, state="failed" if receipt["state"] == "measured" else receipt["state"], phase=phase,
                 reasons=[*receipt["reasons"], f"attempt did not finish; last phase started: {phase}"])
    temporary = evidence / "receipt.json.tmp"
    problem = ""
    try:
        temporary.write_text(dumps(shown) + "\n", encoding="utf-8")
        os.replace(temporary, evidence / "receipt.json")
    except OSError as error:
        problem = f"receipt checkpoint at {phase} not persisted: {error}"
    return problem


def write_receipt(receipt: dict, evidence: Path) -> str:
    """Atomically write the final receipt; returns '' or the reason it could not be persisted."""
    temporary = evidence / "receipt.json.tmp"
    problem = ""
    try:
        temporary.write_text(dumps(receipt) + "\n", encoding="utf-8")
        os.replace(temporary, evidence / "receipt.json")
    except OSError as error:
        problem = f"final receipt not persisted: {error}"
    return problem


def mark(receipt: dict, evidence: Path, phase: str) -> None:
    """Record the phase about to start and checkpoint it."""
    receipt["phase"] = phase
    problem = checkpoint(receipt, evidence, phase)
    if problem:
        receipt.setdefault("notes", []).append(problem)


class ClosureCleanupUncertain(RuntimeError):
    """A native owner could not prove cleanup; preserve its work and stop the attempt."""


def closure_phase(arguments: argparse.Namespace, candidate: Path, work: Path, evidence: Path,
                  receipt: dict, operation: str) -> str:
    """Minimal bridge to the trusted native driver; all closure policy and filesystem work stay in C."""
    started = time.monotonic()
    record = evidence / f"closure-{operation}.json"
    expected = receipt.get("closure", {}).get("snapshot", {}).get("manifest_sha256", "-")
    driver = OWNED_PHASE_CONTEXT.driver if OWNED_PHASE_CONTEXT is not None else TRUSTED_ROOT / "build.sh"
    status = run([str(driver), "compiler_closure", operation, str(candidate),
                  str(work / "frozen-baseline"), arguments.base, arguments.base_tree, str(record), expected],
                 TRUSTED_ROOT, evidence / "closure.log", BUILD_TIMEOUT_SECONDS)
    receipt["timings"][f"closure_{operation}_seconds"] = round(time.monotonic() - started, 3)
    native, problem = read_exported_json(record)
    if isinstance(native, dict):
        receipt["closure"][operation] = native
        if operation == "snapshot" and type(native.get("harness_preparation_us")) is int and native["harness_preparation_us"] >= 0:
            receipt["timings"]["harness_preparation_seconds"] = native["harness_preparation_us"] / 1_000_000
    if not isinstance(native, dict) or native.get("cleanup_proven") is not True:
        receipt["cleanup_proven"] = False
        receipt["work_retained"] = str(work)
        (evidence / "cleanup-uncertain").write_text("Native closure child ownership or cleanup unproven; retain attempt root.\n", encoding="utf-8")
        raise ClosureCleanupUncertain(f"native frozen-baseline {operation} ownership/cleanup receipt missing or unproven")
    if status != 0 or problem or not isinstance(native, dict) or native.get("state") != "complete" or \
            type(native.get("harness_preparation_us")) is not int or native["harness_preparation_us"] < 0:
        problem = f"native frozen-baseline {operation} exited {status}: {problem or 'incomplete receipt'}"
    return problem


AST_PILOT_REQUIRED = ("acceptance.json", "probe/probe.json",
                      *(f"{cell}/{name}" for cell in AST_PILOT_ACCEPTANCE_PROFILE["cells"]
                        for name in ("summary.json", "compare.json", "a/lab.json", "b/lab.json")))


def measure_ast_pilot(arguments: argparse.Namespace, candidate: Path, binary: Path, work: Path, evidence: Path,
                      receipt: dict) -> None:
    """Run #3397's fixed -fc-ast-pilot profile on the frozen candidate build and record it in the receipt."""
    mark(receipt, evidence, "ast-pilot-acceptance")
    output = work / "ast-pilot-acceptance"
    helper = Path(__file__).with_name("ast_pilot_acceptance.py").resolve()
    measured = time.monotonic()
    status = run([sys.executable, "-B", str(helper),
                  "--lab", str(arguments.lab.resolve()), "--candidate-ide", str(binary),
                  "--repo-root", str(candidate), "--head-revision", arguments.head,
                  "--cpu", str(AST_PILOT_ACCEPTANCE_PROFILE["cpu"]), "--output", str(output)],
                 candidate, evidence / "ast-pilot-acceptance.log", AST_PILOT_ACCEPTANCE_TIMEOUT_SECONDS)
    receipt["timings"]["ast_pilot_acceptance_seconds"] = round(time.monotonic() - measured, 3)
    record = receipt["ast_pilot_acceptance"]
    record["exit"] = status
    errors, omissions = export_tree(output, evidence / "ast_pilot_acceptance", evidence, EVIDENCE_IGNORE, AST_PILOT_REQUIRED)
    record["evidence_omissions"] = omissions
    summary, problem = read_exported_json(evidence / "ast_pilot_acceptance" / "acceptance.json")
    if problem:
        errors.append(f"issue #3397 ast-pilot acceptance receipt unreadable: {problem}")
    else:
        record["summary"] = summary
        errors.extend(validate_ast_pilot_acceptance(summary, arguments.head, receipt["binaries"]["candidate"]["sha256"]))
    if status != 0:
        errors.append(f"issue #3397 ast-pilot acceptance exited {status}")
    record["errors"] = errors
    record["status"] = "complete" if not errors else "failed"


def measure_real_source(candidate: Path, bins: Path, work: Path, evidence: Path, base: str, head: str,
                        receipt: dict, harness: Path | None = None) -> None:
    """Stage the pinned inputs, run #2761's real-source leg on both binaries and record it in the receipt."""
    record = receipt["throughput_real_source"]
    output = work / "throughput-real-source"
    exported = evidence / "throughput_real_source"
    errors: list[str] = []
    status = None
    started = time.monotonic()
    try:
        roots, staging = throughput_real_source.stage(candidate, work / "real-source-inputs")
    except (OSError, ValueError, tarfile.TarError, zipfile.BadZipFile) as error:
        roots, staging = None, None
        errors.append(f"real-source inputs could not be staged: {error}")
    receipt["timings"]["real_source_staging_seconds"] = round(time.monotonic() - started, 3)
    if roots is not None:
        argv = [*([str(harness)] if harness else ["./build.sh", "bench_throughput"]), "run", "--baseline", str(bins / "ide-base"),
                "--candidate", str(bins / "ide-cand"), "--output", str(output), "--baseline-id", base,
                "--candidate-id", head, *throughput_real_source.harness_arguments(candidate, roots),
                *THROUGHPUT_REAL_SOURCE_PROFILE["arguments"]]
        measured = time.monotonic()
        status = run(argv, candidate, evidence / "throughput-real-source.log", REAL_SOURCE_TIMEOUT_SECONDS)
        receipt["timings"]["real_source_seconds"] = round(time.monotonic() - measured, 3)
        if output.is_dir():
            (output / "staging.json").write_text(dumps(staging) + "\n", encoding="utf-8")
            problems, omissions = export_tree(output, exported, evidence, REAL_SOURCE_IGNORE, REAL_SOURCE_REQUIRED)
            errors.extend(problems)
            record["evidence_omissions"] = omissions
        documents, unreadable = read_exported_pair(exported, ("summary.json", "metadata.json"), "throughput_real_source")
        errors.extend(unreadable)
        staged, problem = read_exported_json(exported / "staging.json")
        if problem:
            errors.append(f"exported evidence throughput_real_source/staging.json unreadable: {problem}")
        try:
            cpuinfo = (exported / "cpuinfo.txt").read_text(encoding="utf-8", errors="replace")
        except OSError as error:
            cpuinfo = None
            errors.append(f"exported evidence throughput_real_source/cpuinfo.txt unreadable: {error}")
        errors.extend(validate_throughput_real_source(status, documents[0], documents[1], staged, cpuinfo,
                                                      receipt["binaries"]))
        record["cells"] = throughput_real_source_digest(documents[0])
    record["exit"] = status
    record["errors"] = errors
    record["status"] = "complete" if not errors else "failed"


def measure_production_profile(candidate: Path, bins: Path, work: Path, evidence: Path, head: str,
                               receipt: dict, harness: Path | None = None) -> None:
    """Build the head's PGO/LTO compilers in an isolated clone, then time Release against PGO+LTO on the corpus."""
    record = receipt["production_profile"]
    source = work / "production-profile-source"
    output = source / "build" / "production-profile"
    exported = evidence / "production_profile"
    errors: list[str] = []
    status = None
    measured = time.monotonic()
    # A shared clone keeps the base checkout and its build tree untouched for the closure checks.
    steps = ((["git", "-c", "core.hooksPath=/dev/null", "clone", "--quiet", "--shared", "--no-checkout",
               str(candidate), str(source)], candidate, GIT_TIMEOUT_SECONDS),
             (["git", "-c", "gc.auto=0", "-c", "maintenance.auto=false", "-c", "core.hooksPath=/dev/null",
               "-C", str(source), "checkout", "--quiet", "--detach", head], candidate, GIT_TIMEOUT_SECONDS),
             (["./build.sh", "production_profile", *PRODUCTION_PROFILE_PROFILE["build_arguments"]], source,
              PRODUCTION_PROFILE_BUILD_TIMEOUT_SECONDS))
    missing = [tool for tool in PRODUCTION_PROFILE_HOST_TOOLS if shutil.which(tool) is None]
    if missing:
        errors.append(f"production profile host lacks {', '.join(missing)}; install them before requesting this profile")
    record["missing_tools"] = missing
    build_status = 1 if missing else 0
    for argv, cwd, timeout in steps:
        if build_status == 0:
            build_status = run(argv, cwd, evidence / "production-profile-build.log", timeout)
    receipt["timings"]["production_profile_build_seconds"] = round(time.monotonic() - measured, 3)
    if output.is_dir():
        problems, record["evidence_omissions"] = export_tree(output, exported, evidence, PRODUCTION_PROFILE_IGNORE,
                                                             PRODUCTION_PROFILE_REQUIRED)
        errors.extend(problems)
    compilers = {}
    for role, tree in PRODUCTION_PROFILE_PROFILE["compilers"].items():
        built = output / tree / "Release" / "ide"
        if build_status == 0 and built.is_file():
            binary = bins / f"ide-production-{tree}"
            shutil.copyfile(built, binary)
            binary.chmod(0o755)
            compilers[role] = {"sha256": sha256(binary), "size_bytes": binary.stat().st_size, "revision": head, "tree": tree}
    record["binaries"] = compilers
    texts = {}
    for name in PRODUCTION_PROFILE_REQUIRED:
        try:
            texts[name] = (exported / name).read_text(encoding="utf-8", errors="replace")
        except OSError as error:
            texts[name] = None
            errors.append(f"production profile {name} unreadable: {error}")
    documents = [None, None]
    if len(compilers) == 2:
        timing = work / "production-profile-throughput"
        argv = [*([str(harness)] if harness else ["./build.sh", "bench_throughput"]), "run",
                "--baseline", str(bins / "ide-production-release"), "--candidate", str(bins / "ide-production-pgo-lto"),
                "--output", str(timing), "--baseline-id", head + "-release", "--candidate-id", head + "-pgo-lto",
                *PRODUCTION_PROFILE_PROFILE["arguments"]]
        measured = time.monotonic()
        status = run(argv, candidate, evidence / "production-profile-throughput.log", THROUGHPUT_TIMEOUT_SECONDS)
        receipt["timings"]["production_profile_throughput_seconds"] = round(time.monotonic() - measured, 3)
        if timing.is_dir():
            problems, omissions = export_tree(timing, exported / "throughput", evidence, EVIDENCE_IGNORE, THROUGHPUT_REQUIRED)
            errors.extend(problems)
            record["throughput_evidence_omissions"] = omissions
        documents, unreadable = read_exported_pair(exported / "throughput", THROUGHPUT_REQUIRED, "production_profile/throughput")
        errors.extend(unreadable)
    errors.extend(validate_production_profile(build_status, texts["summary.txt"], texts["evidence/phases.tsv"], head,
                                              status, documents[0], documents[1], compilers))
    record["cells"] = throughput_real_source_digest(documents[0])
    record["build_exit"] = build_status
    record["exit"] = status
    record["errors"] = errors
    record["status"] = "complete" if not errors else "failed"


def measure_compiler_probes(candidate: Path, work: Path, evidence: Path, base: str, head: str, receipt: dict) -> None:
    """Build the trusted native probe harness, run #2761's probe leg and record it with independently hashed inputs."""
    record = receipt["compiler_probes"]
    output = work / "compiler-probes"
    scratch = work / "compiler-probes-work"
    harness = work / "compiler-probes-harness"
    exported = evidence / "compiler_probes"
    log = evidence / "compiler-probes.log"
    errors: list[str] = []
    measured = time.monotonic()
    # The harness comes from this trusted checkout's tools/, never from the candidate.
    status = run(["clang", *COMPILER_PROBES_HARNESS_FLAGS,
                  str(Path(__file__).resolve().with_name(COMPILER_PROBES_HARNESS)), "-o", str(harness)],
                 candidate, log, GIT_TIMEOUT_SECONDS)
    record["harness_exit"] = status
    if status == 0:
        status = run([str(harness), str(candidate), base, head, str(scratch), str(output)], candidate, log,
                     COMPILER_PROBES_TIMEOUT_SECONDS)
    receipt["timings"]["compiler_probes_seconds"] = round(time.monotonic() - measured, 3)
    binaries: dict = {}
    sources: dict = {}
    for probe, definition in COMPILER_PROBES_PROFILE["probes"].items():
        copied = scratch / "probe-sources" / Path(definition["source"]).name
        if copied.is_file():
            sources[probe] = sha256(copied)
        for role, revision in zip(COMPILER_PROBES_ROLES, (base, head)):
            built = output / "bin" / f"{probe}-{role}"
            if built.is_file():
                binaries.setdefault(probe, {})[role] = {"sha256": sha256(built), "size_bytes": built.stat().st_size,
                                                        "revision": revision}
    record["binaries"] = binaries
    record["probe_source_sha256"] = sources
    summary = None
    if output.is_dir():
        problems, record["evidence_omissions"] = export_tree(output, exported, evidence, COMPILER_PROBES_IGNORE,
                                                             COMPILER_PROBES_REQUIRED)
        errors.extend(problems)
        summary, problem = read_exported_json(exported / "summary.json")
        if problem:
            errors.append(f"exported evidence compiler_probes/summary.json unreadable: {problem}")
    errors.extend(validate_compiler_probes(status, summary, base, head, binaries, sources))
    record["cells"] = compiler_probes_digest(summary)
    record["exit"] = status
    record["errors"] = errors
    record["status"] = "complete" if not errors else "failed"


def measure_frontend_selector(candidate: Path, bins: Path, work: Path, evidence: Path, receipt: dict) -> None:
    """Run #2761's frontend and selector probes from the base checkout on both binaries and record them."""
    record = receipt["frontend_selector"]
    profile = FRONTEND_SELECTOR_PROFILE
    exported = evidence / "frontend_selector"
    roles = {"baseline": bins / "ide-base", "candidate": bins / "ide-cand"}
    errors: list[str] = []
    scaling_output = work / "frontend-scaling"
    measured = time.monotonic()
    scaling_status = run([sys.executable, "-B", "tools/benchmark_frontend_scaling.py", "--baseline", str(roles["baseline"]),
                          "--candidate", str(roles["candidate"]), "--out", str(scaling_output),
                          *profile["frontend_scaling"]["arguments"]],
                         candidate, evidence / "frontend-scaling.log", FRONTEND_SCALING_TIMEOUT_SECONDS)
    receipt["timings"]["frontend_scaling_seconds"] = round(time.monotonic() - measured, 3)
    selection_output = work / "selection"
    measured = time.monotonic()
    selection_status = run([sys.executable, "-B", "tools/selection_benchmark.py", "--compiler", str(roles["baseline"]),
                            "--candidate", str(roles["candidate"]), "--workloads", str(work / "selection-sources"),
                            "--output", str(selection_output / "report.json"), *profile["selection"]["arguments"]],
                           candidate, evidence / "selection.log", SELECTION_TIMEOUT_SECONDS)
    receipt["timings"]["selection_seconds"] = round(time.monotonic() - measured, 3)
    bench_output = work / "ide-bench"
    bench_output.mkdir(parents=True, exist_ok=True)
    bench = {"schema": FRONTEND_SELECTOR_SCHEMA, "cpu": profile["cpu"], "cpu_model": cpu_model(),
             "binaries": {role: sha256(path) for role, path in roles.items()}, "runs": []}
    measured = time.monotonic()
    for pair in range(profile["ide_bench"]["pairs"]):
        for role in ("baseline", "candidate") if pair % 2 == 0 else ("candidate", "baseline"):
            log = bench_output / f"{len(bench['runs']):02d}-{role}.log"
            status = run(["taskset", "-c", str(profile["cpu"]), str(roles[role]), "bench"], candidate, log,
                         IDE_BENCH_TIMEOUT_SECONDS)
            text = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
            lines = [line for line in text.splitlines() if line.startswith("BENCH_C_FRONTEND ")]
            bench["runs"].append({"role": role, "exit": status, "line": lines[0] if len(lines) == 1 else None})
    receipt["timings"]["ide_bench_seconds"] = round(time.monotonic() - measured, 3)
    if {role: sha256(path) for role, path in roles.items()} != bench["binaries"]:
        bench["binaries"] = None
    (bench_output / "ide-bench.json").write_text(dumps(bench) + "\n", encoding="utf-8")
    record["evidence_omissions"] = {}
    for name, source, ignore, required in (("frontend_scaling", scaling_output, FRONTEND_SCALING_IGNORE,
                                            FRONTEND_SCALING_REQUIRED),
                                           ("selection", selection_output, EVIDENCE_IGNORE, SELECTION_REQUIRED),
                                           ("ide_bench", bench_output, EVIDENCE_IGNORE, ("ide-bench.json",))):
        if source.is_dir():
            problems, record["evidence_omissions"][name] = export_tree(source, exported / name, evidence, ignore, required)
            errors.extend(problems)
        else:
            errors.append(f"frontend-selector {name} produced no output")
    documents = {}
    for key, path in (("provenance", "frontend_scaling/provenance.json"), ("results", "frontend_scaling/results.json"),
                      ("report", "selection/report.json"), ("bench", "ide_bench/ide-bench.json")):
        documents[key], problem = read_exported_json(exported / path)
        if problem:
            errors.append(f"exported evidence frontend_selector/{path} unreadable: {problem}")
    errors.extend(validate_frontend_selector(scaling_status, documents["provenance"], documents["results"],
                                             selection_status, documents["report"], documents["bench"],
                                             receipt["binaries"]))
    record["cells"] = frontend_selector_digest(documents["results"], documents["report"], documents["bench"])
    record["exits"] = {"frontend_scaling": scaling_status, "selection": selection_status}
    record["errors"] = errors
    record["status"] = "complete" if not errors else "failed"


def measure(arguments: argparse.Namespace, candidate: Path, work: Path, evidence: Path, bins: Path, log: Path,
            receipt: dict, summaries: list) -> None:
    """Build both revisions and run the lab, corpus and scaling legs; the lab summary goes to summaries."""
    if arguments.mode == "pull" and getattr(arguments, "analyzer_profile_requested", False):
        measure_analyzer_profile(arguments, candidate, work, evidence, receipt, summaries,
                                 arguments.analyzer_request_line)
        return
    reasons = receipt["reasons"]
    selected_profile = named_main_profile(arguments.main_profile) if getattr(arguments, "main_owned_phases", False) else PROFILE
    request_problem = getattr(arguments, "analyzer_profile_request_problem", "")
    if request_problem and request_problem not in reasons:
        reasons.append(request_problem)
    summary = None
    snapshot_closure = getattr(arguments, "closure_policy", "legacy-rebuild") == "snapshot-v1"
    receipt["preparation_policy"] = "snapshot-v1" if snapshot_closure else "legacy-rebuild"
    harness = candidate / "build/throughput-tools/throughput" if snapshot_closure else None
    if snapshot_closure:
        receipt["closure"] = {"policy": "snapshot-v1", "fallback": None}
    if OWNED_PHASE_CONTEXT is not None:
        receipt["phase_ownership"].update(candidate_root=str(candidate.resolve()), binaries_root=str(bins.resolve()),
                                          lab_path=str(arguments.lab.resolve()))

    inline_requested = arguments.mode == "pull" and inline_acceptance_requested(candidate)
    receipt["inline_acceptance"] = {"requested": inline_requested,
        "request_line": INLINE_ACCEPTANCE_REQUEST_LINE if inline_requested else None,
        "profile": INLINE_ACCEPTANCE_PROFILE if inline_requested else None,
        "status": "pending" if inline_requested else "not requested"}
    # Selected in main() before any owned phase starts, so its git probes add no owned capture rows.
    ast_requested = arguments.mode == "pull" and getattr(arguments, "ast_pilot_requested", False)
    problem = getattr(arguments, "ast_pilot_request_problem", "") if arguments.mode == "pull" else ""
    if problem and problem not in reasons:
        reasons.append(problem)
    if ast_requested:
        receipt["ast_pilot_acceptance"] = {"requested": True, "request_line": AST_PILOT_ACCEPTANCE_REQUEST_LINE,
                                           "profile": AST_PILOT_ACCEPTANCE_PROFILE, "status": "pending"}
    real_requested = arguments.mode == "pull" and getattr(arguments, "real_source_requested", False)
    problem = getattr(arguments, "real_source_request_problem", "") if arguments.mode == "pull" else ""
    if problem and problem not in reasons:
        reasons.append(problem)
    if real_requested:
        receipt["throughput_real_source"] = {"requested": True, "request_line": THROUGHPUT_REAL_SOURCE_REQUEST_LINE,
                                             "profile": THROUGHPUT_REAL_SOURCE_PROFILE, "status": "pending"}
    production_requested = arguments.mode == "pull" and getattr(arguments, "production_profile_requested", False)
    problem = getattr(arguments, "production_profile_request_problem", "") if arguments.mode == "pull" else ""
    if problem and problem not in reasons:
        reasons.append(problem)
    if production_requested:
        receipt["production_profile"] = {"requested": True, "request_line": PRODUCTION_PROFILE_REQUEST_LINE,
                                         "profile": PRODUCTION_PROFILE_PROFILE, "status": "pending"}
        # Its build leg and the other long opt-in legs together would not fit the compare-pull job.
        if real_requested or ast_requested:
            reasons.append("production-profile-v1 cannot be combined with real-source throughput or ast-pilot acceptance")
    probes_requested = arguments.mode == "pull" and getattr(arguments, "compiler_probes_requested", False)
    problem = getattr(arguments, "compiler_probes_request_problem", "") if arguments.mode == "pull" else ""
    if problem and problem not in reasons:
        reasons.append(problem)
    if probes_requested:
        receipt["compiler_probes"] = {"requested": True, "request_line": COMPILER_PROBES_REQUEST_LINE,
                                      "profile": COMPILER_PROBES_PROFILE, "status": "pending"}
        # One long opt-in leg per compare-pull job keeps the unchanged 90-minute budget provable.
        if real_requested or ast_requested or production_requested:
            reasons.append("compiler-probes-v1 cannot be combined with real-source throughput, ast-pilot acceptance "
                           "or the PGO/LTO production profile")
    selector_requested = arguments.mode == "pull" and getattr(arguments, "frontend_selector_requested", False)
    problem = getattr(arguments, "frontend_selector_request_problem", "") if arguments.mode == "pull" else ""
    if problem and problem not in reasons:
        reasons.append(problem)
    if selector_requested:
        receipt["frontend_selector"] = {"requested": True, "request_line": FRONTEND_SELECTOR_REQUEST_LINE,
                                        "profile": FRONTEND_SELECTOR_PROFILE, "status": "pending"}
        # One long opt-in leg per compare-pull job keeps the leg's measured cost inside its unchanged budget.
        if real_requested or ast_requested or production_requested or probes_requested:
            reasons.append("frontend-selector-v1 cannot be combined with real-source throughput, ast-pilot "
                           "acceptance, the PGO/LTO production profile or compiler-probes-v1")
    if not reasons:
        for role, commit in (("baseline", arguments.base), ("candidate", arguments.head), ("closure", arguments.base)):
            mark(receipt, evidence, f"build-{role}")
            if role == "closure" and snapshot_closure:
                started = time.monotonic()
                checkout = ["git", "-c", "gc.auto=0", "-c", "maintenance.auto=false", "-c", "core.hooksPath=/dev/null",
                            "-C", str(candidate), "checkout", "--quiet", "--detach", commit]
                status = run(checkout,
                             candidate, log, GIT_TIMEOUT_SECONDS)
                receipt["timings"]["closure_checkout_seconds"] = round(time.monotonic() - started, 3)
                problem = f"baseline checkout exited {status}" if status else closure_phase(arguments, candidate, work, evidence, receipt, "restore")
            else:
                problem, seconds = build(candidate, commit, log)
                receipt["timings"]["build_seconds"][role] = round(seconds, 3)
            if problem:
                reasons.append(problem)
                break
            if role != "closure":
                binary = bins / ("ide-base" if role == "baseline" else "ide-cand")
                shutil.copyfile(candidate / "build" / "Release" / "ide", binary)
                binary.chmod(0o755)
                shutil.copyfile(candidate / "build" / "CMakeCache.txt", evidence / f"{role}.CMakeCache.txt")
                receipt["binaries"][role] = {"sha256": sha256(binary), "size_bytes": binary.stat().st_size,
                                             "revision": commit}
                if role == "baseline" and snapshot_closure:
                    mark(receipt, evidence, "closure-snapshot")
                    problem = closure_phase(arguments, candidate, work, evidence, receipt, "snapshot")
                    if problem:
                        reasons.append(problem)
                        break
                if role == "candidate" and inline_requested:
                    mark(receipt, evidence, "inline-acceptance")
                    inline_dir = work / "inline-acceptance"
                    helper = Path(__file__).with_name("inline_acceptance.py").resolve()
                    measured = time.monotonic()
                    inline_status = run([sys.executable, "-B", str(helper),
                                         "--lab", str(arguments.lab.resolve()), "--candidate-ide", str(binary),
                                         "--repo-root", str(candidate), "--head-revision", arguments.head,
                                         "--cpu", str(PROFILE["cpu"]), "--output", str(inline_dir)],
                                        candidate, evidence / "inline-acceptance.log", INLINE_ACCEPTANCE_TIMEOUT_SECONDS)
                    receipt["timings"]["inline_acceptance_seconds"] = round(time.monotonic() - measured, 3)
                    inline = receipt["inline_acceptance"]
                    inline["exit"] = inline_status
                    export_problems, omissions = export_tree(inline_dir, evidence / "inline_acceptance", evidence,
                                                              EVIDENCE_IGNORE, ("acceptance.json", "stage1/identities.json",
                                                                                "selfhost/identities.json"))
                    inline["evidence_omissions"] = omissions
                    errors = list(export_problems)
                    inline_summary, problem = read_exported_json(evidence / "inline_acceptance" / "acceptance.json")
                    if problem:
                        errors.append(f"issue #48 inline self-host acceptance receipt unreadable: {problem}")
                    else:
                        inline["summary"] = inline_summary
                        errors.extend(validate_inline_acceptance(inline_summary, arguments.head,
                                                                 receipt["binaries"]["candidate"]["sha256"]))
                    if inline_status != 0:
                        errors.append(f"issue #48 inline self-host acceptance exited {inline_status}")
                    inline["errors"] = errors
                    inline["status"] = "complete" if not errors else "failed"
                if role == "candidate" and ast_requested:
                    measure_ast_pilot(arguments, candidate, binary, work, evidence, receipt)

    if not reasons:
        lab = work / "lab"
        mark(receipt, evidence, "lab")
        measured = time.monotonic()
        lab_command = [sys.executable, "-B", str(arguments.lab.resolve()), "compare",
                       "--baseline", str(bins / "ide-base"), "--candidate", str(bins / "ide-cand"),
                       "--repo-root", str(candidate), "--cpu", str(selected_profile["cpu"]), "--output", str(lab),
                       "--target-minutes", str(selected_profile["target_minutes"]), "--warmups", str(selected_profile["warmups"])]
        if selected_profile is MAIN40_PROFILE:
            lab_command += ["--pairs", "40", "--seed", "20261003", "--min-effect", "0.5"]
        status = run(lab_command, candidate, evidence / "lab.log",
                     300 if selected_profile is MAIN40_PROFILE else LAB_TIMEOUT_SECONDS)
        receipt["timings"]["measurement_seconds"] = round(time.monotonic() - measured, 3)
        receipt["lab"]["exit"] = status
        mark(receipt, evidence, "lab-evidence")
        reasons.extend(collect_evidence(lab, evidence, receipt.setdefault("evidence_omissions", {})))
        summary, problem = read_exported_json(evidence / "lab" / "summary.json")
        if problem:
            reasons.append(f"lab summary unreadable: {problem}")
        else:
            summaries[:] = [summary]
        if status != 0:
            reasons.append(f"uarch_lab compare exited {status}")
        if getattr(arguments, "main_owned_phases", False):
            validity = classify(summary, receipt["binaries"], expected_profile=selected_profile["name"])
            if reasons or validity:
                reasons.extend(item for item in validity if item not in reasons)
                if OWNED_PHASE_CONTEXT is not None:
                    OWNED_PHASE_CONTEXT.stopped = True
                    receipt["phase_ownership"]["state"] = "failed"
                receipt["work_retained"] = str(work)
                raise OwnedPhaseFailed("MAIN-owned lab data incomplete; no later child is admitted")
        mark(receipt, evidence, "throughput")
        measured = time.monotonic()
        corpus, receipt["throughput"] = measure_throughput(candidate, bins, work, evidence, arguments.base,
                                                           arguments.head, receipt["binaries"],
                                                           receipt.setdefault("evidence_omissions", {}), harness)
        receipt["timings"]["throughput_seconds"] = round(time.monotonic() - measured, 3)
        reasons.extend(corpus)
        if arguments.mode == "pull" and scaling_requested(candidate, arguments.base, arguments.head):
            mark(receipt, evidence, "scaling")
            measured = time.monotonic()
            receipt["scaling_profile"] = SCALING_PROFILE
            scaled, receipt["scaling"] = measure_scaling(candidate, bins, work, evidence, receipt["binaries"],
                                                      receipt.setdefault("evidence_omissions", {}), harness)
            receipt["timings"]["scaling_seconds"] = round(time.monotonic() - measured, 3)
            reasons.extend(scaled)
        if real_requested:
            mark(receipt, evidence, "throughput-real-source")
            measure_real_source(candidate, bins, work, evidence, arguments.base, arguments.head, receipt, harness)
        if production_requested:
            mark(receipt, evidence, "production-profile")
            measure_production_profile(candidate, bins, work, evidence, arguments.head, receipt, harness)
        if probes_requested:
            mark(receipt, evidence, "compiler-probes")
            measure_compiler_probes(candidate, work, evidence, arguments.base, arguments.head, receipt)
        if selector_requested:
            mark(receipt, evidence, "frontend-selector")
            measure_frontend_selector(candidate, bins, work, evidence, receipt)
        mark(receipt, evidence, "validate")
        raw_closure = {}
        if snapshot_closure:
            problem = closure_phase(arguments, candidate, work, evidence, receipt, "verify")
            if problem:
                reasons.append(problem)
            for operation in ("snapshot", "restore", "verify"):
                try:
                    with os.fdopen(os.open(evidence / f"closure-{operation}.json.manifest.tsv",
                                           os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)), "rb") as reader:
                        raw_closure[operation] = reader.read(EVIDENCE_MEMBER_LIMIT + 1)
                except OSError:
                    pass
        if OWNED_PHASE_CONTEXT is not None:
            OWNED_PHASE_CONTEXT.finish()
            owned_raw = {}
            for row in receipt["phase_ownership"]["phases"]:
                path = OWNED_PHASE_CONTEXT.directory / row["file"]
                try:
                    owned_raw[row["file"]] = {"receipt": bounded_owned_member(path),
                        "command": bounded_owned_member(Path(str(path) + ".argv")),
                        "stdout": bounded_owned_member(Path(str(path) + ".stdout"), empty=True),
                        "stderr": bounded_owned_member(Path(str(path) + ".stderr"), empty=True),
                        "bootstrap": bounded_owned_member(Path(str(path) + ".bootstrap.complete"))}
                except (OSError, ValueError):
                    pass
            raw_closure["owned_phases"] = owned_raw
        try:
            raw_closure["owned_throughput"] = {"summary": bounded_owned_member(evidence / "throughput/summary.json"),
                "metadata": bounded_owned_member(evidence / "throughput/metadata.json")}
        except (OSError, ValueError):
            pass
        if snapshot_closure or OWNED_PHASE_CONTEXT is not None:
            from compiler_owned_phase import UTILITY_POPULATION_SCHEMA, MAIN_POPULATION_SCHEMA
            reasons.extend(validate_closure(receipt, raw_closure,
                expected_policy="snapshot-v1" if snapshot_closure else "legacy-rebuild", require_owned_phases=True,
                expected_phase_schema=MAIN_POPULATION_SCHEMA if getattr(arguments, "main_owned_phases", False) else
                                      UTILITY_POPULATION_SCHEMA if getattr(arguments, "utility_owned_phases", False) else None,
                require_owned_preflight=getattr(OWNED_PHASE_CONTEXT, "owned_preflight", False),
                expected_profile=selected_profile["name"] if getattr(arguments, "main_owned_phases", False) else None))
        for role, name in (("baseline", "ide-base"), ("candidate", "ide-cand")):
            if sha256(bins / name) != receipt["binaries"][role]["sha256"]:
                reasons.append(f"{role} binary changed during measurement")
        reasons.extend(classify(summary, receipt["binaries"],
                                expected_profile=selected_profile["name"] if getattr(arguments, "main_owned_phases", False) else None))
        if isinstance(summary, dict):
            receipt["lab"].update(schema=summary.get("schema"), verdict=summary.get("verdict"),
                                  complete_pairs=(summary.get("plan") or {}).get("complete_pairs"))
        if not reasons:
            receipt["state"] = "measured"
    if inline_requested:
        reasons.extend(receipt["inline_acceptance"].get("errors", []))
        if receipt["inline_acceptance"]["status"] == "pending":
            receipt["inline_acceptance"]["status"] = "failed"
            reasons.append("issue #48 inline self-host acceptance was requested but did not reach its candidate-build phase")
        if receipt["inline_acceptance"]["status"] != "complete":
            receipt["state"] = "failed"
    if ast_requested:
        record = receipt["ast_pilot_acceptance"]
        reasons.extend(record.get("errors", []))
        if record["status"] == "pending":
            record["status"] = "failed"
            reasons.append("issue #3397 ast-pilot acceptance was requested but did not reach its candidate-build phase")
        if record["status"] != "complete":
            receipt["state"] = "failed"
    if real_requested:
        record = receipt["throughput_real_source"]
        reasons.extend(record.get("errors", []))
        if record["status"] == "pending":
            record["status"] = "failed"
            reasons.append("real-source throughput was requested but did not reach its measurement phase")
        if record["status"] != "complete":
            receipt["state"] = "failed"
    if production_requested:
        record = receipt["production_profile"]
        reasons.extend(record.get("errors", []))
        if record["status"] == "pending":
            record["status"] = "failed"
            reasons.append("the PGO/LTO production profile was requested but did not reach its measurement phase")
        if record["status"] != "complete":
            receipt["state"] = "failed"
    if probes_requested:
        record = receipt["compiler_probes"]
        reasons.extend(record.get("errors", []))
        if record["status"] == "pending":
            record["status"] = "failed"
            reasons.append("the compiler probes were requested but did not reach their measurement phase")
        if record["status"] != "complete":
            receipt["state"] = "failed"
    if selector_requested:
        record = receipt["frontend_selector"]
        reasons.extend(record.get("errors", []))
        if record["status"] == "pending":
            record["status"] = "failed"
            reasons.append("the frontend-selector probes were requested but did not reach their measurement phase")
        if record["status"] != "complete":
            receipt["state"] = "failed"



def main(argv: list[str] | None = None) -> int:
    global OWNED_PHASE_CONTEXT
    arguments = parse(argv)
    started = time.monotonic()
    started_at = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    candidate = arguments.candidate.resolve()
    try:
        work, evidence = plan_scratch(candidate, arguments.lab, arguments.work, arguments.evidence)
    except ScratchError as error:
        print(f"compiler_compare: refusing scratch paths: {error}", file=sys.stderr)
        return 2
    prepare_scratch(work)
    prepare_scratch(evidence)
    analyzer_request_line, analyzer_request_problem = None, ""
    if arguments.mode == "pull":
        analyzer_request_line, analyzer_request_problem = analyzer_profile_request_selection(candidate, arguments.head)
    analyzer_requested = analyzer_request_line is not None
    arguments.analyzer_profile_requested = analyzer_requested
    arguments.analyzer_request_line = analyzer_request_line
    arguments.analyzer_profile_request_problem = analyzer_request_problem
    arguments.ast_pilot_requested, arguments.ast_pilot_request_problem = (
        ast_pilot_request_selection(candidate, arguments.head) if arguments.mode == "pull" else (False, ""))
    arguments.real_source_requested, arguments.real_source_request_problem = (
        real_source_request_selection(candidate, arguments.head) if arguments.mode == "pull" else (False, ""))
    arguments.production_profile_requested, arguments.production_profile_request_problem = (
        production_profile_request_selection(candidate, arguments.head) if arguments.mode == "pull" else (False, ""))
    arguments.compiler_probes_requested, arguments.compiler_probes_request_problem = (
        compiler_probes_request_selection(candidate, arguments.head) if arguments.mode == "pull" else (False, ""))
    arguments.frontend_selector_requested, arguments.frontend_selector_request_problem = (
        frontend_selector_request_selection(candidate, arguments.head) if arguments.mode == "pull" else (False, ""))
    identity = {key: getattr(arguments, key) for key in IDENTITY_KEYS}
    utility = getattr(arguments, "utility_owned_phases", False)
    main_owned = getattr(arguments, "main_owned_phases", False)
    selected_profile = named_main_profile(arguments.main_profile) if main_owned else PROFILE
    early_owned = utility or main_owned or (arguments.mode == "main" and arguments.closure_policy == "snapshot-v1")
    receipt = {"schema": RECEIPT_SCHEMA, "mode": arguments.mode, "state": "failed", "reasons": [], "identity": identity,
               "profile": ANALYZER_PROFILE_BY_LINE[analyzer_request_line] if analyzer_requested else selected_profile,
               "throughput_profile": THROUGHPUT_PROFILE if not analyzer_requested else None,
               "host": {"hostname": socket.gethostname(), "cpu_model": "" if early_owned else cpu_model()},
               "toolchain": {} if early_owned else toolchain(), "binaries": {}, "lab": {},
               "timings": {"started_at": started_at, "build_seconds": {}},
               "inline_acceptance": {"requested": False, "status": "not requested"}}
    reasons = receipt["reasons"]
    if analyzer_request_problem:
        reasons.append(analyzer_request_problem)
    if analyzer_requested and arguments.real_source_requested:
        reasons.append("an analyzer profile cannot be combined with real-source throughput")
    log = evidence / "build.log"
    bins = work / "bin"
    bins.mkdir()
    summary = None
    initialization_failed, initialization_aborted = False, None
    if early_owned:
        receipt["preparation_policy"] = arguments.closure_policy
        try:
            OWNED_PHASE_CONTEXT = NativePhaseContext(arguments.closure_driver, work, evidence, receipt, utility=utility, owned_preflight=True, main_owned=main_owned)
            receipt["phase_ownership"].update(candidate_root=str(candidate), binaries_root=str(bins.resolve()),
                                               lab_path=str(arguments.lab.resolve()))
            receipt["host"]["cpu_model"] = cpu_model()
            receipt["toolchain"] = toolchain()
        except BaseException as error:
            initialization_failed = True
            if isinstance(receipt.get("phase_ownership"), dict):
                receipt["phase_ownership"]["state"] = "failed"
            if OWNED_PHASE_CONTEXT is not None:
                OWNED_PHASE_CONTEXT.stopped = True
            reasons.append(f"native phase initialization failed: {error.__class__.__name__}: {error}")
            receipt["state"] = "failed"
            receipt["work_retained"] = str(work)
            if not isinstance(error, Exception):
                initialization_aborted = error

    # Identity first. A main commit has the base on its first-parent chain (its
    # first parent, or a range's measured ancestor) and, when a queue merge
    # produced it, the pull request head as second parent (else it is its own
    # pull head); a pull request head is its own pull head and descends from
    # the base (its merge base). Both trees must match.
    problem = host_problem(receipt)
    if problem:
        reasons.append(problem)
    observed = None
    if not initialization_failed:
        try:
            if arguments.mode == "main":
                second = captured_run(["git", "-C", str(candidate), "rev-parse", "--verify", "--quiet", "HEAD^2"],
                                        capture_output=True, text=True, timeout=GIT_TIMEOUT_SECONDS, check=False).stdout.strip()
                chain = git(candidate, "rev-list", "--first-parent", f"--max-count={RECONCILE_DEPTH}", "HEAD^1").split()
                parents = (arguments.base if arguments.base in chain else "base is not on the first-parent chain",
                           second or git(candidate, "rev-parse", "HEAD"))
                if arguments.base in chain:
                    receipt["coverage"] = {"first_parent": chain[0], "range": str(chain.index(arguments.base) + 1)}
            else:
                ancestry = captured_run(["git", "-C", str(candidate), "merge-base", "--is-ancestor", arguments.base, "HEAD"],
                                          capture_output=True, timeout=GIT_TIMEOUT_SECONDS, check=False).returncode == 0
                parents = (arguments.base if ancestry else "base is not an ancestor", git(candidate, "rev-parse", "HEAD"))
            observed = (git(candidate, "rev-parse", "HEAD"), *parents, git(candidate, "rev-parse", "HEAD^{tree}"),
                        git(candidate, "rev-parse", arguments.base + "^{tree}"))
        except BaseException as error:
            if not early_owned and not isinstance(error, (subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError)):
                raise
            observed = None
            reasons.append(f"candidate checkout cannot be inspected: {error}")
            if early_owned:
                initialization_failed = True
                receipt["work_retained"] = str(work)
                if not isinstance(error, Exception):
                    initialization_aborted = error
    expected = (arguments.head, arguments.base, arguments.pull_head, arguments.head_tree, arguments.base_tree)
    if observed is not None and observed != expected:
        reasons.append(f"candidate checkout {observed} does not match the authorized identity {expected}")

    # Skip a pull request whose head already moved; an unknown answer measures.
    # A main commit stays on main as main moves on, so it is always measured.
    live = queue_head(arguments.repository, arguments.ref) if not reasons and arguments.mode == "pull" else None
    if live is not None and live != arguments.head:
        receipt["state"] = "superseded"
        reasons.append(f"{arguments.ref} names {live or 'nothing'} before measurement")
    elif live is None and not reasons and arguments.mode == "pull":
        receipt["notes"] = [f"{arguments.ref} could not be read before measurement; measured anyway"]

    summaries: list = []
    aborted = initialization_aborted
    try:
        if not early_owned and getattr(arguments, "closure_policy", "legacy-rebuild") == "snapshot-v1":
            if getattr(arguments, "closure_driver", None) is None:
                raise OwnedPhaseFailed("snapshot-v1 requires the trusted native --closure-driver")
            OWNED_PHASE_CONTEXT = NativePhaseContext(arguments.closure_driver, work, evidence, receipt)
        if not initialization_failed:
            measure(arguments, candidate, work, evidence, bins, log, receipt, summaries)
    except BaseException as error:  # noqa: BLE001 - recorded, then re-raised after the receipt is written
        reasons.append(f"attempt aborted in phase {receipt.get('phase', 'start')}: {error.__class__.__name__}: {error}")
        receipt["state"] = "failed"
        if not isinstance(error, Exception):
            aborted = error
    if OWNED_PHASE_CONTEXT is not None:
        OWNED_PHASE_CONTEXT.finish()
        OWNED_PHASE_CONTEXT = None
    summary = summaries[-1] if summaries else None
    receipt["timings"]["total_seconds"] = round(time.monotonic() - started, 3)
    receipt["timings"]["finished_at"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    receipt.pop("phase", None)
    if not receipt.get("evidence_omissions"):
        receipt.pop("evidence_omissions", None)
    persisted = write_receipt(receipt, evidence)
    if persisted:
        reasons.append(persisted)
        receipt["state"] = "failed"
        print(f"compiler_compare: {persisted}", file=sys.stderr)
    try:
        with arguments.summary.open("a", encoding="utf-8") as stream:
            stream.write(render(receipt, summary, receipt["state"], []) + "\n")
    except OSError as error:
        reasons.append(f"step summary not written: {error}")
        print(f"compiler_compare: step summary not written: {error}", file=sys.stderr)
        receipt["state"] = "failed"
    print(f"BENCH_COMPILER_{receipt['state'].upper()} " + "; ".join(reasons))
    if aborted is not None:
        raise aborted
    return 0 if receipt["state"] in ("measured", "superseded") else 1


def cancel(number: int, frame: object) -> None:
    """Turn SIGTERM into an exception so the running command's group is reaped on the way out."""
    raise SystemExit(128 + number)


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, cancel)
    sys.exit(main())
