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
processes of both. A fresh exact FRONTEND_NESTING_REQUEST_LINE adds the
deep-nesting leg (#2676): the base revision's benchmark_frontend_scaling.py
--suite nesting times the same two binaries on control statements nested to
growing depths.
A fresh exact COMPILER_PROBES_REQUEST_LINE adds the compiler-probes leg (#2761): the
trusted native compiler_probes.c harness builds the FAST register-allocation and
scheduler line-mark probes against both revisions in isolated clones and times
them as pairs.
On the pull route --harness-admission is hosted authorization's corpus
disposition (#3714); this host re-derives it before any build. A fresh exact
THROUGHPUT_HISTORICAL_REQUEST_LINE on a merge base that predates the harness
floor runs the corpus from the pinned THROUGHPUT_HISTORICAL_HARNESS_REVISION
harness in a detached worktree instead of the base's own.
A fresh exact WILD_LINKER_REQUEST_LINE adds the linker leg
(#2761): the head's ide objects are built in an isolated clone of the head,
then the trusted revision's wild_linker_acceptance.c --link-only times the
host's installed Wild against mold (or the host's default ld) on them.
A fresh exact LINK_TLS_REQUEST_LINE adds the TLS companion link leg (#1915):
the trusted revision's tools/link_tls_bench.py builds frozen TLS-heavy inputs
and times fresh `ide cc` links of both binaries, after requiring byte-identical
images that print the frozen output.
A fresh exact SELFHOST_INLINE_REQUEST_LINE adds the self-built inline-mode leg
(#58): after each role's build its ide builds that revision's stage-1
compiler with -fcanonical-inline, and after the ordinary comparison the
trusted selfhost_inline_compare.py times the two stage-1 compilers on the
frozen merge-base tree with topdown and sampling profiles.

The receipt records identities, the profile, toolchain versions, binary hashes
and timings, and is written even when a step fails. The candidate's build runs
as the runner account before measurement, so the receipt is evidence produced
under the direct path's owner-only trust boundary, not a sealed result.
With --request-attempt, an ordinary (not owned, MAIN-owned or analyzer) attempt,
legacy-rebuild or pull snapshot-v1 (whose closure records and owned phases join
the snapshot inventory), also exports the v1 raw inventory as required (#2918), writes
evidence-omissions.json and, once measured, seals the retained bundle last with
the trusted `./build.sh compiler_evidence seal`; the receipt's evidence_bundle
names that seal, and a refused seal fails the attempt.

Map: queue_head (pull-mode supersession), build (one ide), toolchain, export_tree (bounded evidence export),
read_exported_json (classification reads the export, #2929), lab_evidence_required and collect_evidence,
evidence_omissions_document and seal_evidence (#2918 sealed raw evidence),
throughput_harness_problem (#3714 base-harness admission), historical_request_selection and
prepare_historical_harness (#3714 named historical corpus), measure_throughput (corpus leg),
scaling_requested and measure_scaling (scaling leg),
ast_pilot_request_selection and measure_ast_pilot (#3397 opt-in flag pair),
constant_reuse_request_selection and measure_constant_reuse (#3011 opt-in flag pairs),
process_floor_request_selection and measure_process_floor (#1125 per-process floor),
real_source_request_selection and measure_real_source (#2761 real-source leg),
production_profile_request_selection and measure_production_profile (#2761 PGO/LTO leg),
compiler_probes_request_selection and measure_compiler_probes (#2761 probe leg),
frontend_selector_request_selection and measure_frontend_selector (#2761 probes),
frontend_nesting_request_selection and measure_frontend_nesting (#2676 deep nesting),
wild_linker_request_selection, host_linker and measure_wild_linker (#2761 linker leg),
link_tls_request_selection and measure_link_tls (#1915 TLS link leg),
selfhost_inline_request_selection, build_selfhost_inline_stage1 and measure_selfhost_inline (#58), main. Validity rules live in compiler_receipt.classify.
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
import struct
import subprocess
import sys
import tarfile
import time
import zipfile
from pathlib import Path

from compiler_github import ARTIFACT_LIMIT, RECONCILE_DEPTH
from compiler_receipt import (validate_closure, IDENTITY_KEYS, INLINE_ACCEPTANCE_PROFILE, INLINE_ACCEPTANCE_REQUEST_LINE,
                              AST_PILOT_ACCEPTANCE_PROFILE, AST_PILOT_ACCEPTANCE_REQUEST_LINE, validate_ast_pilot_acceptance,
                              AST_RETIREMENT_PROFILE, AST_RETIREMENT_CELLS, AST_RETIREMENT_CORPUS, AST_RETIREMENT_REQUEST_LINE,
                              validate_ast_retirement_acceptance,
                              CONSTANT_REUSE_ACCEPTANCE_PROFILE, CONSTANT_REUSE_ACCEPTANCE_REQUEST_LINE,
                              validate_constant_reuse_acceptance,
                              PROCESS_FLOOR_PROFILE, PROCESS_FLOOR_REQUEST_LINE, PROCESS_FLOOR_CELLS,
                              validate_process_floor_acceptance,
                              THROUGHPUT_REAL_SOURCE_PROFILE, THROUGHPUT_REAL_SOURCE_REQUEST_LINE,
                              throughput_real_source_digest, validate_throughput_real_source,
                              PRODUCTION_PROFILE_PROFILE, PRODUCTION_PROFILE_REQUEST_LINE, validate_production_profile,
                              COMPILER_PROBES_PROFILE, COMPILER_PROBES_REQUEST_LINE, COMPILER_PROBES_ROLES,
                              COMPILER_PROBES_HARNESS, COMPILER_PROBES_HARNESS_FLAGS,
                              compiler_probes_digest, validate_compiler_probes,
                              FRONTEND_SELECTOR_PROFILE, FRONTEND_SELECTOR_REQUEST_LINE, FRONTEND_SELECTOR_SCHEMA,
                              frontend_selector_digest, validate_frontend_selector,
                              FRONTEND_NESTING_PROFILE, FRONTEND_NESTING_REQUEST_LINE, frontend_scaling_digest,
                              validate_frontend_nesting,
                              WILD_LINKER_PROFILE, WILD_LINKER_REQUEST_LINE, WILD_LINKER_SCHEMA, WILD_LINKER_COMPARATORS,
                              wild_linker_bundle_files, wild_linker_digest, validate_wild_linker, WILD_IDENTITY,
                              WILD_LINKER_LINKERS,
                              LINK_TLS_PROFILE, LINK_TLS_REQUEST_LINE, LINK_TLS_HARNESS, link_tls_digest,
                              validate_link_tls,
                              SELFHOST_INLINE_PROFILE, SELFHOST_INLINE_REQUEST_LINE, SELFHOST_INLINE_HARNESS,
                              selfhost_inline_digest, validate_selfhost_inline,
                              INLINE_ACCEPTANCE_SCHEMA, ANALYZER_PROFILE_BY_LINE, ANALYZER_REQUEST_LINE,
                              ANALYZER_REQUEST_LINES,
                              ANALYZER_REQUEST_PATH, ANALYZER_REQUIRED_FILES, MODES, PROFILE, MAIN40_PROFILE, MAIN_PROFILES, named_main_profile, RECEIPT_SCHEMA, SCALING_PROFILE,
                              SCALING_REQUEST, SHA, THROUGHPUT_HARNESS_FLOOR, THROUGHPUT_PROFILE, HARNESS_ADMISSIONS,
                              THROUGHPUT_HISTORICAL_HARNESS_REVISION, THROUGHPUT_HISTORICAL_PROFILE,
                              THROUGHPUT_HISTORICAL_REQUEST_LINE, classify, classify_scaling,
                              classify_throughput, classify_throughput_exit, dumps, host_problem, inline_acceptance_requested,
                              render, scaling_digest, throughput_digest, validate_inline_acceptance,
                              analyzer_bootstrap_provenance, analyzer_profile_summary)
from compiler_receipt import observed_cpu_model as cpu_model
from compiler_receipt import (EVIDENCE_OMISSIONS, EVIDENCE_OMISSIONS_SCHEMA,
                              EVIDENCE_OMISSION_REASONS, EVIDENCE_OMISSION_EXAMPLES, EVIDENCE_SEAL,
                              evidence_lab_required, evidence_corpus_required, evidence_identity, evidence_inventory)
import throughput_real_source
from selfhost_inline_compare import validate_compare_documents as validate_selfhost_inline_documents

BUILD_TIMEOUT_SECONDS = 1800
LAB_TIMEOUT_SECONDS = 3000
THROUGHPUT_TIMEOUT_SECONDS = 1800
SCALING_TIMEOUT_SECONDS = 1200
INLINE_ACCEPTANCE_TIMEOUT_SECONDS = 3 * 60 * 60
AST_PILOT_ACCEPTANCE_TIMEOUT_SECONDS = 2 * 60 * 60
AST_RETIREMENT_ACCEPTANCE_TIMEOUT_SECONDS = 45 * 60
# Two -c cells of 20 ABBA pairs each, the size of the ast-pilot object cell.
CONSTANT_REUSE_ACCEPTANCE_TIMEOUT_SECONDS = 2 * 60 * 60
# Three cells of tiny compiles, each bounded by process_floor_acceptance.COMPARE_TIMEOUT_SECONDS.
PROCESS_FLOOR_TIMEOUT_SECONDS = 60 * 60
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
# Measured: the whole frontend-nesting leg (68 cells, one warmup and eleven
# repetitions of each binary) took 190 s on a hosted 4-vCPU 2.1 GHz Xeon and
# exported 196 KB; this bound keeps it inside the unchanged compare-pull budget.
FRONTEND_NESTING_TIMEOUT_SECONDS = 20 * 60
# Measured on a hosted 4-vCPU 2.3 GHz Xeon: the head's three input builds (two
# generations, Release unity, Debug and Release separate, and the harness compile)
# took 281 s together and the link-only harness 24 s (GNU ld as comparator, 3.4 MB
# of evidence). The input steps share one deadline; the two bounds together stay
# inside the unchanged 90-minute compare-pull job with its ordinary comparison.
WILD_LINKER_BUILD_TIMEOUT_SECONDS = 20 * 60
WILD_LINKER_HARNESS_TIMEOUT_SECONDS = 15 * 60
WILD_LINKER_PROBE_TIMEOUT_SECONDS = 30
# Measured on a hosted 4-vCPU 2.8 GHz Xeon against a pre-#2247 baseline, whose scan
# is slowest: building the inputs and the seven cells of 2 warmups + 20 pairs took
# 248 s. The bound stays inside the unchanged compare-pull job with its ordinary comparison.
LINK_TLS_TIMEOUT_SECONDS = 20 * 60
# One untimed stage-1 build per role, then 30 pairs and both profile steps of
# two self-built compilers: about 120 compiles of roughly 5 s each on the 9700X.
SELFHOST_INLINE_STAGE1_TIMEOUT_SECONDS = 15 * 60
SELFHOST_INLINE_TIMEOUT_SECONDS = 45 * 60
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
# The sealed raw-evidence contract (#2918, docs/compiler-evidence-v1.md): the
# native verifier bounds the retained bundle by EVIDENCE_SEALED_LIMITS, and
# the exports keep EVIDENCE_CONTROL_RESERVE for the manifest, seal and
# omissions document written after them.
EVIDENCE_SEALED_LIMITS = (ARTIFACT_LIMIT * 3 // 4, 4096)
EVIDENCE_CONTROL_RESERVE = (6 * 1024 * 1024, 3)
EVIDENCE_TOTAL_LIMIT = EVIDENCE_SEALED_LIMITS[0] - EVIDENCE_CONTROL_RESERVE[0]
EVIDENCE_FILE_COUNT = EVIDENCE_SEALED_LIMITS[1] - EVIDENCE_CONTROL_RESERVE[1]
# Sealed attempts' optional members, exported after every leg's required ones (export_deferred_optional).
EVIDENCE_DEFERRED_OPTIONAL: list[dict] = []
# Set by main for a sealed attempt: every export_tree defers its optional members, opt-in legs included.
EVIDENCE_DEFER_ALL = False
# (bundle path, reason code) of every omission in this attempt, for evidence-omissions.json.
EVIDENCE_OMISSION_LEDGER: list[tuple[str, str]] = []
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
WILD_LINKER_REQUIRED = tuple(wild_linker_bundle_files().values())
LINK_TLS_REQUIRED = ("summary.json",)
SELFHOST_INLINE_REQUIRED = ("stage1-baseline/stage1.json", "stage1-candidate/stage1.json", "compare/result.json",
                            "compare/lab/summary.json", "compare/lab/compare.json", "compare/lab/a/lab.json",
                            "compare/lab/b/lab.json")
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
# The linker leg keeps its samples, command logs, argv and manifests; linked
# images stay in the build trees, outside its evidence directory.
WILD_LINKER_IGNORE = (*EVIDENCE_IGNORE, "*.elf")
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
    """(bytes, entries) already under the evidence root, without following symlinks.

    Entries are files and directories below the root, as the sealed bound counts them (#2918).
    """
    total = count = 0
    for directory, folders, names in os.walk(evidence):
        count += len(folders)
        for name in names:
            try:
                total += os.lstat(os.path.join(directory, name)).st_size
                count += 1
            except OSError:
                pass
    return total, count


def copy_evidence_member(path: str, target: Path, limit: int, room: int) -> tuple[int, str, str]:
    """(bytes copied, reason, code): one regular file copied without following links, within its limits.

    room is the aggregate budget left; a file over its limit or one that grows past either bound while
    being read is not copied, and any partial target is removed.
    """
    copied, reason, code = 0, "", ""
    try:
        status = os.lstat(path)
        if not stat.S_ISREG(status.st_mode):
            reason, code = "not a regular file", "excluded-type"
        elif status.st_size > limit:
            reason, code = f"{status.st_size} bytes exceeds the {limit} byte file limit", "member-limit"
        elif status.st_size > room:
            reason, code = "evidence byte budget exhausted", "byte-budget"
        else:
            descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
            with os.fdopen(descriptor, "rb") as reader, target.open("xb") as writer:
                for block in iter(lambda: reader.read(1 << 20), b""):
                    copied += len(block)
                    if copied > limit or copied > room:
                        reason, code = "grew past its limit while being copied", "changed-during-copy"
                        break
                    writer.write(block)
            if reason:
                target.unlink()
    except OSError as error:
        reason, code = f"copy failed: {error}", "read-error"
        if target.is_file() and not target.is_symlink():
            target.unlink()
    return (0 if reason else copied), reason, code


def evidence_leaves(source: Path, ignore: tuple) -> tuple[list[tuple[str, str]], list[dict]]:
    """(relative path, source path) of every non-ignored leaf below source, and the symlinked directories skipped."""
    leaves: list[tuple[str, str]] = []
    skipped: list[dict] = []
    for directory, names, found in os.walk(source, followlinks=False):
        relative = Path(directory).relative_to(source)
        kept = []
        for name in sorted(names):
            if any(fnmatch.fnmatch(name, pattern) for pattern in ignore):
                continue
            if os.path.islink(os.path.join(directory, name)):
                skipped.append({"path": (relative / name).as_posix(), "reason": "symlink is not followed"})
                continue
            kept.append(name)
        names[:] = kept
        leaves.extend(((relative / name).as_posix(), os.path.join(directory, name)) for name in found
                      if not any(fnmatch.fnmatch(name, pattern) for pattern in ignore))
    return sorted(leaves, key=lambda leaf: leaf[0].encode()), skipped


def admit_evidence_leaves(leaves: list[tuple[str, str]], root: Path, created: set[str], usage: list[int],
                          present: set[str], omissions: list, codes: list[str]) -> None:
    """Copy leaves under root in order, charging each file and its not yet created ancestor directories.

    usage is [bytes, entries] of the whole evidence directory; a directory is created only for an admitted
    file, so empty or unadmitted optional subtrees never consume the entry budget (#2918).
    """
    for shown, path in leaves:
        parts = shown.split("/")
        parents = [ancestor for ancestor in ("/".join(parts[:depth]) for depth in range(1, len(parts)))
                   if ancestor not in created]
        limit = EVIDENCE_MEMBER_LIMIT if shown.endswith(".json") else EVIDENCE_FILE_LIMIT
        copied, reason, code = 0, "", ""
        if usage[1] + 1 + len(parents) > EVIDENCE_FILE_COUNT:
            reason, code = "evidence file-count budget exhausted", "file-budget"
        else:
            try:
                for parent in parents:
                    (root / parent).mkdir(exist_ok=True)
                    created.add(parent)
                    usage[1] += 1
                copied, reason, code = copy_evidence_member(path, root / shown, limit, EVIDENCE_TOTAL_LIMIT - usage[0])
            except OSError as error:
                reason, code = f"copy failed: {error}", "read-error"
        if reason:
            omissions.append({"path": shown, "reason": reason})
            codes.append(code)
        else:
            usage[0] += copied
            usage[1] += 1
            present.add(shown)


def export_tree(source: Path, destination: Path, evidence: Path, ignore: tuple, required: tuple,
                defer_optional: bool = False) -> tuple[list[str], list]:
    """Copy a measurement tree into the evidence under byte/entry limits enforced before and during copying.

    Only regular files are admitted: symlinks and other types are never followed or copied. A file over
    its limit (EVIDENCE_MEMBER_LIMIT for JSON, else EVIDENCE_FILE_LIMIT), one that would exceed the
    aggregate budget, one that grows while being read, or one that cannot be read is omitted and listed.
    Every required member is copied before any optional one, and a directory is created only as the
    ancestor of an admitted file. With defer_optional (a sealed attempt, #2918) no optional member is
    copied now: export_deferred_optional adds them after every leg's required members. EVIDENCE_DEFER_ALL
    applies the same to every export of a sealed attempt, so an opt-in leg exported before the ordinary lab
    and corpus never spends their budget on optional data; each leg still validates only required members,
    which are exported at once. The tree is staged
    beside its destination and moved into place afterwards. Returns (problems, omissions); an omitted or
    absent required member is a problem, so the export is incomplete rather than quietly successful. Each
    omission also reaches EVIDENCE_OMISSION_LEDGER with its fixed reason code. The source is never modified.
    """
    problems: list[str] = []
    omissions: list = []
    codes: list[str] = []
    staging = destination.with_name(destination.name + ".staging")
    used, files = evidence_usage(evidence)
    if destination.is_dir():  # a re-export replaces it
        replaced = evidence_usage(destination)
        used, files = used - replaced[0], files - replaced[1]
    else:  # the destination directory itself is a retained entry
        files += 1
    present: set[str] = set()
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    try:
        leaves, skipped = evidence_leaves(source, ignore)
        omissions.extend(skipped)
        codes.extend("excluded-type" for _ in skipped)
        usage = [used, files]
        created: set[str] = set()
        admit_evidence_leaves([leaf for leaf in leaves if leaf[0] in required], staging, created, usage, present,
                              omissions, codes)
        defer_optional = defer_optional or EVIDENCE_DEFER_ALL
        if not defer_optional:
            admit_evidence_leaves([leaf for leaf in leaves if leaf[0] not in required], staging, created, usage, present,
                                  omissions, codes)
        for member in required:
            if member not in present:
                why = next((item["reason"] for item in omissions if item["path"] == member), "absent from the source")
                problems.append(f"required evidence {destination.name}/{member} not exported: {why}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists():
            shutil.rmtree(destination)
        os.replace(staging, destination)
        if defer_optional:
            EVIDENCE_DEFERRED_OPTIONAL.append({"source": source, "destination": destination, "ignore": ignore,
                                               "required": required, "created": created, "evidence": evidence})
    except OSError as error:
        problems.append(f"evidence export of {destination.name} failed: {error}")
        shutil.rmtree(staging, ignore_errors=True)
    record_evidence_omissions(destination, evidence, omissions, codes)
    return problems, omissions


def record_evidence_omissions(destination: Path, evidence: Path, omissions: list, codes: list[str]) -> None:
    try:
        prefix = destination.relative_to(evidence).as_posix() + "/"
    except ValueError:
        prefix = destination.name + "/"
    EVIDENCE_OMISSION_LEDGER.extend((prefix + item["path"], code) for item, code in zip(omissions, codes))


def promote_deferred_member(path: Path) -> None:
    """Export a deferred optional member now, because a leg is about to read it (#2918).

    A sealed attempt defers every member a leg did not list as required, but some legs validate members
    they never listed (for example a part exported with required=()). The member is admitted on demand
    within the export budget, exactly as the final pass would; bulk optional data stays deferred.
    """
    for record in EVIDENCE_DEFERRED_OPTIONAL:
        try:
            relative = path.relative_to(record["destination"]).as_posix()
        except ValueError:
            continue
        # Only a leaf the same nofollow walk would export: never through a symlinked or ignored directory.
        leaves = dict(evidence_leaves(record["source"], record["ignore"])[0]) if not os.path.lexists(path) else {}
        if relative in leaves:
            omissions: list = []
            codes: list[str] = []
            admit_evidence_leaves([(relative, leaves[relative])], record["destination"], record["created"],
                                  list(evidence_usage(record["evidence"])), set(), omissions, codes)
            record_evidence_omissions(record["destination"], record["evidence"], omissions, codes)
            record["omitted"] = record.get("omitted", []) + omissions
        break


def export_deferred_optional(evidence: Path, receipt_omissions: dict) -> None:
    """Add sealed attempts' optional members once every leg's required members are retained (#2918).

    Trees are visited in export order and each tree's optional leaves in bytewise path order, within what
    is left of the export budget; omissions join the ledger and the receipt section of their tree.
    """
    used, files = evidence_usage(evidence)
    usage = [used, files]
    for record in EVIDENCE_DEFERRED_OPTIONAL:
        omissions: list = []
        codes: list[str] = []
        leaves, _ = evidence_leaves(record["source"], record["ignore"])
        promoted = {item["path"] for item in record.get("omitted", [])}
        admit_evidence_leaves([leaf for leaf in leaves if leaf[0] not in record["required"] and leaf[0] not in promoted and
                               not os.path.lexists(record["destination"] / leaf[0])],
                              record["destination"], record["created"], usage, set(), omissions, codes)
        record_evidence_omissions(record["destination"], evidence, omissions, codes)
        omissions = record.get("omitted", []) + omissions  # on-demand omissions are already in the ledger
        if omissions:
            name = record["destination"].relative_to(evidence).as_posix()
            earlier = receipt_omissions.get(name, {"count": 0, "shown": []})
            receipt_omissions[name] = {"count": earlier["count"] + len(omissions),
                                       "shown": (earlier["shown"] + omissions)[:OMISSIONS_SHOWN]}
    EVIDENCE_DEFERRED_OPTIONAL.clear()


def read_exported_json(path: Path) -> tuple[object, str]:
    """(document, "") from the exported copy, or (None, why) for anything but a bounded regular JSON file.

    Classification must see the bytes the publisher will see, not the measurement tree they were copied
    from (#2929), so this reads the evidence member without following a symlink and within
    EVIDENCE_MEMBER_LIMIT. A member a sealed attempt deferred is exported first (promote_deferred_member).
    Every failure is a returned reason, never an exception.
    """
    document: object = None
    problem = ""
    promote_deferred_member(path)
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


def lab_evidence_required(lab: Path) -> tuple[str, ...]:
    """The v1 required lab members (#2918) from the source lab's own plan and availability records.

    An unreadable or malformed record leaves the fixed members only; the native seal then refuses the bundle.
    """
    compare, _ = read_exported_json(lab / "compare.json")
    plan = compare.get("plan") if isinstance(compare, dict) else None
    pairs = plan.get("pairs") if isinstance(plan, dict) else None
    variants = [read_exported_json(lab / variant / "lab.json")[0] for variant in "ab"]

    def flag(document: object, section: str, key: str) -> bool:
        value = document.get(section) if isinstance(document, dict) else None
        if section == "capabilities" and isinstance(value, dict):
            value = value.get("perf_stat")
        return isinstance(value, dict) and value.get(key) is True

    valid = type(pairs) is int and 0 < pairs <= 1000
    return evidence_lab_required(pairs if valid else 0, tuple(flag(item, "capabilities", "usable") for item in variants),
                                 tuple(flag(item, "collection", "metrics_out") for item in variants))


def collect_evidence(lab: Path, evidence: Path, omissions: dict, sealed: bool = False) -> list[str]:
    problems: list[str] = []
    if lab.is_dir():
        required = lab_evidence_required(lab) if sealed else LAB_REQUIRED
        problems, found = export_tree(lab, evidence / "lab", evidence, EVIDENCE_IGNORE, required, defer_optional=sealed)
        note_omissions(omissions, "lab", found)
    return problems


def measure_throughput(candidate: Path, bins: Path, work: Path, evidence: Path, base: str, head: str,
                       binaries: dict, omissions: dict, harness: Path | None = None,
                       cwd: Path | None = None, sealed: bool = False) -> tuple[list[str], dict]:
    """Run and validate the exported corpus before admitting a later snapshot child.

    cwd is the pinned historical harness worktree (#3714); its own build.sh runs the corpus there.
    """
    output = work / "throughput"
    argv = [*([str(harness)] if harness else ["./build.sh", "bench_throughput"]), "run",
            "--baseline", str(bins / "ide-base"), "--candidate", str(bins / "ide-cand"),
            "--output", str(output), "--baseline-id", base, "--candidate-id", head,
            *THROUGHPUT_PROFILE["arguments"]]
    context = OWNED_PHASE_CONTEXT
    try:
        if context is None:
            status = run(argv, cwd or candidate, evidence / "throughput.log", THROUGHPUT_TIMEOUT_SECONDS)
        else:
            status = context.execute(argv, candidate, evidence / "throughput.log", THROUGHPUT_TIMEOUT_SECONDS,
                                     exit_policy="corpus-report-only-v1").returncode
        reasons = []
        if output.is_dir():
            required = evidence_corpus_required(THROUGHPUT_PROFILE) if sealed else THROUGHPUT_REQUIRED
            problems, found = export_tree(output, evidence / "throughput", evidence, EVIDENCE_IGNORE, required,
                                          defer_optional=sealed)
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


# The trusted native sealer (#2918); compiler_test substitutes a stand-in.
EVIDENCE_SEAL_TIMEOUT_SECONDS = 600
SCRATCH_MARKER = ".buster-compiler-compare-scratch"
SCRATCH_PROTOCOL = "buster-compiler-compare-scratch-v1\n"
TRUSTED_ROOT = Path(__file__).resolve().parents[2]
EVIDENCE_SEAL_COMMAND = (str(TRUSTED_ROOT / "build.sh"), "compiler_evidence")


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


def throughput_harness_problem(candidate: Path, base: str, historical: bool = False) -> str:
    """Why this base cannot run its selected corpus recipe; '' when admitted (#3714).

    Without the historical recipe the base revision's own harness must carry the floor; with it,
    the base must predate the floor, since only such a base needs the pinned harness. Only the
    commit graph is read, so the blob-less checkout fetches nothing. A floor git cannot place,
    and a probe that times out or cannot start, fail closed, as a reason the caller retains in
    the failed receipt.
    """
    try:
        status = captured_run(["git", "-C", str(candidate), "merge-base", "--is-ancestor", THROUGHPUT_HARNESS_FLOOR,
                               base], capture_output=True, timeout=GIT_TIMEOUT_SECONDS, check=False).returncode
    except (subprocess.TimeoutExpired, OSError) as error:
        status = f"{error.__class__.__name__}: {error}"
    problem = ""
    if isinstance(status, str):
        problem = f"throughput harness admission probe failed ({status}); not measured"
    elif status == 1 and not historical:
        problem = (f"unsupported historical throughput harness: base {base} predates {THROUGHPUT_HARNESS_FLOOR}, "
                   f"so its --mode all is not the {THROUGHPUT_PROFILE['name']} FAST/QUALITY population; "
                   "not measured")
    elif status == 0 and historical:
        problem = (f"{THROUGHPUT_HISTORICAL_PROFILE['name']} requested, but base {base} already carries "
                   f"{THROUGHPUT_HARNESS_FLOOR}; not measured")
    elif status not in (0, 1):
        problem = (f"throughput harness admission could not place {THROUGHPUT_HARNESS_FLOOR} before base {base} "
                   f"(git exited {status}); not measured")
    return problem


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


def ast_retirement_request_selection(candidate: Path, head: str) -> tuple[bool, str]:
    """Select #4058 only on one exact new occurrence against every head parent."""
    deltas = request_selector_parent_deltas(candidate, head, AST_RETIREMENT_REQUEST_LINE)
    if deltas is None:
        return False, "could not prove the AST retirement selector against every head parent"
    if not any(delta > 0 for delta in deltas):
        return False, ""
    if any(delta != 1 for delta in deltas):
        return False, "the AST retirement selector must be added exactly once relative to every head parent"
    return True, ""


def constant_reuse_request_selection(candidate: Path, head: str) -> tuple[bool, str]:
    """Select #3011's fixed profile only when its exact line is added once against every head parent."""
    deltas = request_selector_parent_deltas(candidate, head, CONSTANT_REUSE_ACCEPTANCE_REQUEST_LINE)
    if deltas is None:
        return False, "could not prove the constant-reuse acceptance selector count against every head parent"
    if not any(delta > 0 for delta in deltas):
        return False, ""
    if any(delta != 1 for delta in deltas):
        return False, "the constant-reuse acceptance selector must be added exactly once relative to every head parent"
    return True, ""


def process_floor_request_selection(candidate: Path, head: str) -> tuple[bool, str]:
    """Select #1125's fixed profile only when its exact line is added once against every head parent."""
    deltas = request_selector_parent_deltas(candidate, head, PROCESS_FLOOR_REQUEST_LINE)
    if deltas is None:
        return False, "could not prove the process-floor selector count against every head parent"
    if not any(delta > 0 for delta in deltas):
        return False, ""
    if any(delta != 1 for delta in deltas):
        return False, "the process-floor selector must be added exactly once relative to every head parent"
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


def historical_request_selection(candidate: Path, head: str) -> tuple[bool, str]:
    """Select #3714's named historical corpus only when its exact line is added once against every head parent."""
    deltas = request_selector_parent_deltas(candidate, head, THROUGHPUT_HISTORICAL_REQUEST_LINE)
    if deltas is None:
        return False, "could not prove the historical corpus selector count against every head parent"
    if not any(delta > 0 for delta in deltas):
        return False, ""
    if any(delta != 1 for delta in deltas):
        return False, "the historical corpus selector must be added exactly once relative to every head parent"
    return True, ""


def prepare_historical_harness(candidate: Path, work: Path, evidence: Path, receipt: dict) -> tuple[Path | None, str]:
    """A detached worktree at THROUGHPUT_HISTORICAL_HARNESS_REVISION for the corpus (#3714); (root, problem).

    The worktree lives under --work, so the candidate checkout and its build tree are untouched.
    It keeps the candidate's own remote, so checking out the pinned revision lazily fetches its
    missing blobs exactly as the base checkout does in the blob-less clone; a shared clone would
    have only the local candidate as remote. Its own `./build.sh bench_throughput` builds and runs
    that revision's harness.
    """
    clone = work / "historical-harness"
    log = evidence / "historical-harness.log"
    revision = THROUGHPUT_HISTORICAL_HARNESS_REVISION
    record = receipt.setdefault("throughput_historical", {"harness_revision": revision})
    argv = ["git", "-c", "gc.auto=0", "-c", "maintenance.auto=false", "-c", "core.hooksPath=/dev/null",
            "-C", str(candidate), "worktree", "add", "--quiet", "--detach", str(clone), revision]
    problem = ""
    started = time.monotonic()
    status = run(argv, candidate, log, GIT_TIMEOUT_SECONDS)
    if status != 0:
        problem = f"historical corpus harness worktree exited {status}"
    if not problem:
        try:
            observed = git(clone, "rev-parse", "HEAD")
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError) as error:
            observed, problem = None, f"historical corpus harness checkout cannot be inspected: {error}"
        record["observed_revision"] = observed
        if not problem and observed != revision:
            problem = f"historical corpus harness checkout {observed} is not the pinned {revision}"
    receipt["timings"]["historical_harness_seconds"] = round(time.monotonic() - started, 3)
    record["status"] = "failed" if problem else "prepared"
    return (None if problem else clone), problem


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


def frontend_nesting_request_selection(candidate: Path, head: str) -> tuple[bool, str]:
    """Select #2676's deep-nesting leg only when its exact line is added once against every head parent."""
    deltas = request_selector_parent_deltas(candidate, head, FRONTEND_NESTING_REQUEST_LINE)
    if deltas is None:
        return False, "could not prove the frontend-nesting selector count against every head parent"
    if not any(delta > 0 for delta in deltas):
        return False, ""
    if any(delta != 1 for delta in deltas):
        return False, "the frontend-nesting selector must be added exactly once relative to every head parent"
    return True, ""


def wild_linker_request_selection(candidate: Path, head: str) -> tuple[bool, str]:
    """Select #2761's Wild linker leg only when its exact line is added once against every head parent."""
    deltas = request_selector_parent_deltas(candidate, head, WILD_LINKER_REQUEST_LINE)
    if deltas is None:
        return False, "could not prove the wild-linker selector count against every head parent"
    if not any(delta > 0 for delta in deltas):
        return False, ""
    if any(delta != 1 for delta in deltas):
        return False, "the wild-linker selector must be added exactly once relative to every head parent"
    return True, ""


def link_tls_request_selection(candidate: Path, head: str) -> tuple[bool, str]:
    """Select #1915's TLS companion link leg only when its exact line is added once against every head parent."""
    deltas = request_selector_parent_deltas(candidate, head, LINK_TLS_REQUEST_LINE)
    if deltas is None:
        return False, "could not prove the link-tls selector count against every head parent"
    if not any(delta > 0 for delta in deltas):
        return False, ""
    if any(delta != 1 for delta in deltas):
        return False, "the link-tls selector must be added exactly once relative to every head parent"
    return True, ""


def selfhost_inline_request_selection(candidate: Path, head: str) -> tuple[bool, str]:
    """Select #58's self-built inline-mode leg only when its exact line is added once against every head parent."""
    deltas = request_selector_parent_deltas(candidate, head, SELFHOST_INLINE_REQUEST_LINE)
    if deltas is None:
        return False, "could not prove the self-host inline selector count against every head parent"
    if not any(delta > 0 for delta in deltas):
        return False, ""
    if any(delta != 1 for delta in deltas):
        return False, "the self-host inline selector must be added exactly once relative to every head parent"
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
    reuse_problem = getattr(arguments, "constant_reuse_request_problem", "")
    if reuse_problem:
        reasons.append(reuse_problem)
    elif getattr(arguments, "constant_reuse_requested", False):
        reasons.append(f"{selected_profile['name']} cannot be combined with the constant-reuse acceptance selector")
    floor_problem = getattr(arguments, "process_floor_request_problem", "")
    if floor_problem:
        reasons.append(floor_problem)
    elif getattr(arguments, "process_floor_requested", False):
        reasons.append(f"{selected_profile['name']} cannot be combined with the process-floor selector")
    retirement_problem = getattr(arguments, "ast_retirement_request_problem", "")
    if retirement_problem:
        reasons.append(retirement_problem)
    elif getattr(arguments, "ast_retirement_requested", False):
        reasons.append(f"{selected_profile['name']} cannot be combined with the AST retirement selector")
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
    parser.add_argument("--harness-admission", choices=HARNESS_ADMISSIONS[:2],
                        help="pull only: the hosted pre-dispatch corpus disposition this host must re-derive (#3714)")
    parser.add_argument("--request-attempt",
                        help="the request run's attempt; seals the ordinary raw evidence for the publisher (#2918)")
    for name in IDENTITY_KEYS[1:]:
        parser.add_argument("--" + name.replace("_", "-"), required=True)
    arguments = parser.parse_args(argv)
    if arguments.utility_owned_phases and (arguments.mode != "main" or arguments.closure_driver is None):
        parser.error("--utility-owned-phases requires main mode and the trusted --closure-driver")
    if arguments.main_owned_phases and (arguments.utility_owned_phases or arguments.mode != "main" or arguments.closure_driver is None):
        parser.error("--main-owned-phases requires main mode and trusted driver, exclusively from Utility")
    if arguments.main_profile is not None and not arguments.main_owned_phases:
        parser.error("--main-profile requires --main-owned-phases")
    if arguments.harness_admission is not None and arguments.mode != "pull":
        parser.error("--harness-admission is the pull route's hosted disposition")
    if arguments.request_attempt is not None and (not arguments.request_attempt.isdigit() or
                                                  not arguments.request_attempt.isascii() or
                                                  arguments.request_attempt.startswith("0")):
        parser.error("--request-attempt must be a positive decimal attempt")
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


def evidence_omissions_document() -> dict:
    """evidence-omissions.json from this attempt's omission ledger: fixed reason codes and at most 50 examples."""
    counts = {reason: 0 for reason in EVIDENCE_OMISSION_REASONS}
    for _, code in EVIDENCE_OMISSION_LEDGER:
        counts[code] += 1
    return {"schema": EVIDENCE_OMISSIONS_SCHEMA, "total": len(EVIDENCE_OMISSION_LEDGER), "by_reason": counts,
            "examples": [{"path": path, "reason": code} for path, code in EVIDENCE_OMISSION_LEDGER[:EVIDENCE_OMISSION_EXAMPLES]]}


def seal_evidence(receipt: dict, evidence: Path, work: Path, request_attempt: str) -> str:
    """Write the omissions document, then seal the retained bundle with the trusted native driver (#2918).

    Runs after every export and before the final receipt, which binds the seal through
    receipt["evidence_bundle"]. Returns '' or why the bundle is not sealed; the native seal refuses an
    incomplete bundle, so an unsealed measured attempt fails rather than publishing unverifiable raw bytes.
    """
    identity = evidence_identity(receipt, request_attempt)
    problem = "" if identity else "sealed evidence identity is incomplete"
    output = ""
    if not problem:
        try:
            (evidence / EVIDENCE_OMISSIONS).write_text(dumps(evidence_omissions_document()) + "\n", encoding="utf-8")
            (work / "evidence-identity.tsv").write_text(identity, encoding="utf-8")
            completed = captured_run([*EVIDENCE_SEAL_COMMAND, "seal", str(evidence), str(work / "evidence-identity.tsv")],
                                     cwd=TRUSTED_ROOT, capture_output=True, text=True, timeout=EVIDENCE_SEAL_TIMEOUT_SECONDS,
                                     check=False)
            lines = (completed.stdout or "").strip().splitlines()
            output = lines[-1] if lines else ""
            if completed.returncode != 0 or output != "COMPILER_EVIDENCE_SEALED":
                problem = f"raw evidence not sealed (exit {completed.returncode}): {output or (completed.stderr or '').strip()[-400:]}"
        except (OSError, subprocess.SubprocessError, UnicodeError) as error:
            problem = f"raw evidence not sealed: {error.__class__.__name__}: {error}"
    seal: dict = {}
    if not problem:
        try:
            with os.fdopen(os.open(evidence / EVIDENCE_SEAL, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)), "rb") as reader:
                text = reader.read(4097).decode("ascii")
            seal = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)
        except (OSError, UnicodeError, ValueError) as error:
            problem = f"evidence seal unreadable after sealing: {error}"
    if not problem and not all(seal.get(key, "").isdigit() for key in ("manifest_bytes", "members", "payload_bytes")):
        problem = "evidence seal is malformed after sealing"
    inventory = evidence_inventory(receipt)
    bundle = {"inventory": inventory, "state": "failed", "reason": problem}
    if not problem:
        bundle = {"inventory": inventory, "state": "sealed", "manifest_sha256": seal.get("manifest_sha256", ""),
                  "manifest_bytes": int(seal["manifest_bytes"]), "members": int(seal["members"]),
                  "payload_bytes": int(seal["payload_bytes"]), "omissions": len(EVIDENCE_OMISSION_LEDGER)}
    receipt["evidence_bundle"] = bundle
    return problem


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

AST_RETIREMENT_REQUIRED = ("acceptance.json", "source.json", "inputs/unity.c",
                           *(f"inputs/{name}.c" for name in AST_RETIREMENT_CORPUS),
                           *(f"{cell}/{name}" for cell in AST_RETIREMENT_CELLS
                             for name in ("summary.json", "compare.json", "pairs.json", "reference.json", "a/lab.json", "b/lab.json",
                                          "a/env/env.json", "b/env/env.json", "a/reference.diagnostics", "b/reference.diagnostics",
                                          "a/reference.000.bin", "a/reference.001.bin", "b/reference.000.bin", "b/reference.001.bin")),
                           *(f"{cell}/pairs/{pair:04d}-{arm}.csv" for cell in AST_RETIREMENT_CELLS
                             for pair in range(1, AST_RETIREMENT_PROFILE["pairs"] + 1) for arm in ("a", "b")),
                           *(f"probe/{cell}/{name}" for cell in AST_RETIREMENT_CELLS for name in ("probe.json", "a.log", "b.log")))


def measure_ast_retirement(arguments: argparse.Namespace, candidate: Path, bins: Path, work: Path, evidence: Path,
                           receipt: dict, harness: Path | None) -> None:
    """Run the fixed default/default leg after both binaries and the base corpus exist."""
    mark(receipt, evidence, "ast-retirement-acceptance")
    output = work / "ast-retirement-acceptance"
    helper = Path(__file__).with_name("ast_retirement_acceptance.py").resolve()
    measured = time.monotonic()
    native = harness if harness is not None else candidate / "build/throughput-tools/throughput"
    status = run([sys.executable, "-B", str(helper), "--lab", str(arguments.lab.resolve()), "--harness", str(native),
                  "--baseline-ide", str(bins / "ide-base"), "--candidate-ide", str(bins / "ide-cand"),
                  "--repo-root", str(candidate), "--base-revision", arguments.base, "--head-revision", arguments.head,
                  "--cpu", str(AST_RETIREMENT_PROFILE["cpu"]), "--output", str(output)],
                 candidate, evidence / "ast-retirement-acceptance.log", AST_RETIREMENT_ACCEPTANCE_TIMEOUT_SECONDS)
    receipt["timings"]["ast_retirement_acceptance_seconds"] = round(time.monotonic() - measured, 3)
    record = receipt["ast_retirement_acceptance"]
    record["exit"] = status
    errors, omissions = export_tree(output, evidence / "ast_retirement_acceptance", evidence, EVIDENCE_IGNORE, AST_RETIREMENT_REQUIRED)
    record["evidence_omissions"] = omissions
    summary, problem = read_exported_json(evidence / "ast_retirement_acceptance" / "acceptance.json")
    if problem:
        errors.append(f"issue #4058 retirement receipt unreadable: {problem}")
    else:
        record["summary"] = summary
        errors.extend(validate_ast_retirement_acceptance(summary, arguments.base, arguments.head, receipt["binaries"]))
    if status != 0:
        errors.append(f"issue #4058 retirement measurement exited {status}; raw unavailable/failed evidence is retained")
    record["errors"] = errors
    record["status"] = "complete" if not errors else "failed"


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


CONSTANT_REUSE_REQUIRED = ("acceptance.json",
                           *(f"{cell}/{name}" for cell in CONSTANT_REUSE_ACCEPTANCE_PROFILE["cells"]
                             for name in ("summary.json", "compare.json", "a/lab.json", "b/lab.json")))


def measure_constant_reuse(arguments: argparse.Namespace, candidate: Path, binary: Path, work: Path, evidence: Path,
                           receipt: dict) -> None:
    """Run #3011's fixed constant-reuse profile on the frozen candidate build and record it in the receipt."""
    mark(receipt, evidence, "constant-reuse-acceptance")
    output = work / "constant-reuse-acceptance"
    helper = Path(__file__).with_name("constant_reuse_acceptance.py").resolve()
    measured = time.monotonic()
    status = run([sys.executable, "-B", str(helper),
                  "--lab", str(arguments.lab.resolve()), "--candidate-ide", str(binary),
                  "--repo-root", str(candidate), "--head-revision", arguments.head,
                  "--cpu", str(CONSTANT_REUSE_ACCEPTANCE_PROFILE["cpu"]), "--output", str(output)],
                 candidate, evidence / "constant-reuse-acceptance.log", CONSTANT_REUSE_ACCEPTANCE_TIMEOUT_SECONDS)
    receipt["timings"]["constant_reuse_acceptance_seconds"] = round(time.monotonic() - measured, 3)
    record = receipt["constant_reuse_acceptance"]
    record["exit"] = status
    errors, omissions = export_tree(output, evidence / "constant_reuse_acceptance", evidence, EVIDENCE_IGNORE,
                                    CONSTANT_REUSE_REQUIRED)
    record["evidence_omissions"] = omissions
    summary, problem = read_exported_json(evidence / "constant_reuse_acceptance" / "acceptance.json")
    if problem:
        errors.append(f"issue #3011 constant-reuse acceptance receipt unreadable: {problem}")
    else:
        record["summary"] = summary
        errors.extend(validate_constant_reuse_acceptance(summary, arguments.head, receipt["binaries"]["candidate"]["sha256"]))
    if status != 0:
        errors.append(f"issue #3011 constant-reuse acceptance exited {status}")
    record["errors"] = errors
    record["status"] = "complete" if not errors else "failed"


PROCESS_FLOOR_REQUIRED = ("acceptance.json",
                          *(f"{cell}/{name}" for cell in PROCESS_FLOOR_CELLS
                            for name in ("summary.json", "compare.json", "a/lab.json", "b/lab.json")))


def measure_process_floor(arguments: argparse.Namespace, candidate: Path, binary: Path, work: Path, evidence: Path,
                          receipt: dict) -> None:
    """Run #1125's fixed per-process floor profile on the frozen candidate build and record it in the receipt."""
    mark(receipt, evidence, "process-floor")
    output = work / "process-floor"
    helper = Path(__file__).with_name("process_floor_acceptance.py").resolve()
    measured = time.monotonic()
    status = run([sys.executable, "-B", str(helper),
                  "--lab", str(arguments.lab.resolve()), "--candidate-ide", str(binary),
                  "--repo-root", str(candidate), "--head-revision", arguments.head,
                  "--cpu", str(PROCESS_FLOOR_PROFILE["cpu"]), "--output", str(output)],
                 candidate, evidence / "process-floor.log", PROCESS_FLOOR_TIMEOUT_SECONDS)
    receipt["timings"]["process_floor_seconds"] = round(time.monotonic() - measured, 3)
    record = receipt["process_floor"]
    record["exit"] = status
    errors, omissions = export_tree(output, evidence / "process_floor", evidence, EVIDENCE_IGNORE, PROCESS_FLOOR_REQUIRED)
    record["evidence_omissions"] = omissions
    summary, problem = read_exported_json(evidence / "process_floor" / "acceptance.json")
    if problem:
        errors.append(f"issue #1125 process-floor receipt unreadable: {problem}")
    else:
        record["summary"] = summary
        errors.extend(validate_process_floor_acceptance(summary, arguments.head, receipt["binaries"]["candidate"]["sha256"]))
    if status != 0:
        errors.append(f"issue #1125 process-floor profile exited {status}")
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


def measure_frontend_nesting(candidate: Path, bins: Path, work: Path, evidence: Path, receipt: dict) -> None:
    """Run #2676's deep-nesting families from the base checkout on both binaries and record them."""
    record = receipt["frontend_nesting"]
    profile = FRONTEND_NESTING_PROFILE
    exported = evidence / "frontend_nesting"
    output = work / "frontend-nesting"
    errors: list[str] = []
    measured = time.monotonic()
    status = run([sys.executable, "-B", "tools/benchmark_frontend_scaling.py", "--baseline", str(bins / "ide-base"),
                  "--candidate", str(bins / "ide-cand"), "--out", str(output), *profile["frontend_scaling"]["arguments"]],
                 candidate, evidence / "frontend-nesting.log", FRONTEND_NESTING_TIMEOUT_SECONDS)
    receipt["timings"]["frontend_nesting_seconds"] = round(time.monotonic() - measured, 3)
    documents = {"provenance.json": None, "results.json": None}
    if output.is_dir():
        problems, record["evidence_omissions"] = export_tree(output, exported, evidence, FRONTEND_SCALING_IGNORE,
                                                             FRONTEND_SCALING_REQUIRED)
        errors.extend(problems)
        for name in documents:
            documents[name], problem = read_exported_json(exported / name)
            if problem:
                errors.append(f"exported evidence frontend_nesting/{name} unreadable: {problem}")
    else:
        errors.append("frontend-nesting produced no output")
    errors.extend(validate_frontend_nesting(status, documents["provenance.json"], documents["results.json"],
                                            receipt["binaries"]))
    record["cells"] = frontend_scaling_digest(documents["results.json"])
    record["exit"] = status
    record["errors"] = errors
    record["status"] = "complete" if not errors else "failed"


def host_linker(command: str, log: Path) -> tuple[dict | None, str, bool]:
    """(identity, problem, absent) of the host's installed `command`, found with `command -v`, never downloaded."""
    identity, problem, found = None, "", ""
    try:
        found = captured_run(["/bin/sh", "-c", f"command -v {command}"], capture_output=True, text=True,
                             timeout=WILD_LINKER_PROBE_TIMEOUT_SECONDS, check=False).stdout.strip()
    except (OSError, subprocess.TimeoutExpired) as error:
        problem = f"could not look up the host's {command}: {error.__class__.__name__}"
    absent = not problem and not (found.startswith("/") and os.path.isfile(found) and os.access(found, os.X_OK))
    if absent:
        problem = f"the host has no installed {command} (command -v {command} found {found!r})"
    if not problem and ("'" in found or '"' in found):
        problem = f"the host's {command} path {found!r} cannot be quoted for the harness"
    if not problem:
        try:
            result = captured_run([found, "--version"], capture_output=True, text=True,
                                  timeout=WILD_LINKER_PROBE_TIMEOUT_SECONDS, check=False)
            lines = (result.stdout + result.stderr).strip().splitlines()
            version = lines[0].strip() if result.returncode == 0 and lines else ""
        except (OSError, subprocess.TimeoutExpired):
            version = ""
        if not version:
            problem = f"the host's {command} at {found} did not report a version"
        else:
            real = os.path.realpath(found)
            identity = {"command": command, "path": found, "realpath": real, "version": version,
                        "sha256": sha256(Path(real))}
    with log.open("a", encoding="utf-8") as stream:
        stream.write(f"$ command -v {command}\n{found}\n{identity or problem}\n")
    return identity, problem, absent


def elf_comment(data: bytes) -> list[str] | None:
    """The NUL-separated strings of a little-endian ELF64 image's .comment section, or None if it has none."""
    strings = None
    try:
        if data[:4] == b"\x7fELF" and data[4] == 2 and data[5] == 1:
            shoff, = struct.unpack_from("<Q", data, 0x28)
            entsize, count, names = struct.unpack_from("<HHH", data, 0x3A)
            sections = [struct.unpack_from("<IIQQQQ", data, shoff + index * entsize) for index in range(count)]
            table = sections[names]
            for name, _, _, _, offset, size in sections:
                end = data.index(b"\0", table[4] + name)
                if data[table[4] + name:end] == b".comment":
                    strings = [item.decode("utf-8", "replace") for item in data[offset:offset + size].split(b"\0") if item]
    except (struct.error, IndexError, ValueError):
        strings = None
    return strings


def record_link_comment(tree: Path, output: Path, cell: str, linker: str) -> list[str]:
    """Write the .comment strings of the harness's saved output for one cell and linker, after checking that the
    saved image is the one whose hash the harness recorded; return the problems."""
    problems: list[str] = []
    image = tree / f"{cell}-{linker}.elf"
    recorded = output / f"{cell}-{linker}.output.sha256"
    try:
        data = image.read_bytes()
        expected = recorded.read_text(encoding="utf-8").strip()
    except OSError as error:
        data, expected = b"", None
        problems.append(f"wild-linker {cell} {linker} output unreadable: {error.__class__.__name__}")
    if expected is not None and hashlib.sha256(data).hexdigest() != expected:
        problems.append(f"wild-linker {cell} {linker} saved output is not the image the harness hashed")
    strings = elf_comment(data) if not problems else None
    if strings is None and not problems:
        problems.append(f"wild-linker {cell} {linker} output has no readable .comment section")
    (output / f"{cell}-{linker}.comment.txt").write_text("".join(item + "\n" for item in strings or []),
                                                        encoding="utf-8")
    return problems


def measure_wild_linker(candidate: Path, work: Path, evidence: Path, head: str, receipt: dict) -> None:
    """Build the head's ide link inputs in an isolated clone, then time the host's Wild against its comparator."""
    record = receipt["wild_linker"]
    profile = WILD_LINKER_PROFILE
    log = evidence / "wild-linker-build.log"
    exported = evidence / "wild_linker"
    errors: list[str] = []
    status = None
    linkers = {}
    wild, problem, _ = host_linker("wild", log)
    if wild is not None and not WILD_IDENTITY.fullmatch(wild["version"]):
        problem = f"the host's wild at {wild['path']} reports {wild['version']!r}, not Wild's own identity"
    if problem:
        errors.append(problem)
    else:
        linkers["wild"] = {**wild, "kind": "wild", "policy": profile["wild"]["policy"],
                           "threads1": profile["wild"]["threads1"]}
        # The audit's comparator, mold; the host's default linker only when mold is not installed at all.
        for kind, frozen in WILD_LINKER_COMPARATORS.items():
            if "comparator" not in linkers and not errors:
                comparator, problem, absent = host_linker(frozen["command"], log)
                if comparator is not None and not comparator["version"].startswith(frozen["version"]):
                    problem = f"the host's {frozen['command']} reports {comparator['version']!r}, not a {kind} version"
                if not problem:
                    linkers["comparator"] = {**comparator, "kind": kind, "policy": frozen["policy"],
                                             "threads1": frozen["threads1"], "substituted": kind != "mold"}
                elif not (absent and kind == "mold"):
                    errors.append(f"comparator {kind}: {problem}")
    source = work / "wild-linker-source"
    output = work / "wild-linker"
    tools = Path(__file__).resolve().parents[1]
    harness_sources = {name: sha256(tools / name) for name in ("wild_linker_acceptance.c", "wild_linker_bench.c")
                       if (tools / name).is_file()}
    harness = work / "wild-linker-harness"
    if len(linkers) == 2:
        measured = time.monotonic()
        # A shared clone keeps the candidate checkout and its build tree untouched for the closure checks.
        steps = [(["git", "-c", "core.hooksPath=/dev/null", "clone", "--quiet", "--shared", "--no-checkout",
                   str(candidate), str(source)], candidate, GIT_TIMEOUT_SECONDS),
                 (["git", "-c", "gc.auto=0", "-c", "maintenance.auto=false", "-c", "core.hooksPath=/dev/null",
                   "-C", str(source), "checkout", "--quiet", "--detach", head], candidate, GIT_TIMEOUT_SECONDS)]
        for tree, arguments in profile["trees"].items():
            steps.append((["./build.sh", "generate", "--build-directory", tree, *arguments], source,
                          WILD_LINKER_BUILD_TIMEOUT_SECONDS))
            steps.extend((["./build.sh", "build", "--build-directory", tree, "--config", config, "-t", "ide"], source,
                          WILD_LINKER_BUILD_TIMEOUT_SECONDS) for built, config in profile["builds"] if built == tree)
        steps.append((["clang", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                       str(tools / "wild_linker_acceptance.c"), "-o", str(harness)], candidate, GIT_TIMEOUT_SECONDS))
        build_status = 0
        # One deadline for every input step, so the leg's worst case is the two bounds together.
        deadline = measured + WILD_LINKER_BUILD_TIMEOUT_SECONDS
        for argv, cwd, timeout in steps:
            if build_status == 0:
                build_status = run(argv, cwd, log, max(1, min(timeout, int(deadline - time.monotonic()))))
        receipt["timings"]["wild_linker_build_seconds"] = round(time.monotonic() - measured, 3)
        record["build_exit"] = build_status
        if build_status != 0:
            errors.append(f"the wild-linker input build or harness compile exited {build_status} (see {log.name})")
        else:
            output.mkdir(parents=True, exist_ok=False)
            measured = time.monotonic()
            status = run(["taskset", "-c", ",".join(str(cpu) for cpu in profile["cpus"]), str(harness), "--link-only",
                          str(output), str(source), linkers["wild"]["path"], linkers["comparator"]["path"],
                          linkers["comparator"]["policy"], linkers["comparator"]["threads1"]],
                         source, evidence / "wild-linker.log", WILD_LINKER_HARNESS_TIMEOUT_SECONDS)
            receipt["timings"]["wild_linker_harness_seconds"] = round(time.monotonic() - measured, 3)
            for linker in linkers.values():
                try:
                    linker["sha256_after"] = sha256(Path(linker["realpath"]))
                except OSError:
                    # Removed or replaced during the leg: recorded, and refused by validate_wild_linker.
                    linker["sha256_after"] = None
            inputs = {}
            for cell in profile["cells"]:
                manifest = output / f"{cell}-inputs.sha256"
                inputs[cell] = sha256(manifest) if manifest.is_file() else None
                for linker in WILD_LINKER_LINKERS if status == 0 else ():
                    errors.extend(record_link_comment(source / profile["cell_trees"][cell][0], output, cell, linker))
            identity = {"schema": WILD_LINKER_SCHEMA, "profile": profile["name"], "head": head, "cpu_model": cpu_model(),
                        "cpus": profile["cpus"], "linkers": linkers, "harness": harness_sources, "inputs": inputs,
                        "build_root": str(source), "exit": status}
            (output / "wild-linker.json").write_text(dumps(identity) + "\n", encoding="utf-8")
            record["identity"] = identity
            problems, record["evidence_omissions"] = export_tree(output, exported, evidence, WILD_LINKER_IGNORE,
                                                                 WILD_LINKER_REQUIRED)
            errors.extend(problems)
            files = {}
            for key, name in wild_linker_bundle_files().items():
                try:
                    files[key] = (exported / name).read_text(encoding="utf-8")
                except (OSError, UnicodeDecodeError) as error:
                    errors.append(f"exported evidence wild_linker/{name} unreadable: {error.__class__.__name__}")
            errors.extend(validate_wild_linker(status, files, head, identity))
            record["cells"] = wild_linker_digest(files.get("raw"), files.get("summary"))
    record["linkers"] = linkers
    record["exit"] = status
    record["errors"] = errors
    record["status"] = "complete" if not errors and status == 0 else "failed"


def measure_link_tls(bins: Path, work: Path, evidence: Path, receipt: dict) -> None:
    """Run the trusted revision's TLS companion link harness on both binaries and record it (#1915)."""
    record = receipt["link_tls"]
    output = work / "link-tls"
    exported = evidence / "link_tls"
    errors: list[str] = []
    harness = Path(__file__).resolve().parents[1] / LINK_TLS_HARNESS
    record["harness_sha256"] = sha256(harness) if harness.is_file() else None
    measured = time.monotonic()
    # The harness comes from this trusted checkout's tools/, never from the candidate.
    status = run([sys.executable, "-I", str(harness), str(bins / "ide-base"), str(bins / "ide-cand"),
                  str(work / "link-tls-work"), str(output)], work, evidence / "link-tls.log", LINK_TLS_TIMEOUT_SECONDS)
    receipt["timings"]["link_tls_seconds"] = round(time.monotonic() - measured, 3)
    summary = None
    if output.is_dir():
        problems, record["evidence_omissions"] = export_tree(output, exported, evidence, EVIDENCE_IGNORE,
                                                             LINK_TLS_REQUIRED)
        errors.extend(problems)
        summary, problem = read_exported_json(exported / "summary.json")
        if problem:
            errors.append(f"exported evidence link_tls/summary.json unreadable: {problem}")
    errors.extend(validate_link_tls(status, summary, receipt["binaries"]))
    record["cells"] = link_tls_digest(summary)
    record["exit"] = status
    record["errors"] = errors
    record["status"] = "complete" if not errors else "failed"


def selfhost_inline_helper() -> Path:
    """The trusted revision's #58 helper, never the candidate's."""
    return Path(__file__).resolve().with_name(SELFHOST_INLINE_HARNESS)


def build_selfhost_inline_stage1(role: str, binary: Path, candidate: Path, revision: str, work: Path, evidence: Path,
                                 receipt: dict) -> None:
    """Build one role's stage-1 compiler with -fcanonical-inline while its revision is checked out (#58)."""
    record = receipt["selfhost_inline"]
    measured = time.monotonic()
    status = run([sys.executable, "-B", str(selfhost_inline_helper()), "stage1", "--ide", str(binary),
                  "--repo-root", str(candidate), "--revision", revision,
                  "--output", str(work / "selfhost-inline" / f"stage1-{role}")],
                 candidate, evidence / f"selfhost-inline-stage1-{role}.log", SELFHOST_INLINE_STAGE1_TIMEOUT_SECONDS)
    receipt["timings"][f"selfhost_inline_stage1_{role}_seconds"] = round(time.monotonic() - measured, 3)
    record.setdefault("stage1_exit", {})[role] = status
    if status != 0:
        record.setdefault("errors", []).append(f"self-host inline {role} stage-1 build exited {status}")


def measure_selfhost_inline(arguments: argparse.Namespace, candidate: Path, work: Path, evidence: Path,
                            receipt: dict) -> None:
    """Time both stage-1 compilers on the frozen merge-base tree and record the validated result (#58)."""
    record = receipt["selfhost_inline"]
    root = work / "selfhost-inline"
    exported = evidence / "selfhost_inline"
    errors = list(record.get("errors", []))
    helper = selfhost_inline_helper()
    record["harness_sha256"] = sha256(helper) if helper.is_file() else None
    status = None
    if not errors and all(record.get("stage1_exit", {}).get(role) == 0 for role in ("baseline", "candidate")):
        measured = time.monotonic()
        status = run([sys.executable, "-B", str(helper), "compare", "--lab", str(arguments.lab.resolve()),
                      "--baseline-stage1", str(root / "stage1-baseline/stage1.json"),
                      "--candidate-stage1", str(root / "stage1-candidate/stage1.json"),
                      "--repo-root", str(candidate), "--base-revision", arguments.base, "--output", str(root / "compare")],
                     candidate, evidence / "selfhost-inline.log", SELFHOST_INLINE_TIMEOUT_SECONDS)
        receipt["timings"]["selfhost_inline_seconds"] = round(time.monotonic() - measured, 3)
        if status != 0:
            errors.append(f"self-host inline comparison exited {status}")
    elif not errors:
        errors.append("self-host inline stage-1 compilers were not both built")
    documents = {}
    if root.is_dir():
        record["evidence_omissions"] = []
        # Each part has its own destination: an export replaces its destination directory. Each part
        # names its mandatory documents, so they are exported now even when optional files are deferred.
        for part in ("stage1-baseline", "stage1-candidate", "compare"):
            if (root / part).is_dir():
                required = tuple(name[len(part) + 1:] for name in SELFHOST_INLINE_REQUIRED
                                 if name.startswith(part + "/"))
                problems, omissions = export_tree(root / part, exported / part, evidence, EVIDENCE_IGNORE, required)
                errors.extend(problems)
                record["evidence_omissions"].extend(omissions)
        for name in SELFHOST_INLINE_REQUIRED:
            documents[name], problem = read_exported_json(exported / name)
            if problem:
                errors.append(f"exported evidence selfhost_inline/{name} unreadable: {problem}")
    if status == 0 and len(documents) == len(SELFHOST_INLINE_REQUIRED) and all(documents.values()):
        stage1 = {role: documents[f"stage1-{role}/stage1.json"] for role in ("baseline", "candidate")}
        errors.extend(validate_selfhost_inline(stage1, documents["compare/result.json"], receipt["binaries"]))
        expected = {role: (stage1[role].get("stage1") or {}).get("sha256") for role in ("baseline", "candidate")}
        errors.extend(validate_selfhost_inline_documents(
            documents["compare/lab/summary.json"], documents["compare/lab/compare.json"],
            {"a": documents["compare/lab/a/lab.json"], "b": documents["compare/lab/b/lab.json"]}, expected))
        record["digest"] = selfhost_inline_digest(documents["compare/lab/summary.json"])
    record["exit"] = status
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
    reuse_requested = arguments.mode == "pull" and getattr(arguments, "constant_reuse_requested", False)
    problem = getattr(arguments, "constant_reuse_request_problem", "") if arguments.mode == "pull" else ""
    if problem and problem not in reasons:
        reasons.append(problem)
    if reuse_requested:
        receipt["constant_reuse_acceptance"] = {"requested": True, "request_line": CONSTANT_REUSE_ACCEPTANCE_REQUEST_LINE,
                                                "profile": CONSTANT_REUSE_ACCEPTANCE_PROFILE, "status": "pending"}
        # One long opt-in leg per compare-pull job keeps the unchanged compare-pull budget provable. The inline
        # profile is not one: its exact line is in main's request history, so it rides every pull compare.
        if ast_requested:
            reasons.append("constant-reuse-acceptance-v1 cannot be combined with the ast-pilot acceptance profile")
    real_requested = arguments.mode == "pull" and getattr(arguments, "real_source_requested", False)
    problem = getattr(arguments, "real_source_request_problem", "") if arguments.mode == "pull" else ""
    if problem and problem not in reasons:
        reasons.append(problem)
    if real_requested:
        receipt["throughput_real_source"] = {"requested": True, "request_line": THROUGHPUT_REAL_SOURCE_REQUEST_LINE,
                                             "profile": THROUGHPUT_REAL_SOURCE_PROFILE, "status": "pending"}
        if reuse_requested:
            reasons.append("constant-reuse-acceptance-v1 cannot be combined with real-source throughput")
    production_requested = arguments.mode == "pull" and getattr(arguments, "production_profile_requested", False)
    problem = getattr(arguments, "production_profile_request_problem", "") if arguments.mode == "pull" else ""
    if problem and problem not in reasons:
        reasons.append(problem)
    if production_requested:
        receipt["production_profile"] = {"requested": True, "request_line": PRODUCTION_PROFILE_REQUEST_LINE,
                                         "profile": PRODUCTION_PROFILE_PROFILE, "status": "pending"}
        # Its build leg and the other long opt-in legs together would not fit the compare-pull job.
        if real_requested or ast_requested or reuse_requested:
            reasons.append("production-profile-v1 cannot be combined with real-source throughput, ast-pilot or "
                           "constant-reuse acceptance")
    probes_requested = arguments.mode == "pull" and getattr(arguments, "compiler_probes_requested", False)
    problem = getattr(arguments, "compiler_probes_request_problem", "") if arguments.mode == "pull" else ""
    if problem and problem not in reasons:
        reasons.append(problem)
    if probes_requested:
        receipt["compiler_probes"] = {"requested": True, "request_line": COMPILER_PROBES_REQUEST_LINE,
                                      "profile": COMPILER_PROBES_PROFILE, "status": "pending"}
        # One long opt-in leg per compare-pull job keeps the unchanged 90-minute budget provable.
        if real_requested or ast_requested or production_requested or reuse_requested:
            reasons.append("compiler-probes-v1 cannot be combined with real-source throughput, ast-pilot or "
                           "constant-reuse acceptance or the PGO/LTO production profile")
    selector_requested = arguments.mode == "pull" and getattr(arguments, "frontend_selector_requested", False)
    problem = getattr(arguments, "frontend_selector_request_problem", "") if arguments.mode == "pull" else ""
    if problem and problem not in reasons:
        reasons.append(problem)
    if selector_requested:
        receipt["frontend_selector"] = {"requested": True, "request_line": FRONTEND_SELECTOR_REQUEST_LINE,
                                        "profile": FRONTEND_SELECTOR_PROFILE, "status": "pending"}
        # One long opt-in leg per compare-pull job keeps the leg's measured cost inside its unchanged budget.
        if real_requested or ast_requested or production_requested or probes_requested or reuse_requested:
            reasons.append("frontend-selector-v1 cannot be combined with real-source throughput, ast-pilot or "
                           "constant-reuse acceptance, the PGO/LTO production profile or compiler-probes-v1")
    linker_requested = arguments.mode == "pull" and getattr(arguments, "wild_linker_requested", False)
    problem = getattr(arguments, "wild_linker_request_problem", "") if arguments.mode == "pull" else ""
    if problem and problem not in reasons:
        reasons.append(problem)
    if linker_requested:
        receipt["wild_linker"] = {"requested": True, "request_line": WILD_LINKER_REQUEST_LINE,
                                  "profile": WILD_LINKER_PROFILE, "status": "pending"}
        # The long opt-in legs and its input builds together would not fit the compare-pull job, and one
        # opt-in leg per attempt keeps a report from being shared between two profiles.
        if real_requested or ast_requested or production_requested or probes_requested or selector_requested or \
                reuse_requested:
            reasons.append("wild-linker-v1 cannot be combined with real-source throughput, ast-pilot or constant-reuse "
                           "acceptance, the PGO/LTO production profile, compiler-probes-v1 or the frontend-selector probes")
    link_tls_requested = arguments.mode == "pull" and getattr(arguments, "link_tls_requested", False)
    problem = getattr(arguments, "link_tls_request_problem", "") if arguments.mode == "pull" else ""
    if problem and problem not in reasons:
        reasons.append(problem)
    if link_tls_requested:
        receipt["link_tls"] = {"requested": True, "request_line": LINK_TLS_REQUEST_LINE, "profile": LINK_TLS_PROFILE,
                               "status": "pending"}
        # One opt-in leg per attempt keeps a report from being shared between two profiles.
        if real_requested or ast_requested or production_requested or probes_requested or selector_requested or \
                linker_requested or reuse_requested:
            reasons.append("link-tls-v1 cannot be combined with real-source throughput, ast-pilot or constant-reuse "
                           "acceptance, the PGO/LTO production profile, compiler-probes-v1, the frontend-selector probes "
                           "or wild-linker-v1")
    selfhost_requested = arguments.mode == "pull" and getattr(arguments, "selfhost_inline_requested", False)
    problem = getattr(arguments, "selfhost_inline_request_problem", "") if arguments.mode == "pull" else ""
    if problem and problem not in reasons:
        reasons.append(problem)
    if selfhost_requested:
        receipt["selfhost_inline"] = {"requested": True, "request_line": SELFHOST_INLINE_REQUEST_LINE,
                                      "profile": SELFHOST_INLINE_PROFILE, "status": "pending"}
        # One opt-in leg per attempt keeps a report from being shared between two profiles; the
        # scaling leg also runs before this one and would share its budget. The #48 inline leg is
        # not an opt-in here: inline_acceptance_requested selects it whenever its line is anywhere in
        # the request file, which it has been on main since #48, so it runs beside this leg.
        if real_requested or ast_requested or production_requested or probes_requested or selector_requested or \
                linker_requested or link_tls_requested or reuse_requested or \
                scaling_requested(candidate, arguments.base, arguments.head):
            reasons.append("self-host-inline-compare-v1 cannot be combined with another opt-in leg, the scaling "
                           "leg included")
    nesting_requested = arguments.mode == "pull" and getattr(arguments, "frontend_nesting_requested", False)
    problem = getattr(arguments, "frontend_nesting_request_problem", "") if arguments.mode == "pull" else ""
    if problem and problem not in reasons:
        reasons.append(problem)
    if nesting_requested:
        receipt["frontend_nesting"] = {"requested": True, "request_line": FRONTEND_NESTING_REQUEST_LINE,
                                       "profile": FRONTEND_NESTING_PROFILE, "status": "pending"}
        # One opt-in leg per attempt keeps a report from being shared between two profiles.
        if real_requested or ast_requested or production_requested or probes_requested or selector_requested or \
                linker_requested or link_tls_requested or selfhost_requested or reuse_requested:
            reasons.append("frontend-nesting-v1 cannot be combined with another opt-in leg")
    floor_requested = arguments.mode == "pull" and getattr(arguments, "process_floor_requested", False)
    problem = getattr(arguments, "process_floor_request_problem", "") if arguments.mode == "pull" else ""
    if problem and problem not in reasons:
        reasons.append(problem)
    if floor_requested:
        receipt["process_floor"] = {"requested": True, "request_line": PROCESS_FLOOR_REQUEST_LINE,
                                    "profile": PROCESS_FLOOR_PROFILE, "status": "pending"}
        # One opt-in leg per attempt keeps a report from being shared between two profiles. The #48 inline leg
        # is not an opt-in here: its line is in main's request history, so it rides every pull compare.
        if real_requested or ast_requested or production_requested or probes_requested or selector_requested or \
                linker_requested or link_tls_requested or selfhost_requested or reuse_requested or nesting_requested:
            reasons.append("process-floor-v1 cannot be combined with another opt-in leg")
    retirement_requested = arguments.mode == "pull" and getattr(arguments, "ast_retirement_requested", False)
    problem = getattr(arguments, "ast_retirement_request_problem", "") if arguments.mode == "pull" else ""
    if problem and problem not in reasons:
        reasons.append(problem)
    if retirement_requested:
        receipt["ast_retirement_acceptance"] = {"requested": True, "request_line": AST_RETIREMENT_REQUEST_LINE,
                                                "profile": AST_RETIREMENT_PROFILE, "status": "pending"}
        if real_requested or ast_requested or production_requested or probes_requested or selector_requested or \
                linker_requested or link_tls_requested or selfhost_requested or reuse_requested or nesting_requested or floor_requested or \
                scaling_requested(candidate, arguments.base, arguments.head) or getattr(arguments, "analyzer_profile_requested", False):
            reasons.append("ast-retirement-acceptance-v1 cannot be combined with another opt-in leg or scaling")
        if not getattr(arguments, "evidence_seal", False):
            reasons.append("ast-retirement-acceptance-v1 requires the ordinary verified evidence-seal route")
    # #3714: the named historical recipe swaps only the corpus harness; every other opt-in leg would
    # still run the base's own pre-floor harness, so none may join it. Its pinned worktree is prepared
    # before any build.
    historical_root = None
    if receipt.get("throughput_profile") == THROUGHPUT_HISTORICAL_PROFILE:
        others = (real_requested or ast_requested or production_requested or probes_requested or selector_requested
                  or inline_requested or linker_requested or link_tls_requested or reuse_requested
                  or selfhost_requested or nesting_requested or floor_requested or retirement_requested
                  or (arguments.mode == "pull" and scaling_requested(candidate, arguments.base,
                                                                                           arguments.head)))
        if snapshot_closure:
            reasons.append(f"{THROUGHPUT_HISTORICAL_PROFILE['name']} runs only with the legacy-rebuild closure")
        elif others:
            reasons.append(f"{THROUGHPUT_HISTORICAL_PROFILE['name']} cannot be combined with another opt-in leg")
        if not reasons:
            mark(receipt, evidence, "historical-harness")
            historical_root, problem = prepare_historical_harness(candidate, work, evidence, receipt)
            if problem:
                reasons.append(problem)
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
                if role == "candidate" and reuse_requested:
                    measure_constant_reuse(arguments, candidate, binary, work, evidence, receipt)
                if role == "candidate" and floor_requested:
                    measure_process_floor(arguments, candidate, binary, work, evidence, receipt)
                if selfhost_requested:
                    mark(receipt, evidence, f"selfhost-inline-stage1-{role}")
                    build_selfhost_inline_stage1(role, binary, candidate, commit, work, evidence, receipt)

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
        reasons.extend(collect_evidence(lab, evidence, receipt.setdefault("evidence_omissions", {}),
                                        getattr(arguments, "evidence_seal", False)))
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
                                                           receipt.setdefault("evidence_omissions", {}), harness,
                                                           cwd=historical_root,
                                                           sealed=getattr(arguments, "evidence_seal", False))
        receipt["timings"]["throughput_seconds"] = round(time.monotonic() - measured, 3)
        reasons.extend(corpus)
        if retirement_requested and not reasons:
            measure_ast_retirement(arguments, candidate, bins, work, evidence, receipt, harness)
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
        if linker_requested:
            mark(receipt, evidence, "wild-linker")
            measure_wild_linker(candidate, work, evidence, arguments.head, receipt)
        if link_tls_requested:
            mark(receipt, evidence, "link-tls")
            measure_link_tls(bins, work, evidence, receipt)
        if selfhost_requested:
            mark(receipt, evidence, "selfhost-inline")
            measure_selfhost_inline(arguments, candidate, work, evidence, receipt)
        if nesting_requested:
            mark(receipt, evidence, "frontend-nesting")
            measure_frontend_nesting(candidate, bins, work, evidence, receipt)
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
    if reuse_requested:
        record = receipt["constant_reuse_acceptance"]
        reasons.extend(record.get("errors", []))
        if record["status"] == "pending":
            record["status"] = "failed"
            reasons.append("issue #3011 constant-reuse acceptance was requested but did not reach its candidate-build phase")
        if record["status"] != "complete":
            receipt["state"] = "failed"
    if floor_requested:
        record = receipt["process_floor"]
        reasons.extend(record.get("errors", []))
        if record["status"] == "pending":
            record["status"] = "failed"
            reasons.append("issue #1125 process-floor profile was requested but did not reach its candidate-build phase")
        if record["status"] != "complete":
            receipt["state"] = "failed"
    if retirement_requested:
        record = receipt["ast_retirement_acceptance"]
        reasons.extend(record.get("errors", []))
        if record["status"] == "pending":
            record["status"] = "failed"
            reasons.append("issue #4058 retirement profile was requested but did not reach its two-binary measurement phase")
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
    if linker_requested:
        record = receipt["wild_linker"]
        reasons.extend(record.get("errors", []))
        if record["status"] == "pending":
            record["status"] = "failed"
            reasons.append("the wild-linker leg was requested but did not reach its measurement phase")
        if record["status"] != "complete":
            receipt["state"] = "failed"
    if link_tls_requested:
        record = receipt["link_tls"]
        reasons.extend(record.get("errors", []))
        if record["status"] == "pending":
            record["status"] = "failed"
            reasons.append("the link-tls leg was requested but did not reach its measurement phase")
        if record["status"] != "complete":
            receipt["state"] = "failed"
    if selfhost_requested:
        record = receipt["selfhost_inline"]
        reasons.extend(record.get("errors", []))
        if record["status"] == "pending":
            record["status"] = "failed"
            reasons.append("the self-host inline leg was requested but did not reach its measurement phase")
        if record["status"] != "complete":
            receipt["state"] = "failed"
    if nesting_requested:
        record = receipt["frontend_nesting"]
        reasons.extend(record.get("errors", []))
        if record["status"] == "pending":
            record["status"] = "failed"
            reasons.append("the frontend-nesting leg was requested but did not reach its measurement phase")
        if record["status"] != "complete":
            receipt["state"] = "failed"



def main(argv: list[str] | None = None) -> int:
    global OWNED_PHASE_CONTEXT, EVIDENCE_DEFER_ALL
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
    arguments.ast_retirement_requested, arguments.ast_retirement_request_problem = (
        ast_retirement_request_selection(candidate, arguments.head) if arguments.mode == "pull" else (False, ""))
    arguments.constant_reuse_requested, arguments.constant_reuse_request_problem = (
        constant_reuse_request_selection(candidate, arguments.head) if arguments.mode == "pull" else (False, ""))
    arguments.process_floor_requested, arguments.process_floor_request_problem = (
        process_floor_request_selection(candidate, arguments.head) if arguments.mode == "pull" else (False, ""))
    arguments.real_source_requested, arguments.real_source_request_problem = (
        real_source_request_selection(candidate, arguments.head) if arguments.mode == "pull" else (False, ""))
    arguments.historical_requested, arguments.historical_request_problem = (
        historical_request_selection(candidate, arguments.head) if arguments.mode == "pull" else (False, ""))
    arguments.production_profile_requested, arguments.production_profile_request_problem = (
        production_profile_request_selection(candidate, arguments.head) if arguments.mode == "pull" else (False, ""))
    arguments.compiler_probes_requested, arguments.compiler_probes_request_problem = (
        compiler_probes_request_selection(candidate, arguments.head) if arguments.mode == "pull" else (False, ""))
    arguments.frontend_selector_requested, arguments.frontend_selector_request_problem = (
        frontend_selector_request_selection(candidate, arguments.head) if arguments.mode == "pull" else (False, ""))
    arguments.frontend_nesting_requested, arguments.frontend_nesting_request_problem = (
        frontend_nesting_request_selection(candidate, arguments.head) if arguments.mode == "pull" else (False, ""))
    arguments.wild_linker_requested, arguments.wild_linker_request_problem = (
        wild_linker_request_selection(candidate, arguments.head) if arguments.mode == "pull" else (False, ""))
    arguments.link_tls_requested, arguments.link_tls_request_problem = (
        link_tls_request_selection(candidate, arguments.head) if arguments.mode == "pull" else (False, ""))
    arguments.selfhost_inline_requested, arguments.selfhost_inline_request_problem = (
        selfhost_inline_request_selection(candidate, arguments.head) if arguments.mode == "pull" else (False, ""))
    identity = {key: getattr(arguments, key) for key in IDENTITY_KEYS}
    utility = getattr(arguments, "utility_owned_phases", False)
    main_owned = getattr(arguments, "main_owned_phases", False)
    selected_profile = named_main_profile(arguments.main_profile) if main_owned else PROFILE
    early_owned = utility or main_owned or (arguments.mode == "main" and arguments.closure_policy == "snapshot-v1")
    # Contract v1 (#2918) seals the ordinary lab and corpus, on the legacy-rebuild and the pull
    # snapshot-v1 route alike (#3211); the owned, MAIN-owned and analyzer routes keep their own contracts.
    arguments.evidence_seal = getattr(arguments, "request_attempt", None) is not None and not early_owned and \
        not analyzer_requested and arguments.closure_policy in ("legacy-rebuild", "snapshot-v1")
    EVIDENCE_OMISSION_LEDGER.clear()
    EVIDENCE_DEFERRED_OPTIONAL.clear()
    EVIDENCE_DEFER_ALL = arguments.evidence_seal
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
    if arguments.ast_retirement_request_problem:
        reasons.append(arguments.ast_retirement_request_problem)
    if analyzer_requested and arguments.ast_retirement_requested:
        reasons.append("an analyzer profile cannot be combined with AST retirement acceptance")
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
    # Admit the corpus harness before any build or lab (#3714): the base revision's own, or the
    # pinned one of the named historical recipe. The MAIN-owned routes replay a sealed preflight
    # and measure only main commits past the floor. On the pull route this host's disposition must
    # equal the hosted one that dispatched it.
    if arguments.historical_request_problem:
        reasons.append(arguments.historical_request_problem)
    if not reasons and not early_owned and not analyzer_requested:
        problem = throughput_harness_problem(candidate, arguments.base, arguments.historical_requested)
        if problem:
            reasons.append(problem)
        derived = "historical" if arguments.historical_requested else "admitted"
        if not problem and arguments.harness_admission is not None and arguments.harness_admission != derived:
            reasons.append(f"hosted harness admission {arguments.harness_admission!r} is not this host's {derived!r}; "
                           "not measured")
        if arguments.historical_requested:
            receipt["throughput_profile"] = THROUGHPUT_HISTORICAL_PROFILE
    elif arguments.historical_requested and analyzer_requested:
        reasons.append("an analyzer profile cannot be combined with the historical corpus recipe")

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
    if EVIDENCE_DEFERRED_OPTIONAL and aborted is None:
        export_deferred_optional(evidence, receipt.setdefault("evidence_omissions", {}))
    EVIDENCE_DEFER_ALL = False
    if not receipt.get("evidence_omissions"):
        receipt.pop("evidence_omissions", None)
    if arguments.evidence_seal and receipt["state"] == "measured" and aborted is None:
        problem = seal_evidence(receipt, evidence, work, arguments.request_attempt)
        if problem:
            reasons.append(problem)
            receipt["state"] = "failed"
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
