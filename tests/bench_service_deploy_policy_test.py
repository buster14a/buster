#!/usr/bin/env python3
"""Static policy tests for the dedicated-host installer and verifier."""

from __future__ import annotations

import ast
import importlib.util
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEPLOY = ROOT / "tools" / "bench_service" / "deploy"
INSTALL = DEPLOY / "install_9700x.py"
VERIFY = DEPLOY / "verify_9700x.py"
UNIT = DEPLOY / "buster-bench.service"
SLICE = DEPLOY / "buster-bench.slice"
SYSUSERS = DEPLOY / "buster-bench.sysusers.conf"
TMPFILES = DEPLOY / "buster-bench.tmpfiles.conf"
SUDOERS = DEPLOY / "buster-bench-gateway.sudoers"
POLKIT = DEPLOY / "60-buster-bench.rules"


class PolicyError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise PolicyError(message)


def text(path: Path) -> str:
    require(path.is_file(), f"missing {path.relative_to(ROOT)}")
    data = path.read_text(encoding="utf-8")
    require("\r" not in data and data.endswith("\n"), f"non-canonical text: {path}")
    return data


def parse_script(path: Path) -> tuple[str, ast.Module]:
    data = text(path)
    return data, ast.parse(data, filename=str(path))


def call_name(call: ast.Call) -> str:
    value = call.func
    if isinstance(value, ast.Name):
        return value.id
    if isinstance(value, ast.Attribute):
        parts = [value.attr]
        value = value.value
        while isinstance(value, ast.Attribute):
            parts.append(value.attr)
            value = value.value
        if isinstance(value, ast.Name):
            parts.append(value.id)
        return ".".join(reversed(parts))
    return ""


def check_scripts() -> None:
    installer, install_tree = parse_script(INSTALL)
    verifier, verify_tree = parse_script(VERIFY)

    for path, data, tree in ((INSTALL, installer, install_tree), (VERIFY, verifier, verify_tree)):
        require("argparse" not in data and "input(" not in data, f"caller-selected input in {path.name}")
        for node in ast.walk(tree):
            if isinstance(node, ast.Call):
                name = call_name(node)
                require(name not in {"eval", "exec", "os.system", "subprocess.call"},
                        f"unsafe process surface {name} in {path.name}")
                for keyword in node.keywords:
                    if keyword.arg == "shell":
                        require(isinstance(keyword.value, ast.Constant) and keyword.value.value is False,
                                f"shell execution in {path.name}")

    install_markers = (
        "len(sys.argv)!=1",
        "os.geteuid()!=0",
        "os.O_NOFOLLOW",
        "os.O_EXCL",
        "fcntl.LOCK_EX|fcntl.LOCK_NB",
        "active lease; installation forbidden",
        "recorded lease inode drift",
        "lease inode changed during installation",
        "systemd-sysusers",
        "systemd-tmpfiles",
        "visudo",
        "daemon-reload",
        "capture-host-identity",
        "installed.sha256",
        "service stopped; dispatch disabled",
    )
    for marker in install_markers:
        require(marker in installer, f"installer missing policy marker: {marker}")
    require("LOCK.unlink" not in installer and "os.unlink(LOCK" not in installer,
            "installer may replace the stable lease")
    require("subprocess.run(a" in installer and "env=ENV" in installer,
            "installer process environment is not fixed")

    verify_markers = (
        "host/kernel/topology/scheduler drift",
        "identity manifest closure mismatch",
        "stable lease drift",
        "competing/stale units",
        "/proc/self/mountinfo",
        " - cgroup2 ",
        "cpuset.cpus.effective",
        "memory.max",
        "memory.swap.max",
        "pids.max",
        "cgroup.events",
        "MainPID cgroup mismatch",
        "MainPID credentials drift",
        "service-invocation-id",
        "boot-id",
        "evidence-sha256",
    )
    for marker in verify_markers:
        require(marker in verifier, f"verifier missing policy marker: {marker}")
    require("/buster-bench.slice/buster-bench.service" in verifier,
            "live verifier does not bind exact cgroup ancestry")

    spec = importlib.util.spec_from_file_location("buster_bench_verify_policy", VERIFY)
    require(spec is not None and spec.loader is not None, "cannot load verifier module")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    require(module.cpus("0-2,7") == {0, 1, 2, 7}, "CPU-list parser changed")
    require(module.limit("max") is None and module.limit("8192") == 8192,
            "cgroup-limit parser changed")


def check_units() -> None:
    unit = text(UNIT)
    service_required = (
        "User=buster-bench",
        "Group=buster-bench",
        "SupplementaryGroups=buster-bench-candidate",
        "Slice=buster-bench.slice",
        "ExecStartPre=+/usr/local/libexec/buster-bench-verify preflight",
        "ExecStart=/usr/local/libexec/buster-bench-service serve ",
        "NoNewPrivileges=yes",
        "PrivateNetwork=yes",
        "ProtectSystem=strict",
        "ReadOnlyPaths=/opt/buster-bench/installed",
        "ReadOnlyPaths=/etc/buster-bench",
        "ReadWritePaths=/var/lib/buster-bench",
        "RestrictAddressFamilies=AF_UNIX",
    )
    for marker in service_required:
        require(marker in unit, f"service missing hardening: {marker}")
    require("Environment=" not in unit and "EnvironmentFile=" not in unit,
            "service exposes a mutable environment surface")

    slice_text = text(SLICE)
    for marker in ("AllowedCPUs=2", "MemoryMax=8G", "MemorySwapMax=0", "TasksMax=256"):
        require(marker in slice_text, f"slice missing fixed limit: {marker}")


def check_identities_and_paths() -> None:
    sysusers = text(SYSUSERS)
    for marker in (
        "g buster-bench -",
        "g buster-bench-candidate -",
        "g buster-bench-dispatch -",
        "g buster-bench-admin -",
        "u buster-bench -",
        "u buster-bench-candidate -",
        "m buster-bench buster-bench-candidate",
    ):
        require(marker in sysusers, f"sysusers missing: {marker}")

    tmpfiles = text(TMPFILES)
    for marker in (
        "d /var/lib/buster-bench 0710 root buster-bench-candidate -",
        "d /var/lib/buster-bench/queue 0700 buster-bench buster-bench -",
        "d /var/lib/buster-bench/lease 0710 root buster-bench -",
        "d /var/lib/buster-bench/workspaces 2710 buster-bench buster-bench-candidate -",
        "d /var/lib/buster-bench/verification 0700 buster-bench buster-bench -",
        "d /opt/buster-bench/installed 0550 root buster-bench -",
    ):
        require(marker in tmpfiles, f"tmpfiles missing fixed path: {marker}")
    require("host.lock" not in "\n".join(
        line for line in tmpfiles.splitlines() if line and not line.startswith("#")
    ), "tmpfiles may replace or age the stable lease")


def check_authorization() -> None:
    sudoers = text(SUDOERS)
    require("%buster-bench-dispatch ALL=(buster-bench:buster-bench)" in sudoers,
            "dispatch identity is not reduced to the service principal")
    require("NOPASSWD: NOEXEC: BUSTER_BENCH_GATEWAY" in sudoers,
            "gateway command is not noexec/fixed")
    for operation in ("capabilities", "submit *", "status *", "result *", "cancel *", "logs *"):
        require(f"buster-bench-service gateway {operation}" in sudoers,
                f"sudoers missing typed gateway operation: {operation}")
    for forbidden in ("/bin/sh", "/bin/bash", "systemctl", "systemd-run", "ssh", "env *"):
        require(forbidden not in sudoers, f"sudoers exposes forbidden command: {forbidden}")

    polkit = text(POLKIT)
    for marker in (
        'action.id !== "org.freedesktop.systemd1.manage-units"',
        'subject.user === "buster-bench"',
        'subject.isInGroup("buster-bench-admin")',
        'unit === "buster-bench.service"',
        "base-generate|base-build|candidate-generate|candidate-build|throughput",
        "return polkit.Result.NO;",
    ):
        require(marker in polkit, f"polkit policy missing: {marker}")
    require("buster-bench-dispatch" not in polkit,
            "dispatch identity may manage systemd units")


def main() -> int:
    try:
        check_scripts()
        check_units()
        check_identities_and_paths()
        check_authorization()
    except (PolicyError, OSError, SyntaxError) as error:
        print(f"DEPLOY_POLICY_FAIL {error}", file=sys.stderr)
        return 1
    print("DEPLOY_POLICY_PASS installed 9700X boundary is fixed and fail-closed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
