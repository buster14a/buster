#!/usr/bin/env python3
"""Offline parser and workflow-contract regressions for the eBPF kernel oracle gate (#1625)."""
import contextlib
import io
import os
from pathlib import Path
import re
import stat
import tempfile
import unittest
import unittest.mock

import ci_ebpf_kernel_oracle as oracle


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github" / "workflows" / "ci.yml"

AVAILABLE_ROWS = {
    "codegen_test_ebpf_scalars": 1152,
    "codegen_test_ebpf_argument_images": 6912,
    "codegen_test_ebpf_stack_liveness": 420,
    "codegen_test_ebpf_local_aggregates": 490,
    "codegen_test_ebpf_kernel_regressions": 3469,
    "codegen_test_canonical_entry": 36,
}


def codegen_output(rows):
    lines = []
    for name, executions in rows.items():
        if executions is None:
            lines.append(f"eBPF kernel verifier/JIT for {name}: unavailable, VM only")
        else:
            lines.append(f"eBPF kernel verifier/JIT for {name}: available, {executions} executions compared")
    return "\n".join(lines) + "\n"


def metamorphic_output(available=1, status="passed", transforms_unavailable=False):
    lines = [
        f"METAMORPHIC_EBPF_KERNEL available={available}",
    ]
    if transforms_unavailable:
        lines.append("METAMORPHIC_TRANSFORMS_UNAVAILABLE target=ebpf mask=1024 reason=local-calls-not-interpreted")
    lines.append(f"METAMORPHIC_SUMMARY cases=4 status={status}")
    return "\n".join(lines) + "\n"


class CodegenParserTests(unittest.TestCase):
    def check(self, rows):
        return oracle.check_codegen(codegen_output(rows))

    def test_all_expected_rows_available(self):
        errors, unavailable_only, count, available, executions = self.check(dict(AVAILABLE_ROWS))
        self.assertEqual(errors, [])
        self.assertEqual(count, 6)
        self.assertEqual(available, 6)
        self.assertEqual(executions, sum(AVAILABLE_ROWS.values()))

    def test_one_unavailable_row_is_an_unavailability_error(self):
        rows = dict(AVAILABLE_ROWS, codegen_test_ebpf_scalars=None)
        errors, unavailable_only, _, available, _ = self.check(rows)
        self.assertEqual(len(errors), 1)
        self.assertIn("codegen_test_ebpf_scalars", errors[0])
        self.assertTrue(unavailable_only)
        self.assertEqual(available, 5)

    def test_missing_expected_row_is_an_error(self):
        rows = dict(AVAILABLE_ROWS)
        del rows["codegen_test_canonical_entry"]
        errors, unavailable_only, _, _, _ = self.check(rows)
        self.assertEqual(len(errors), 1)
        self.assertIn("missing expected eBPF kernel oracle row", errors[0])
        self.assertTrue(unavailable_only)

    def test_zero_executions_is_a_failure(self):
        rows = dict(AVAILABLE_ROWS, codegen_test_ebpf_scalars=0)
        errors, unavailable_only, _, _, _ = self.check(rows)
        self.assertEqual(len(errors), 1)
        self.assertIn("0 executions", errors[0])
        self.assertFalse(unavailable_only)

    def test_duplicate_row_is_a_failure(self):
        text = codegen_output(AVAILABLE_ROWS)
        text += "eBPF kernel verifier/JIT for codegen_test_ebpf_scalars: available, 1 executions compared\n"
        errors, unavailable_only, _, _, _ = oracle.check_codegen(text)
        self.assertEqual(len(errors), 1)
        self.assertIn("duplicate", errors[0])
        self.assertFalse(unavailable_only)

    def test_extra_unknown_available_row_is_accepted(self):
        rows = dict(AVAILABLE_ROWS, codegen_test_ebpf_future=7)
        errors, _, count, available, executions = self.check(rows)
        self.assertEqual(errors, [])
        self.assertEqual(count, 7)
        self.assertEqual(available, 7)
        self.assertEqual(executions, sum(AVAILABLE_ROWS.values()) + 7)

    def test_extra_unknown_unavailable_row_is_an_error(self):
        rows = dict(AVAILABLE_ROWS, codegen_test_ebpf_future=None)
        errors, unavailable_only, _, _, _ = self.check(rows)
        self.assertEqual(len(errors), 1)
        self.assertIn("codegen_test_ebpf_future", errors[0])
        self.assertTrue(unavailable_only)


class MetamorphicParserTests(unittest.TestCase):
    def test_available_kernel_passes(self):
        errors, _, kernel = oracle.check_metamorphic(metamorphic_output())
        self.assertEqual(errors, [])
        self.assertEqual(kernel, 1)

    def test_unavailable_kernel_is_an_unavailability_error(self):
        errors, unavailable_only, kernel = oracle.check_metamorphic(metamorphic_output(available=0))
        self.assertEqual(len(errors), 1)
        self.assertTrue(unavailable_only)
        self.assertEqual(kernel, 0)

    def test_missing_kernel_line_is_an_error(self):
        errors, _, kernel = oracle.check_metamorphic("METAMORPHIC_SUMMARY status=passed\n")
        self.assertEqual(len(errors), 1)
        self.assertIn("exactly one", errors[0])
        self.assertEqual(kernel, "missing")

    def test_failed_summary_is_a_failure(self):
        errors, unavailable_only, _ = oracle.check_metamorphic(metamorphic_output(status="failed"))
        self.assertEqual(len(errors), 1)
        self.assertIn("status=passed", errors[0])
        self.assertFalse(unavailable_only)

    def test_transforms_unavailable_is_an_error(self):
        errors, unavailable_only, _ = oracle.check_metamorphic(
            metamorphic_output(available=0, transforms_unavailable=True))
        self.assertIn("METAMORPHIC_TRANSFORMS_UNAVAILABLE", " ".join(errors))
        self.assertTrue(unavailable_only)


FAKE_IDE = """\
#!/usr/bin/env python3
import os
import sys

if sys.argv[1] == "test":
    rows = {
        "codegen_test_ebpf_scalars": 1152,
        "codegen_test_ebpf_argument_images": 6912,
        "codegen_test_ebpf_stack_liveness": 420,
        "codegen_test_ebpf_local_aggregates": 490,
        "codegen_test_ebpf_kernel_regressions": 3469,
        "codegen_test_canonical_entry": 36,
    }
    for name, executions in rows.items():
        if os.environ.get("FAKE_KERNEL") == "0":
            print(f"eBPF kernel verifier/JIT for {name}: unavailable, VM only")
        else:
            print(f"eBPF kernel verifier/JIT for {name}: available, {executions} executions compared")
elif sys.argv[1] == "metamorphic":
    available = os.environ.get("FAKE_KERNEL", "1")
    print(f"METAMORPHIC_EBPF_KERNEL available={available}")
    if available == "0":
        print("METAMORPHIC_TRANSFORMS_UNAVAILABLE target=ebpf mask=1024 reason=local-calls-not-interpreted")
    print("METAMORPHIC_SUMMARY cases=4 status=passed")
"""

# The fixture ide is a Python script; only POSIX exec can run it directly.
@unittest.skipIf(os.name == "nt", "fake ide script cannot exec on Windows")
class MainEndToEndTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.ide = self.root / "ide"
        self.ide.write_text(FAKE_IDE)
        self.ide.chmod(self.ide.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        self.logs = self.root / "logs"
        self.meta = self.root / "meta"

    def run_main(self, fake_kernel):
        output = io.StringIO()
        with unittest.mock.patch.dict(os.environ, {"FAKE_KERNEL": fake_kernel}, clear=False):
            with contextlib.redirect_stdout(output):
                status = oracle.main([
                    "--ide", str(self.ide),
                    "--log-directory", str(self.logs),
                    "--metamorphic-output", str(self.meta),
                ])
        return status, output.getvalue()

    def test_available_kernel_exits_zero(self):
        status, output = self.run_main("1")
        self.assertEqual(status, 0)
        self.assertIn("CI_EBPF_KERNEL_ORACLE_V1 status=available", output)
        self.assertIn("codegen_rows=6 codegen_available=6", output)
        self.assertIn("metamorphic_kernel=1", output)
        self.assertIn("available, 1152 executions compared",
                      (self.logs / "ebpf-kernel-codegen.log").read_text())

    def test_unavailable_kernel_exits_one_and_writes_logs(self):
        status, output = self.run_main("0")
        self.assertEqual(status, 1)
        self.assertIn("status=unavailable", output)
        self.assertIn("metamorphic_kernel=0", output)
        codegen_log = (self.logs / "ebpf-kernel-codegen.log").read_text()
        self.assertIn("unavailable, VM only", codegen_log)
        metamorphic_log = (self.logs / "ebpf-kernel-metamorphic.log").read_text()
        self.assertIn("METAMORPHIC_EBPF_KERNEL available=0", metamorphic_log)


class WorkflowContractTests(unittest.TestCase):
    def setUp(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        self.native = workflow.split("\n  native:", 1)[1].split("\n  mobile:", 1)[0]

    def test_ebpf_kernel_step_is_linux_x86_64_only(self):
        match = re.search(r"id: ebpf_kernel\n        if: (.+)", self.native)
        self.assertIsNotNone(match)
        condition = match.group(1)
        self.assertIn("!cancelled()", condition)
        self.assertIn("steps.checkout.outcome == 'success'", condition)
        self.assertIn("matrix.os == 'linux'", condition)
        self.assertIn("matrix.arch == 'x86_64'", condition)
        self.assertNotIn("steps.modes", condition)
        self.assertNotIn("steps.differential", condition)

    def test_ebpf_kernel_step_grants_capabilities_to_a_copy(self):
        step = self.native.split("id: ebpf_kernel\n", 1)[1].split("\n      - name:", 1)[0]
        self.assertIn("sudo setcap cap_bpf,cap_perfmon+ep", step)
        self.assertIn("tools/ci_ebpf_kernel_oracle.py", step)

    def test_required_results_include_ebpf_only_on_linux_x86_64(self):
        for required in re.findall(r"BUSTER_CI_REQUIRED: \$\{\{ (.+) \}\}", self.native):
            linux = re.search(r"matrix\.os == 'linux' && matrix\.arch == 'x86_64' && '([^']+)'", required)
            self.assertIsNotNone(linux, required)
            self.assertIn("ebpf_kernel", linux.group(1))
            branches = re.findall(r"'([^']+)'", required)
            for branch in branches:
                if branch == linux.group(1):
                    continue
                self.assertNotIn("ebpf_kernel", branch)


if __name__ == "__main__":
    unittest.main()
