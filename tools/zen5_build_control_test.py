"""Offline regressions for the same-source Zen 5 cross-build control."""

from __future__ import annotations

from copy import deepcopy
from typing import Any

from zen5_aa_noise import canonical_bytes, derived_labels, sha256_bytes
from zen5_aa_noise_test import synthetic_capture as synthetic_aa_capture
from zen5_build_control import CAPTURE_SCHEMA, analyze_capture, validate_capture


def build(label: str, root: str, source_identity: str) -> dict[str, Any]:
    return {
        "source_revision": "b" * 40,
        "source_tree": "c" * 40,
        "source_identity_sha256": source_identity,
        "build_root": root,
        "toolchain_identity_sha256": "2" * 64,
        "build_environment_sha256": "3" * 64,
        "build_argv": ["./build.sh", "build", "--config", "Release", "-t", "ide"],
        "compile_argv": ["clang", "-O3", "-fwrapv", "-c", f"{root}/src/ide.c", "-o", f"{root}/build/ide.o"],
        "link_argv": ["clang", f"{root}/build/ide.o", "-o", f"{root}/build/ide"],
        "compile_commands_sha256": ("4" if label == "A" else "5") * 64,
        "normalized_commands_sha256": "1" * 64,
        "build_log_sha256": ("6" if label == "A" else "7") * 64,
        "build_started_monotonic_ns": 100 if label == "A" else 300,
        "build_finished_monotonic_ns": 200 if label == "A" else 400,
        "binary": {
            "path": f"/frozen/{label}/ide",
            "size": 12000,
            "mode": 0o555,
            "sha256": ("8" if label == "A" else "9") * 64,
        },
        "text_section": {
            "file_offset": 4096,
            "virtual_address": 0x1000 if label == "A" else 0x1050,
            "size": 7000,
            "sha256": "a" * 64,
        },
    }


def synthetic_capture(kind: str = "cross-root") -> dict[str, Any]:
    capture = synthetic_aa_capture()
    capture["schema"] = CAPTURE_SCHEMA
    capture["purpose"] = "same-source cross-build calibration"
    capture["control_kind"] = kind
    capture["predeclared_family_sha256"] = "0" * 64
    capture["staging_paths"] = {"path0": "/staged/path0/ide", "path1": "/staged/path1/ide"}
    capture["builds"] = {
        "A": build("A", "/build/one", capture["source_identity_sha256"]),
        "B": build("B", "/build/one" if kind == "same-root-rebuild" else "/build/two", capture["source_identity_sha256"]),
    }
    del capture["binary_paths"]
    for slot in capture["observations"]:
        first_label, second_label = derived_labels(slot)
        slot["first_binary_sha256"] = capture["builds"][first_label]["binary"]["sha256"]
        slot["second_binary_sha256"] = capture["builds"][second_label]["binary"]["sha256"]
    return capture


def run_self_test() -> int:
    for kind in ("cross-root", "same-root-rebuild"):
        capture = synthetic_capture(kind)
        problems, invalid = validate_capture(capture)
        assert not problems and not invalid, (problems, invalid)
        analysis = analyze_capture(capture, sha256_bytes(canonical_bytes(capture)))
        assert analysis["analysis_status"] == "descriptive-complete"
        assert analysis["performance_decision"] == "not-evaluated"
        assert analysis["builds"]["text_section_virtual_address_delta"] == 80
        assert analysis["metrics"]["wall_time_ns"]["build_effect"]["count"] == 120

    changed_source = synthetic_capture()
    changed_source["builds"]["B"]["source_identity_sha256"] = "0" * 64
    problems, _ = validate_capture(changed_source)
    assert any("source_identity_sha256" in problem for problem in problems)

    changed_flags = synthetic_capture()
    changed_flags["builds"]["B"]["compile_argv"][1] = "-O2"
    problems, _ = validate_capture(changed_flags)
    assert any("normalized compile_argv" in problem for problem in problems)

    false_prefix = synthetic_capture()
    false_prefix["builds"]["A"]["compile_argv"].append("-I/build/one-extra")
    false_prefix["builds"]["B"]["compile_argv"].append("-I/build/two-extra")
    problems, _ = validate_capture(false_prefix)
    assert any("normalized compile_argv" in problem for problem in problems)

    wrong_kind = synthetic_capture("same-root-rebuild")
    wrong_kind["builds"]["B"]["build_root"] = "/build/two"
    problems, _ = validate_capture(wrong_kind)
    assert any("one configured build root" in problem for problem in problems)

    malformed_kind = synthetic_capture()
    malformed_kind["control_kind"] = []
    problems, _ = validate_capture(malformed_kind)
    assert any("control_kind" in problem for problem in problems)

    wrong_binary = synthetic_capture()
    slot = wrong_binary["observations"][0]
    slot["first_binary_sha256"] = "0" * 64
    slot["valid"] = False
    slot["invalid_reasons"] = ["first binary digest differs from build A"]
    wrong_binary["capture_status"] = "invalid"
    wrong_binary["invalid_reasons"] = ["observation 0: first binary digest differs from build A"]
    problems, invalid = validate_capture(wrong_binary)
    assert not problems and invalid, (problems, invalid)
    assert analyze_capture(wrong_binary, sha256_bytes(canonical_bytes(wrong_binary)))["metrics"] is None

    concealed_binary = deepcopy(wrong_binary)
    concealed_binary["observations"][0]["valid"] = True
    concealed_binary["observations"][0]["invalid_reasons"] = []
    problems, _ = validate_capture(concealed_binary)
    assert any("do not replay" in problem for problem in problems)

    shortened = synthetic_capture()
    shortened["observations"].pop()
    problems, _ = validate_capture(shortened)
    assert any("exactly 120 slots" in problem for problem in problems)

    oversized = synthetic_capture()
    oversized["observations"][0]["first_wall_ns"] = 1 << 10000
    problems, _ = validate_capture(oversized)
    assert any("exceeds uint64" in problem for problem in problems)

    overlapping_builds = synthetic_capture()
    overlapping_builds["builds"]["B"]["build_started_monotonic_ns"] = 150
    problems, _ = validate_capture(overlapping_builds)
    assert any("builds overlap" in problem for problem in problems)

    print("zen5_build_control self-test passed")
    return 0
