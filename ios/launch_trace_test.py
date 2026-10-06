"""Run the native, allocation-independent iOS trace producer on POSIX hosts."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <buster/lib/entry_point.h>
#include <buster/lib/system_headers.h>
void buster_ios_launch_trace(String8 stage);
void buster_ios_launch_process_trace(void);
#if TRACE_CLOCK_FAILURE
static int trace_clock_failure(clockid_t clock, struct timespec* value)
{
    (void)clock;
    (void)value;
    return -1;
}
static int trace_cpu_failure(int who, struct rusage* value)
{
    (void)who;
    (void)value;
    return -1;
}
#define clock_gettime trace_clock_failure
#define getrusage trace_cpu_failure
#endif
#include <buster/lib/entry_point_ios.c>
int main(void)
{
    buster_ios_launch_trace(S8("main"));
    buster_ios_launch_process_trace();
    struct timespec delay = {.tv_nsec = 100000000};
    nanosleep(&delay, 0);
    buster_ios_launch_trace(S8("fixtures-ready"));
    buster_ios_launch_trace(S8("this-stage-is-deliberately-too-long-to-fit-the-fixed-sixty-four-byte-stage-bound"));
    return 0;
}
'''


class LaunchTraceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="buster-ios-trace-")
        cls.addClassCleanup(cls.directory.cleanup)
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            raise RuntimeError("A native C compiler is required")
        source = Path(cls.directory.name) / "trace.c"
        source.write_text(HARNESS)
        cls.binaries = {}
        for fault in (0, 1):
            binary = source.with_name(f"trace-{fault}")
            # Darwin's sysctl headers expose BSD types used by system_headers;
            # strict POSIX-only visibility hides them in this standalone harness.
            platform_flags = ["-D_DARWIN_C_SOURCE"] if sys.platform == "darwin" else []
            subprocess.run([compiler, "-std=c11", "-D_POSIX_C_SOURCE=200809L",
                            *platform_flags,
                            f"-DTRACE_CLOCK_FAILURE={fault}", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-function", "-Wno-unused-variable", "-fwrapv",
                            "-fno-strict-aliasing", "-funsigned-char", f"-I{ROOT / 'src'}",
                            str(source), "-o", str(binary)], check=True, timeout=30)
            cls.binaries[fault] = binary

    def run_trace(self, enabled, fault=0):
        environment = dict(os.environ)
        environment.pop("BUSTER_IOS_LAUNCH_TRACE", None)
        if enabled is not None:
            environment["BUSTER_IOS_LAUNCH_TRACE"] = enabled
        result = subprocess.run([self.binaries[fault]], env=environment,
                                capture_output=True, check=True, timeout=5)
        self.assertEqual(result.stdout, b"")
        return result.stderr.decode().splitlines()

    def test_opt_in_and_fixed_record_bound(self):
        for enabled in (None, "", "0", "10", "true"):
            with self.subTest(enabled=enabled):
                self.assertEqual(self.run_trace(enabled), [])
        lines = self.run_trace("1")
        self.assertEqual(len(lines), 3)
        process = self.process_record(lines.pop(1))
        records = []
        for line in lines:
            self.assertTrue(line.startswith("BUSTER_IOS_LAUNCH_V1 "))
            self.assertLess(len(line) + 1, 512)
            records.append(dict(field.split("=", 1) for field in line.split()[1:]))
        self.assertEqual([r["stage"] for r in records], ["main", "fixtures-ready"])
        self.assertEqual(records[0]["pid"], records[1]["pid"])
        for record in records:
            for field in ("monotonic_status", "wall_status", "cpu_status"):
                self.assertEqual(record[field], "0")
            for field in ("monotonic_us", "wall_us", "process_cpu_us"):
                self.assertGreater(int(record[field]), 0)
        self.assertGreaterEqual(int(records[1]["monotonic_us"]) - int(records[0]["monotonic_us"]), 90000)
        self.assertGreaterEqual(int(records[1]["process_cpu_us"]), int(records[0]["process_cpu_us"]))
        self.assertEqual(process["pid"], records[0]["pid"])
        if sys.platform == "darwin":
            # The kernel start time precedes main; the split is the #2819 attribution.
            self.assertEqual(process["start_status"], "0")
            start = int(process["start_wall_us"])
            self.assertGreater(start, 0)
            self.assertLessEqual(start, int(records[0]["wall_us"]))
        else:
            self.assertEqual(process["start_status"], "-1")
            self.assertEqual(process["start_wall_us"], "0")

    def process_record(self, line):
        self.assertTrue(line.startswith("BUSTER_IOS_PROCESS_V1 "))
        self.assertLess(len(line) + 1, 256)
        record = dict(field.split("=", 1) for field in line.split()[1:])
        self.assertEqual(sorted(record), ["pid", "start_status", "start_wall_us"])
        return record

    def test_failed_queries_are_explicit(self):
        lines = self.run_trace("1", fault=1)
        self.assertEqual(len(lines), 3)
        self.process_record(lines.pop(1))
        for line in lines:
            for field in ("monotonic_status", "wall_status", "cpu_status"):
                self.assertIn(f"{field}=-1", line)
            for field in ("monotonic_us", "wall_us", "process_cpu_us"):
                self.assertIn(f"{field}=0", line)


if __name__ == "__main__":
    unittest.main()
