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
alone, from the same base checkout, and its bundle joins the evidence.

The receipt records identities, the profile, toolchain versions, binary hashes
and timings, and is written even when a step fails. The candidate's build runs
as the runner account before measurement, so the receipt is evidence produced
under the direct path's owner-only trust boundary, not a sealed result.

Map: queue_head (pull-mode supersession), build (one ide), toolchain, export_tree (bounded evidence export), collect_evidence,
measure_throughput (corpus leg), scaling_requested and measure_scaling (scaling leg),
main. Validity rules live in compiler_receipt.classify.
"""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import os
import platform
import shutil
import signal
import socket
import stat
import subprocess
import sys
import time
from pathlib import Path

from compiler_github import ARTIFACT_LIMIT, RECONCILE_DEPTH
from compiler_receipt import (IDENTITY_KEYS, INLINE_ACCEPTANCE_PROFILE, INLINE_ACCEPTANCE_REQUEST_LINE,
                              INLINE_ACCEPTANCE_SCHEMA, MODES, PROFILE, RECEIPT_SCHEMA, SCALING_PROFILE,
                              SCALING_REQUEST, SHA, THROUGHPUT_PROFILE, classify, classify_scaling,
                              classify_throughput, dumps, host_problem, inline_acceptance_requested,
                              render, scaling_digest, throughput_digest, validate_inline_acceptance)
from compiler_receipt import observed_cpu_model as cpu_model

BUILD_TIMEOUT_SECONDS = 1800
LAB_TIMEOUT_SECONDS = 3000
THROUGHPUT_TIMEOUT_SECONDS = 1800
SCALING_TIMEOUT_SECONDS = 1200
INLINE_ACCEPTANCE_TIMEOUT_SECONDS = 3 * 60 * 60
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
SCALING_REQUIRED = ("scaling.json", "scaling-metadata.json")
# Raw evidence without compiled outputs, per-run binary copies or perf data.
EVIDENCE_IGNORE = ("*.exe", "instances", "*.data", "*.data.old", "*.o", "*.obj")
# A scaling bundle keeps its reports, raw samples and per-sample logs; its
# generated inputs are reproducible from the recorded seed and hashes.
SCALING_IGNORE = (*EVIDENCE_IGNORE, "inputs", "*.metrics")
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


def run(argv: list[str], cwd: Path, log: Path, timeout: int) -> int:
    """Run argv in its own process group with output appended to log; returns the exit status.

    On timeout (124), on any exception such as cancellation by SIGTERM, and after normal completion the
    whole group is killed and awaited before returning, so no descendant outlives the command or touches
    scratch/evidence during the next phase. The original status is logged first; if the group cannot be
    proven empty the status is CLEANUP_STATUS (125) so no later measurement begins.
    """
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
    return subprocess.run(["git", "-C", str(candidate), *arguments], check=True, capture_output=True,
                          text=True, timeout=GIT_TIMEOUT_SECONDS).stdout.strip()


def queue_head(repository: str, ref: str) -> str:
    """The ref's current head by an anonymous read; '' when gone, None when unknown."""
    environment = dict(os.environ, GIT_TERMINAL_PROMPT="0")
    head = None
    try:
        result = subprocess.run(["git", "ls-remote", f"https://github.com/{repository}.git", ref],
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
    status = run(["git", "-C", str(candidate), "checkout", "--quiet", "--detach", commit], candidate, log,
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
        try:
            result = subprocess.run([tool, flag], capture_output=True, text=True, timeout=30, check=False)
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
                       binaries: dict, omissions: dict) -> tuple[list[str], dict]:
    """Run the corpus on both binaries from the checked-out base; (reasons, receipt section)."""
    output = work / "throughput"
    status = run(["./build.sh", "bench_throughput", "run", "--baseline", str(bins / "ide-base"),
                  "--candidate", str(bins / "ide-cand"), "--output", str(output), "--baseline-id", base,
                  "--candidate-id", head, *THROUGHPUT_PROFILE["arguments"]],
                 candidate, evidence / "throughput.log", THROUGHPUT_TIMEOUT_SECONDS)
    documents = []
    reasons = [] if status == 0 else [f"bench_throughput run exited {status} (see throughput.log)"]
    if output.is_dir():
        problems, found = export_tree(output, evidence / "throughput", evidence, EVIDENCE_IGNORE, THROUGHPUT_REQUIRED)
        reasons.extend(problems)
        note_omissions(omissions, "throughput", found)
    for name in ("summary.json", "metadata.json"):
        try:
            documents.append(json.loads((output / name).read_text(encoding="utf-8")))
        except (OSError, ValueError):
            documents.append(None)
    reasons.extend(classify_throughput(documents[0], documents[1], binaries))
    return reasons, dict(throughput_digest(documents[0]), exit=status)


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
    present = subprocess.run(["git", "-C", str(candidate), "cat-file", "-e", f"{head}:{SCALING_REQUEST}"],
                             capture_output=True, timeout=GIT_TIMEOUT_SECONDS, check=False).returncode == 0
    return bool(changed) and present


def measure_scaling(candidate: Path, bins: Path, work: Path, evidence: Path,
                    binaries: dict, omissions: dict) -> tuple[list[str], dict]:
    """Run every scaling series on the candidate from the checked-out base; (reasons, digest)."""
    reasons: list[str] = []
    bundles: dict = {}
    for name, arguments in SCALING_PROFILE["series"].items():
        output = work / "scaling" / name
        output.parent.mkdir(parents=True, exist_ok=True)
        status = run(["./build.sh", "bench_throughput", "scale", "--compiler", str(bins / "ide-cand"),
                      "--output", str(output), *arguments], candidate, evidence / f"scaling-{name}.log",
                     SCALING_TIMEOUT_SECONDS)
        if output.is_dir():
            problems, found = export_tree(output, evidence / "scaling" / name, evidence, SCALING_IGNORE,
                                          SCALING_REQUIRED)
            reasons.extend(problems)
            note_omissions(omissions, f"scaling/{name}", found)
        documents = []
        for leaf in ("scaling.json", "scaling-metadata.json"):
            try:
                documents.append(json.loads((output / leaf).read_text(encoding="utf-8")))
            except (OSError, ValueError):
                documents.append(None)
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
    parser.add_argument("--mode", choices=sorted(MODES), required=True)
    for name in IDENTITY_KEYS[1:]:
        parser.add_argument("--" + name.replace("_", "-"), required=True)
    return parser.parse_args(argv)


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


def measure(arguments: argparse.Namespace, candidate: Path, work: Path, evidence: Path, bins: Path, log: Path,
            receipt: dict, summaries: list) -> None:
    """Build both revisions and run the lab, corpus and scaling legs; the lab summary goes to summaries."""
    reasons = receipt["reasons"]
    summary = None
    inline_requested = arguments.mode == "pull" and inline_acceptance_requested(candidate)
    receipt["inline_acceptance"] = {"requested": inline_requested,
        "request_line": INLINE_ACCEPTANCE_REQUEST_LINE if inline_requested else None,
        "profile": INLINE_ACCEPTANCE_PROFILE if inline_requested else None,
        "status": "pending" if inline_requested else "not requested"}
    if not reasons:
        for role, commit in (("baseline", arguments.base), ("candidate", arguments.head), ("closure", arguments.base)):
            mark(receipt, evidence, f"build-{role}")
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
                                                              EVIDENCE_IGNORE, ("acceptance.json",))
                    inline["evidence_omissions"] = omissions
                    errors = list(export_problems)
                    try:
                        inline_summary = json.loads((inline_dir / "acceptance.json").read_text(encoding="utf-8"))
                        inline["summary"] = inline_summary
                        errors.extend(validate_inline_acceptance(inline_summary, arguments.head,
                                                                 receipt["binaries"]["candidate"]["sha256"]))
                    except (OSError, ValueError) as error:
                        errors.append(f"issue #48 inline self-host acceptance receipt unreadable: {error}")
                    if inline_status != 0:
                        errors.append(f"issue #48 inline self-host acceptance exited {inline_status}")
                    inline["errors"] = errors
                    inline["status"] = "complete" if not errors else "failed"

    if not reasons:
        lab = work / "lab"
        mark(receipt, evidence, "lab")
        measured = time.monotonic()
        status = run([sys.executable, "-B", str(arguments.lab.resolve()), "compare",
                      "--baseline", str(bins / "ide-base"), "--candidate", str(bins / "ide-cand"),
                      "--repo-root", str(candidate), "--cpu", str(PROFILE["cpu"]), "--output", str(lab),
                      "--target-minutes", str(PROFILE["target_minutes"]), "--warmups", str(PROFILE["warmups"])],
                     candidate, evidence / "lab.log", LAB_TIMEOUT_SECONDS)
        receipt["timings"]["measurement_seconds"] = round(time.monotonic() - measured, 3)
        receipt["lab"]["exit"] = status
        mark(receipt, evidence, "lab-evidence")
        reasons.extend(collect_evidence(lab, evidence, receipt.setdefault("evidence_omissions", {})))
        try:
            summary = json.loads((lab / "summary.json").read_text(encoding="utf-8"))
            summaries[:] = [summary]
        except (OSError, ValueError) as error:
            reasons.append(f"lab summary unreadable: {error}")
        if status != 0:
            reasons.append(f"uarch_lab compare exited {status}")
        mark(receipt, evidence, "throughput")
        measured = time.monotonic()
        corpus, receipt["throughput"] = measure_throughput(candidate, bins, work, evidence, arguments.base,
                                                           arguments.head, receipt["binaries"],
                                                           receipt.setdefault("evidence_omissions", {}))
        receipt["timings"]["throughput_seconds"] = round(time.monotonic() - measured, 3)
        reasons.extend(corpus)
        if arguments.mode == "pull" and scaling_requested(candidate, arguments.base, arguments.head):
            mark(receipt, evidence, "scaling")
            measured = time.monotonic()
            receipt["scaling_profile"] = SCALING_PROFILE
            scaled, receipt["scaling"] = measure_scaling(candidate, bins, work, evidence, receipt["binaries"],
                                                      receipt.setdefault("evidence_omissions", {}))
            receipt["timings"]["scaling_seconds"] = round(time.monotonic() - measured, 3)
            reasons.extend(scaled)
        mark(receipt, evidence, "validate")
        for role, name in (("baseline", "ide-base"), ("candidate", "ide-cand")):
            if sha256(bins / name) != receipt["binaries"][role]["sha256"]:
                reasons.append(f"{role} binary changed during measurement")
        reasons.extend(classify(summary, receipt["binaries"]))
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



def main(argv: list[str] | None = None) -> int:
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
    identity = {key: getattr(arguments, key) for key in IDENTITY_KEYS}
    receipt = {"schema": RECEIPT_SCHEMA, "mode": arguments.mode, "state": "failed", "reasons": [], "identity": identity,
               "profile": PROFILE, "throughput_profile": THROUGHPUT_PROFILE, "host": {"hostname": socket.gethostname(), "cpu_model": cpu_model()},
               "toolchain": toolchain(), "binaries": {}, "lab": {},
               "timings": {"started_at": started_at, "build_seconds": {}},
               "inline_acceptance": {"requested": False, "status": "not requested"}}
    reasons = receipt["reasons"]
    log = evidence / "build.log"
    bins = work / "bin"
    bins.mkdir()
    summary = None

    # Identity first. A main commit has the base on its first-parent chain (its
    # first parent, or a range's measured ancestor) and, when a queue merge
    # produced it, the pull request head as second parent (else it is its own
    # pull head); a pull request head is its own pull head and descends from
    # the base (its merge base). Both trees must match.
    problem = host_problem(receipt)
    if problem:
        reasons.append(problem)
    try:
        if arguments.mode == "main":
            second = subprocess.run(["git", "-C", str(candidate), "rev-parse", "--verify", "--quiet", "HEAD^2"],
                                    capture_output=True, text=True, timeout=GIT_TIMEOUT_SECONDS, check=False).stdout.strip()
            chain = git(candidate, "rev-list", "--first-parent", f"--max-count={RECONCILE_DEPTH}", "HEAD^1").split()
            parents = (arguments.base if arguments.base in chain else "base is not on the first-parent chain",
                       second or git(candidate, "rev-parse", "HEAD"))
            if arguments.base in chain:
                receipt["coverage"] = {"first_parent": chain[0], "range": str(chain.index(arguments.base) + 1)}
        else:
            ancestry = subprocess.run(["git", "-C", str(candidate), "merge-base", "--is-ancestor", arguments.base, "HEAD"],
                                      capture_output=True, timeout=GIT_TIMEOUT_SECONDS, check=False).returncode == 0
            parents = (arguments.base if ancestry else "base is not an ancestor", git(candidate, "rev-parse", "HEAD"))
        observed = (git(candidate, "rev-parse", "HEAD"), *parents, git(candidate, "rev-parse", "HEAD^{tree}"),
                    git(candidate, "rev-parse", arguments.base + "^{tree}"))
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError) as error:
        observed = None
        reasons.append(f"candidate checkout cannot be inspected: {error}")
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
    aborted = None
    try:
        measure(arguments, candidate, work, evidence, bins, log, receipt, summaries)
    except BaseException as error:  # noqa: BLE001 - recorded, then re-raised after the receipt is written
        reasons.append(f"attempt aborted in phase {receipt.get('phase', 'start')}: {error.__class__.__name__}: {error}")
        receipt["state"] = "failed"
        if not isinstance(error, Exception):
            aborted = error
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
