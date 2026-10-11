#!/usr/bin/env python3
"""Fail-closed eBPF kernel verifier/JIT oracle gate for native CI (#1625).

Ownership: CI evidence only. The eBPF codegen oracle and the metamorphic
kernel campaign need CAP_BPF (plus CAP_PERFMON for pointer comparisons), which
unprivileged runners do not grant. The Linux x86-64 native lane copies `ide`,
grants that copy the capabilities with `sudo setcap`, and runs this tool
against it. Every verdict is a single CI_EBPF_KERNEL_ORACLE_V1 line; exit 0
means the kernel oracle was actually exercised.

Entry point: main(). Map: parse_codegen_rows/check_codegen inspect
`codegen_test_ebpf_oracle_report` rows, check_metamorphic inspects the
METAMORPHIC_* markers, probe_context records why a refusal happened, and main
runs both child processes, writes their logs and prints the verdict.
"""
import argparse
import os
import platform
import re
import subprocess
import sys
from pathlib import Path

CODEGEN_ROW_PATTERN = re.compile(
    r"^eBPF kernel verifier/JIT for (\S+): (?:available, (\d+) executions compared|unavailable, VM only)$",
    re.MULTILINE,
)

EXPECTED_CODEGEN_ROWS = (
    "codegen_test_ebpf_scalars",
    "codegen_test_ebpf_argument_images",
    "codegen_test_ebpf_stack_liveness",
    "codegen_test_ebpf_local_aggregates",
    "codegen_test_ebpf_kernel_regressions",
    "codegen_test_canonical_entry",
)

VERDICT_PREFIX = "CI_EBPF_KERNEL_ORACLE_V1"


def parse_codegen_rows(output):
    """Map each eBPF oracle report row to its kernel execution count or None."""
    rows = {}
    duplicates = set()
    for name, executions in CODEGEN_ROW_PATTERN.findall(output):
        if name in rows:
            duplicates.add(name)
        rows[name] = int(executions) if executions else None
    return rows, duplicates


def check_codegen(output):
    """Return (errors, unavailable_only, row_count, available_count, executions)."""
    rows, duplicates = parse_codegen_rows(output)
    errors = []
    failures = False
    for name in sorted(duplicates):
        errors.append(f"duplicate eBPF kernel oracle row: {name}")
        failures = True
    for name in EXPECTED_CODEGEN_ROWS:
        if name not in rows:
            errors.append(f"missing expected eBPF kernel oracle row: {name}")
    for name, executions in rows.items():
        if executions is None:
            errors.append(f"eBPF kernel verifier/JIT unavailable for {name} (VM only)")
        elif executions == 0:
            errors.append(f"eBPF kernel verifier/JIT for {name} compared 0 executions")
            failures = True
    available = sum(1 for executions in rows.values() if executions)
    kernel_executions = sum(executions for executions in rows.values() if executions)
    return errors, not failures, len(rows), available, kernel_executions


def check_metamorphic(output):
    """Return (errors, unavailable_only, kernel_flag). kernel_flag: 0, 1, or 'missing'."""
    errors = []
    unavailable_only = True
    kernel_lines = re.findall(r"(?m)^METAMORPHIC_EBPF_KERNEL available=(\d)$", output)
    if len(kernel_lines) != 1:
        errors.append(
            f"expected exactly one METAMORPHIC_EBPF_KERNEL line, found {len(kernel_lines)}"
        )
        kernel = "missing"
    else:
        kernel = int(kernel_lines[0])
        if kernel != 1:
            errors.append("METAMORPHIC_EBPF_KERNEL reports the kernel oracle is unavailable")
    summary_lines = re.findall(r"(?m)^METAMORPHIC_SUMMARY\b.*$", output)
    if not any(" status=passed" in line for line in summary_lines):
        errors.append("no METAMORPHIC_SUMMARY line reports status=passed")
        unavailable_only = False
    if re.search(r"(?m)^METAMORPHIC_TRANSFORMS_UNAVAILABLE target=ebpf\b", output):
        errors.append("METAMORPHIC_TRANSFORMS_UNAVAILABLE target=ebpf was reported")
    return errors, unavailable_only, kernel


def run_and_log(command, log_path, environment=None):
    """Run command, echo combined output to stdout, save it to log_path."""
    log_path.parent.mkdir(parents=True, exist_ok=True)
    process = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
        env=environment,
    )
    sys.stdout.write(process.stdout)
    log_path.write_text(process.stdout, encoding="utf-8")
    return process.returncode, process.stdout


def read_probe(path):
    try:
        return Path(path).read_text(encoding="utf-8").strip()
    except OSError:
        return "unreadable"


def probe_context(ide):
    """One line of kernel/capability context for an unavailable or failed run."""
    getxattr = getattr(os, "getxattr", None)
    if getxattr is None:
        capability = "unsupported"
    else:
        try:
            capability = getxattr(ide, "security.capability").hex()
        except OSError:
            capability = "absent"
    return (
        "probe:"
        f" platform.release={platform.release()}"
        f" unprivileged_bpf_disabled={read_probe('/proc/sys/kernel/unprivileged_bpf_disabled')}"
        f" lockdown={read_probe('/sys/kernel/security/lockdown')}"
        f" ide security.capability={capability}"
    )


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Require the eBPF kernel verifier/JIT oracle in native CI (#1625)."
    )
    parser.add_argument("--ide", required=True, help="capability-granted ide copy")
    parser.add_argument("--log-directory", required=True, type=Path)
    parser.add_argument("--metamorphic-output", required=True, type=Path)
    args = parser.parse_args(argv)

    ide = os.path.abspath(args.ide)
    log_directory = args.log_directory

    codegen_exit, codegen_output = run_and_log(
        [ide, "test", "--module=codegen_tests", "--ci=1"],
        log_directory / "ebpf-kernel-codegen.log",
    )

    metamorphic_environment = dict(
        os.environ,
        BUSTER_METAMORPHIC_SEED="7",
        BUSTER_METAMORPHIC_CASES="4",
        BUSTER_METAMORPHIC_TARGET="ebpf",
        BUSTER_METAMORPHIC_OUTPUT=os.path.abspath(args.metamorphic_output),
    )
    metamorphic_exit, metamorphic_output = run_and_log(
        [ide, "metamorphic"],
        log_directory / "ebpf-kernel-metamorphic.log",
        environment=metamorphic_environment,
    )

    codegen_errors, codegen_unavailable_only, row_count, available_count, kernel_executions = check_codegen(
        codegen_output
    )
    meta_errors, meta_unavailable_only, metamorphic_kernel = check_metamorphic(metamorphic_output)

    errors = []
    unavailable_only = codegen_unavailable_only and meta_unavailable_only
    if codegen_exit != 0:
        errors.append(f"ide test --module=codegen_tests exited {codegen_exit}")
        unavailable_only = False
    if metamorphic_exit != 0:
        errors.append(f"ide metamorphic exited {metamorphic_exit}")
        unavailable_only = False
    errors.extend(codegen_errors)
    errors.extend(meta_errors)

    if not errors:
        status = "available"
    elif unavailable_only:
        status = "unavailable"
    else:
        status = "failed"

    for error in errors:
        print(f"error: {error}")
    if status != "available":
        print(probe_context(ide))
    print(
        f"{VERDICT_PREFIX} status={status}"
        f" codegen_rows={row_count} codegen_available={available_count}"
        f" kernel_executions={kernel_executions}"
        f" metamorphic_kernel={metamorphic_kernel}"
        f" codegen_exit={codegen_exit} metamorphic_exit={metamorphic_exit}"
    )
    return 0 if status == "available" else 1


if __name__ == "__main__":
    sys.exit(main())
