#!/usr/bin/env python3
"""Offline regressions for tools/uarch_lab.py: every parser on recorded perf
and compiler output shapes, the statistics, phase attribution, and one full
`run` against a fake `perf` and a fake `ide` so each step's code path executes
without a PMU.  Run: python3 -B tools/uarch_lab_test.py
"""

import json
import os
import shutil
import stat
import sys
import tempfile
import unittest
from unittest import mock
import xml.etree.ElementTree as ElementTree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import uarch_lab as lab  # noqa: E402

STAT_CSV = """# started on Fri Oct  2 21:45:16 2026

1810466010,ns,duration_time,1810466010,100.00,,
1795.31,msec,task-clock,1795310712,100.00,0.992,CPUs utilized
8693016215,,cycles:u,1795310712,100.00,4.842,GHz
16934571004,,instructions:u,1795310712,100.00,1.95,insn per cycle
<not supported>,,branch-misses:u,0,100.00,,
<not counted>,,L1-dcache-load-misses:u,0,0.00,,
258081,,page-faults,1795310712,100.00,143.751,K/sec
"""

REPEAT_CSV = """1.43,msec,task-clock,19.25%,1704923,100.00,0.674,CPUs utilized
53,,page-faults,0.00%,1704923,100.00,37.069,K/sec
<not supported>,,cycles:u,0.00%,0,100.00,,
"""

TOPDOWN_CSV = """1100000000,,ls_not_halted_cyc,0.12%,1700000000,83.33,,
2000000000,,ex_ret_ops,0.10%,1700000000,83.33,,
,,,,,,24.5,%  frontend_bound
,,,,,,7.1,%  bad_speculation
,,,,,,38.2,%  backend_bound
,,,,,,30.2,%  retiring
"""

INTERVAL_CSV = """0.020098855,40000000,,cycles:u,20000000,100.00,,
0.020098855,60000000,,instructions:u,20000000,100.00,1.50,insn per cycle
0.020098855,59,,minor-faults,20000000,100.00,53.807,K/sec
0.040389647,<not counted>,,cycles:u,0,100.00,,
0.040389647,80000000,,instructions:u,20000000,100.00,,
0.040389647,1,,minor-faults,20000000,100.00,,
"""

CC_METRICS = """CC_METRICS version=1 schema=buster-cc-metrics inputs=1 records=1 ok=1 error=driver.none action=link wall_ns=100000000 peak_rss_bytes=877584384
CC_METRICS_INPUT version=1 index=0 status=ok measured=1 start_ns=10000000 end_ns=90000000 total_ns=80000000 read_ns=0 preprocess_ns=20000000 parse_ns=10000000 analysis_ns=30000000 ir_ns=5000000 codegen_ns=10000000 object_ns=5000000 emit_ns=0 arena_peak_bytes=4366008320 arena_retained_bytes=4270618136 source_bytes=33560496 preprocessed_tokens=3741632 path_hex=2f61
"""

SOURCE_METRICS = """unique.translated_bytes=33522434
lexed.translated_bytes=33560496
lexed.code_lines=389667
preprocessed.tokens=3741632
"""

REPORT_SELF = """# To display the perf.data header info, please use --header/--header-only options.
#
# Samples: 15K of event 'cycles:u'
# Event count (approx.): 5074350792
#
# Overhead  Symbol                                                         IPC   [IPC Coverage]
# ........  .............................................................  ....................
#
     4.32%  [.] ir_validate_canonical_function                             -      -
     3.90%  [.] c_ir_ssa_finish                                            1.23   [ 45.6%]
     0.50%  [k] asm_exc_page_fault
"""

REPORT_CHILDREN = """# Samples: 15K of event 'task-clock:uH'
#
# Children      Self  Symbol
     99.98%     0.00%  [.] _start
            |
            ---_start
               __libc_start_main
     27.05%     0.79%  [.] c_analyze_semantics_core
            |--12.34%--c_parse_expression_type_query
"""

ANNOTATE = """ Percent |	Source code & Disassembly of ide for cycles:u (657 samples, percent: local period)
---------------------------------------------------------------------------------------------
         : 5      00000000007fe9d0 <ir_validate_canonical_function>:
    0.00 :   7fe9d0: push   %rbp
   12.50 :   7fe9e4: mov    0x20(%rdx),%r12d
         : 24     if (!table || id.value >= table->count)
   30.10 :   7fe9e8: cmp    %r12d,%eax
    1.00 │   7fe9f1: jbe    7fec8a <ir_validate_canonical_function+0x2ba>
"""

FAULT_SCRIPT = """ 1919.424591: PERF_RECORD_MMAP2 11663/11663: [0x5581eae12000(0x30cc000) @ 0 fe:00 2810449 4188519128]: r--p /repo/build/Release/ide
 1919.424616: PERF_RECORD_MMAP2 11663/11663: [0x7ef8b9600000(0x1000000) @ 0x7ef8b9600000 00:00 0 0]: rw-p //anon
 1919.424662:     5581eae12040 _start     7f0989a50540 _start
 1919.424764:     7ef8b9600010 [unknown]     5581eae13000 c_ir_append_instruction+0x12
 1919.524764:     7ef8b9601000     5581eae13000 c_lex_dispatch
 1919.624764:     10000 [unknown]     5581eae13000 c_lex_dispatch
"""


class ParserTests(unittest.TestCase):
    def test_stat_csv_values_and_na(self):
        rows = lab.parse_stat_csv(STAT_CSV)
        values = lab.stat_values(rows)
        self.assertEqual(values["duration_time"], 1810466010)
        self.assertEqual(values["instructions"], 16934571004)
        self.assertIsNone(values["branch-misses"])
        self.assertIsNone(values["L1-dcache-load-misses"])
        self.assertEqual(values["page-faults"], 258081)
        self.assertAlmostEqual(lab.multiplex_percent(rows), 100.0)

    def test_stat_csv_repeat_variance(self):
        rows = lab.parse_stat_csv(REPEAT_CSV)
        self.assertAlmostEqual(rows[0]["variance"], 19.25)
        self.assertAlmostEqual(rows[0]["runtime"], 1704923)
        self.assertAlmostEqual(rows[0]["pct_running"], 100.0)
        self.assertIsNone(lab.stat_values(rows)["cycles"])

    def test_topdown_metrics_and_multiplex(self):
        rows = lab.parse_stat_csv(TOPDOWN_CSV)
        metrics = lab.stat_metrics(rows)
        self.assertIn(("backend_bound", 38.2, "%"), metrics)
        self.assertEqual(lab.dominant_category(metrics), ("backend_bound", 38.2))
        self.assertAlmostEqual(lab.multiplex_percent(rows), 83.33)

    def test_normalize_event(self):
        self.assertEqual(lab.normalize_event("cycles:u"), "cycles")
        self.assertEqual(lab.normalize_event("cpu_core/cycles/u"), "cycles")
        self.assertEqual(lab.normalize_event("L1-icache-load-misses:u"), "L1-icache-load-misses")

    def test_interval_table(self):
        intervals = lab.interval_table(lab.parse_stat_csv(INTERVAL_CSV, interval=True))
        self.assertEqual(len(intervals), 2)
        self.assertEqual(intervals[0]["t0"], 0.0)
        self.assertAlmostEqual(intervals[1]["t0"], 0.020098855)
        self.assertIsNone(intervals[1]["values"]["cycles"])
        self.assertEqual(intervals[1]["values"]["instructions"], 80000000)

    def test_metric_files(self):
        source = lab.parse_key_values(SOURCE_METRICS)
        self.assertEqual(source["lexed.translated_bytes"], 33560496)
        metrics = lab.parse_cc_metrics(CC_METRICS)
        self.assertEqual(metrics["header"]["peak_rss_bytes"], 877584384)
        self.assertEqual(metrics["header"]["error"], "driver.none")
        record = lab.measured_input(metrics)
        self.assertEqual(record["analysis_ns"], 30000000)
        self.assertIsNone(lab.measured_input(lab.parse_cc_metrics(CC_METRICS.replace("measured=1", "measured=0"))))
        self.assertIsNone(lab.measured_input(lab.parse_cc_metrics("")))

    def test_report_self_strips_ipc_columns(self):
        sections = lab.parse_report(REPORT_SELF)
        self.assertEqual(list(sections), ["cycles:u"])
        self.assertEqual([entry[2] for entry in sections["cycles:u"]],
                         ["ir_validate_canonical_function", "c_ir_ssa_finish", "asm_exc_page_fault"])
        self.assertEqual(sections["cycles:u"][0][0], 4.32)

    def test_report_children(self):
        entries = lab.report_entries(REPORT_CHILDREN)
        self.assertEqual(entries, [(99.98, 0.0, "_start"), (27.05, 0.79, "c_analyze_semantics_core")])

    def test_annotate(self):
        rows = lab.parse_annotate(ANNOTATE)
        self.assertEqual(rows[0][:2], (30.10, "7fe9e8"))
        self.assertEqual([row[1] for row in rows], ["7fe9e8", "7fe9e4", "7fe9f1"])

    def test_fault_script(self):
        mmaps, faults = lab.parse_fault_script(FAULT_SCRIPT)
        self.assertEqual(len(mmaps), 2)
        self.assertEqual(mmaps[1][2:], ("//anon", "rw-p"))
        self.assertEqual(len(faults), 4)
        self.assertEqual(faults[1][1:], (0x7ef8b9600010, 0x5581eae13000, "c_ir_append_instruction"))
        self.assertEqual(faults[2][1], 0x7ef8b9601000)
        self.assertTrue(lab.classify_address(faults[1][1], mmaps).startswith("anon rw-p"))
        self.assertTrue(lab.classify_address(0x10000, mmaps).startswith("not an mmap"))
        summary = lab.fault_summary(FAULT_SCRIPT, buckets=4)
        self.assertEqual(summary["count"], 4)
        self.assertEqual(sum(summary["timeline"]), 4)
        self.assertEqual(summary["regions"][0][1], 2)

    def test_discover_groups_round_robin(self):
        text = "\nMetric Groups:\n\nPipelineL2\nPipelineL1\nbranch_prediction\ndecoder\nl2_cache\nl3_cache\ntlb\ndata_fabric\nmemory_controller\npower\n"
        groups = lab.discover_groups(text)
        self.assertEqual(groups[:2], ["PipelineL1", "PipelineL2"])
        self.assertNotIn("data_fabric", groups)
        self.assertNotIn("power", groups)
        self.assertLess(groups.index("tlb"), groups.index("l3_cache"))
        self.assertEqual(len(lab.discover_groups(text, limit=3)), 3)
        self.assertEqual(lab.discover_groups("Backend: description\n  continued line\nTopdownL1: x\n"), ["TopdownL1"])


class StatisticsTests(unittest.TestCase):
    def test_summarize(self):
        summary = lab.summarize([5, 1, None, 3, 2, 4])
        self.assertEqual((summary["n"], summary["min"], summary["median"], summary["max"]), (5, 1, 3, 5))
        self.assertAlmostEqual(summary["p10"], 1.4)
        self.assertEqual(summary["mad"], 1)
        self.assertIsNone(lab.summarize([None]))

    def test_runs_since_minimum_and_tenths(self):
        self.assertEqual(lab.runs_since_minimum([3, 2, 4, 2, 5]), 3)
        self.assertIsNone(lab.runs_since_minimum([]))
        tenths = lab.tenth_medians(list(range(20)))
        self.assertEqual(len(tenths), 10)
        self.assertEqual(tenths[0], 0.5)
        self.assertEqual(lab.tenth_medians([1, 2, 3]), [2])
        self.assertIsNone(lab.ratio(1, 0))


class TimelineTests(unittest.TestCase):
    def test_phase_spans_and_attribution(self):
        metrics = lab.parse_cc_metrics(CC_METRICS)
        spans = lab.phase_spans(metrics, 0.11)
        names = [span[0] for span in spans]
        self.assertEqual(names, ["startup", "preprocess", "parse", "analysis", "ir", "codegen", "object", "link+finish", "exit/slack"])
        self.assertAlmostEqual(spans[1][1], 0.01)
        self.assertAlmostEqual(spans[3][2], 0.07)
        self.assertAlmostEqual(lab.alignment_error(metrics, 0.11), 0.01)
        intervals = [{"t0": 0.0, "t1": 0.02, "values": {"instructions": 100.0, "cycles": None}, "pct": {}},
                     {"t0": 0.02, "t1": 0.04, "values": {"instructions": 200.0, "cycles": None}, "pct": {}}]
        attributed = lab.attribute_intervals(intervals, [("a", 0.0, 0.01), ("b", 0.01, 0.04)])
        self.assertAlmostEqual(attributed["a"]["instructions"], 50.0)
        self.assertAlmostEqual(attributed["b"]["instructions"], 250.0)
        self.assertIsNone(attributed["a"]["cycles"])
        self.assertEqual(lab.phase_spans(lab.parse_cc_metrics(""), 1.0), [])

    def test_svg_is_well_formed(self):
        intervals = lab.interval_table(lab.parse_stat_csv(INTERVAL_CSV, interval=True))
        svg = lab.timeline_svg(intervals, lab.phase_spans(lab.parse_cc_metrics(CC_METRICS), 0.04))
        ElementTree.fromstring(svg)
        ElementTree.fromstring(lab.timeline_svg(intervals, []))


FAKE_IDE = r'''#!/usr/bin/env python3
import sys
args = sys.argv[1:]
if args[:1] == ["bench"]:
    print("BENCH_C_FRONTEND path=tests/basic_c_operations.c iterations=30 bytes=21042 min_ns=3359552 median_ns=3983033")
    sys.exit(0)
out = args[args.index("-o") + 1]
for arg in args:
    if arg.startswith("-fsource-metrics="):
        open(arg.split("=", 1)[1], "w").write(SOURCE)
    if arg.startswith("-fmetrics-out="):
        if MODE == "old":
            sys.stderr.write("cc: error: unknown argument\n")
            sys.exit(1)
        open(arg.split("=", 1)[1], "w").write(METRICS)
open(out, "wb").write(b"\x7fELF same bytes")
'''

FAKE_PERF = r'''#!/usr/bin/env python3
import os, subprocess, sys
args = sys.argv[1:]
def child():
    return subprocess.call(args[args.index("--") + 1:]) if "--" in args else 0
def option(name):
    return args[args.index(name) + 1] if name in args else None
command = args[0]
if command == "--version":
    print("perf version 6.99.fake")
elif command == "list":
    print("\nMetric Groups:\n\nPipelineL1\nPipelineL2\ntlb\ndata_fabric\n")
elif command == "stat":
    status = child()
    if "-M" in args:
        text = TOPDOWN if option("-M") != "tlb" else ""
        if "-I" in args:
            text = ""
        if option("-M") == "tlb":
            sys.stderr.write("Cannot find PMU `ls_l1_d_tlb_miss.all'\n")
            sys.exit(1)
    elif "-I" in args:
        text = INTERVAL
    else:
        text = STAT
    open(option("-o"), "w").write(text)
    sys.exit(status)
elif command == "record":
    event = option("-e")
    if event == "dTLB-load-misses:u":
        sys.stderr.write("The dTLB-load-misses event is not supported.\n")
        sys.exit(255)
    status = child()
    open(option("-o"), "w").write(event)
    sys.exit(status)
elif command == "report":
    event = open(option("-i")).read()
    if "--children" in args:
        print(CHILDREN.replace("task-clock:uH", event))
    else:
        print(SELF.replace("cycles:u", event))
elif command == "mem":
    if args[1] == "record":
        status = child()
        open(option("-o"), "w").write("ibs_op//")
        sys.exit(status)
    print("# Samples: 1K of event 'ibs_op//'\n    40.00%  L1 hit  [.] c_lex_dispatch")
elif command == "annotate":
    print(ANNOTATE)
elif command == "script":
    print(FAULTS)
'''


FAKE_SUDO = r'''#!/usr/bin/env python3
import os, sys
args = sys.argv[1:]
if args == ["-v"]:
    sys.exit(0)
while args[:1] in (["-u"], ["-g"]):
    args = args[2:]
if args[:1] == ["--"]:
    args = args[1:]
os.execvp(args[0], args)
'''


def write_script(path, body, constants):
    header = "".join("%s = %r\n" % item for item in constants.items())
    lines = body.split("\n", 1)
    with open(path, "w") as handle:
        handle.write(lines[0] + "\n" + header + lines[1])
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)


@unittest.skipIf(os.name != "posix", "fake executables need a POSIX shebang")
class FlowTests(unittest.TestCase):
    def fakes(self, mode):
        root = tempfile.mkdtemp(prefix="uarch-lab-test-")
        self.addCleanup(shutil.rmtree, root, True)
        ide, perf = os.path.join(root, "ide"), os.path.join(root, "perf")
        write_script(ide, FAKE_IDE, {"SOURCE": SOURCE_METRICS, "METRICS": CC_METRICS, "MODE": mode})
        write_script(perf, FAKE_PERF, {"TOPDOWN": TOPDOWN_CSV, "INTERVAL": INTERVAL_CSV, "STAT": STAT_CSV, "SELF": REPORT_SELF,
                                       "CHILDREN": REPORT_CHILDREN, "ANNOTATE": ANNOTATE, "FAULTS": FAULT_SCRIPT})
        write_script(os.path.join(root, "sudo"), FAKE_SUDO, {})
        return root, ide, perf

    def run_lab(self, mode):
        root, ide, perf = self.fakes(mode)
        output = os.path.join(root, "out")
        stdout = sys.stdout
        try:
            sys.stdout = open(os.devnull, "w")
            lab.main(["run", "--ide", ide, "--repo-root", root, "--cpu", "-1", "--output", output, "--runs", "3", "--perf", perf])
        finally:
            sys.stdout.close()
            sys.stdout = stdout
        with open(os.path.join(output, "lab.json")) as handle:
            meta = json.load(handle)
        with open(os.path.join(output, "report.md")) as handle:
            return meta, handle.read(), output

    def test_full_flow_with_phase_metrics(self):
        meta, report, output = self.run_lab("new")
        for step in ("env", "timed", "topdown", "timeline", "sampling", "micro"):
            self.assertEqual(meta["steps"][step]["status"], "ok", (step, meta["steps"][step]))
        self.assertEqual(meta["steps"]["ibs"]["status"], "skipped")
        self.assertTrue(meta["capabilities"]["metrics_out_measured"])
        self.assertIn("3 runs (0 failed), 3 byte-identical", report)
        self.assertIn("Slowest phase (timed median): **analysis**", report)
        self.assertIn("Dominant top-down level-1 category: **backend_bound**", report)
        self.assertIn("Hot by cycles: `ir_validate_canonical_function` 4.3%", report)
        self.assertIn("**dTLB misses**: NA (The dTLB-load-misses event is not supported.)", report)
        self.assertIn("IPC 1.948", report)
        self.assertIn("| analysis |", report)
        self.assertIn("`7fe9e8`".strip("`"), report)
        self.assertIn("Cannot find PMU", report)
        self.assertTrue(os.path.exists(os.path.join(output, "timeline", "timeline.html")))
        self.assertIn("BENCH_C_FRONTEND", report)
        rerendered = lab.render_report(output)
        self.assertEqual(rerendered, report)

    def test_ibs_step(self):
        root, ide, perf = self.fakes("new")
        real_isdir = os.path.isdir
        saved_path = os.environ["PATH"]
        os.environ["PATH"] = root + os.pathsep + saved_path
        try:
            with mock.patch.object(lab.os.path, "isdir", lambda path: path.startswith("/sys/bus/event_source/") or real_isdir(path)):
                instance = lab.Lab(os.path.join(root, "out"), perf, None, ide, root, (), True)
                note = lab.step_ibs(instance)
        finally:
            os.environ["PATH"] = saved_path
        self.assertEqual(note, "ibs_op exit=0; ibs_fetch exit=0; perf mem exit=0")
        text = "\n".join(lab.render_ibs(os.path.join(root, "out"), []))
        self.assertIn("| 4.32% | `ir_validate_canonical_function` |", text)
        self.assertIn("L1 hit", text)

    def test_binary_without_metrics_out_degrades(self):
        meta, report, _ = self.run_lab("old")
        self.assertFalse(meta["capabilities"]["metrics_out"])
        self.assertEqual(meta["steps"]["timed"]["status"], "ok")
        self.assertIn("Phase breakdown: NA -- the binary does not accept -fmetrics-out=", report)
        self.assertIn("Phase attribution: NA", report)


if __name__ == "__main__":
    unittest.main()
