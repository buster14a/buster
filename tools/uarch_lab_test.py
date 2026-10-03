#!/usr/bin/env python3
"""Offline regressions for tools/uarch_lab.py: every parser on recorded perf
and compiler output shapes, the statistics, phase attribution, and one full
`run` against a fake `perf` and a fake `ide` so each step's code path executes
without a PMU; for `compare`, the sign-test and bootstrap statistics on
synthetic paired data, ABBA order, verdict logic, golden summary.json keys and
end-to-end runs against a second fake `ide-b`.
Run: python3 -B tools/uarch_lab_test.py
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


TOPDOWN_JSON = "\n".join(json.dumps(record) for record in [
    {"counter-value": "1100000000.000000", "unit": "", "event": "ls_not_halted_cyc", "variance": 0.12, "event-runtime": 1700000000, "pcnt-running": 83.33},
    {"counter-value": "2000000000.000000", "unit": "", "event": "ex_ret_ops", "variance": 0.10, "event-runtime": 1700000000, "pcnt-running": 83.33},
    {"metric-value": "24.500000", "metric-unit": "%  frontend_bound"},
    {"metric-value": "7.100000", "metric-unit": "%  bad_speculation"},
    {"metric-value": "38.200000", "metric-unit": "%  backend_bound"},
    {"metric-value": "30.200000", "metric-unit": "%  retiring"}]) + "\n"

# Finding 1 (Zen 5, perf 7.2.4): duration_time reads 0 beside any other event.
DURATION_ZERO_CSV = """0,,duration_time,1,100.00,,
1519.19,msec,task-clock:u,1519190989,100.00,,
8019305103,,cycles:u,1519190989,100.00,,
22287659913,,instructions:u,1519190989,100.00,,
"""

# Finding 5: an uncore group on a host without the amd_umc PMU.
UMC_ERROR = """event syntax error: '{umc_mem_clk/metric-id=umc_mem_clk/,umc_cas_cmd.rd/metric-id=umc_cas_cmd.rd/}:W'
                        \\___ Bad event or PMU

Unable to find PMU or event on a PMU of 'umc_mem_clk'
"""

# Finding 6: perf's -x, metric column keeps one decimal.
BRANCH_CSV = """4120569627,,ex_ret_brn:u,0.00%,1509882571,100.00,0.0,per_branch  branch_misprediction_rate
73900351,,ex_ret_brn_misp:u,0.23%,1509882571,100.00,,
562200,,ls_l1_d_tlb_miss.all:u,1.01%,758573160,50.00,0.0,per_1k_instr  l1_dtlb_misses_pti
8847212,,bp_l1_tlb_miss_l2_tlb_hit:u,2.29%,756066504,49.00,0.4,per_1k_instr  l1_itlb_misses_pti
"""

# perf 6.8.12 `perf stat -j` lines (this container).
STAT_JSON = """{"counter-value" : "1.324303", "unit" : "msec", "event" : "task-clock", "event-runtime" : 1324303, "pcnt-running" : 100.00, "metric-value" : "0.012795", "metric-unit" : "CPUs utilized"}
{"counter-value" : "<not counted>", "unit" : "", "event" : "cycles:u", "event-runtime" : 0, "pcnt-running" : 100.00, "metric-value" : "0.000000", "metric-unit" : ""}
{"counter-value" : "1810008.000000", "unit" : "", "event" : "msr/tsc/", "event-runtime" : 934212, "pcnt-running" : 100.00, "metric-value" : "0.000000", "metric-unit" : "(null)"}
{"counter-value" : "73900351.000000", "unit" : "", "event" : "ex_ret_brn_misp:u", "variance" : 0.23, "event-runtime" : 1509882571, "pcnt-running" : 100.00, "metric-value" : "0.017934", "metric-unit" : "per_branch  branch_misprediction_rate"}
"""

# Finding 4: `perf report --sort pid,dso` of the system-wide IBS capture; the
# shares are the review's, the tids illustrate perf's `tid:comm` column.
TASKS_REPORT = """# Samples: 1M of event 'ibs_op//'
#
# Overhead      Pid:Command      Shared Object
# ........  ...................  .................
#
    92.34%     4242:main_thread      ide
     5.57%     4242:main_thread      [kernel.kallsyms]
     1.48%     4242:main_thread      libc.so.6
     0.36%      311:kworker/u64:18-  [kernel.kallsyms]
     0.08%     4243:ide              [kernel.kallsyms]
"""

IBS_OP_WORKLOAD = """# Samples: 1M of event 'ibs_op//'
#
# Overhead  Symbol
#
     4.58%  [.] ir_validate_canonical_function
     3.24%  [.] machine_fast_placement_build_prepassed
     2.82%  [.] c_type_parse_machine_run
"""

IBS_OP_OLD_COMM = """# Samples: 1M of event 'ibs_op//'
#
# Overhead  Symbol
#
"""

MEM_LEVELS = """# Samples: 13K of event 'ibs_op//'
#
# Overhead       Samples  Memory access
# ........  ............  ........................
#
    45.97%            13  RAM hit
    24.33%          3249  N/A
    16.15%          2157  L1 hit
     9.17%            63  L2 hit
     2.73%             5  L3 hit
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
        self.assertEqual(mmaps[1][3:], ("//anon", "rw-p"))
        self.assertEqual(mmaps[1][0], 1919.424616)
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
import sys, time
args = sys.argv[1:]
time.sleep(globals().get("DELAY", 0.0))
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
open(out, "wb").write(globals().get("OUTPUT", b"\x7fELF same bytes"))
'''

FAKE_PERF = r'''#!/usr/bin/env python3
import os, subprocess, sys
args = sys.argv[1:]
def child():
    return subprocess.call(args[args.index("--") + 1:]) if "--" in args else 0
def option(name):
    return args[args.index(name) + 1] if name in args else None
def sort():
    for index, arg in enumerate(args):
        if arg.startswith("--sort="):
            return arg.split("=", 1)[1]
        if arg == "--sort":
            return args[index + 1]
    return None
# The compare fixtures name the candidate fake `ide-b`: fewer instructions,
# a shifted cycles profile.
CANDIDATE = "ide-b" in " ".join(args)
SPLIT = {"frontend_bound": 22.25, "bad_speculation": 7.5, "backend_bound": 40.125, "retiring": 30.125}
command = args[0]
if command == "--version":
    print("perf version 6.99.fake")
elif command == "list":
    print("\nMetric Groups:\n\nPipelineL1\nPipelineL2\ntlb\ndata_fabric\nmemory_controller\n")
elif command == "stat":
    status = child()
    text = STAT
    if "-M" in args:
        selector = option("-M")
        if selector == "memory_controller":
            sys.stderr.write(UMC_ERROR)
            sys.exit(129)
        if selector in SPLIT:
            text = ('{"counter-value" : "1100000000.000000", "unit" : "", "event" : "ls_not_halted_cyc", "pcnt-running" : 100.00}\n'
                    '{"metric-value" : "%f", "metric-unit" : "%%  %s"}\n' % (SPLIT[selector], selector))
        elif "-I" in args:
            text = ""
        else:
            text = TOPDOWN_JSON if "-j" in args else TOPDOWN
    elif "-I" in args:
        text = INTERVAL
    elif CANDIDATE:
        text = text.replace("16934571004", "16595879584")
    if option("-o"):
        open(option("-o"), "w").write(text)
    sys.exit(status)
elif command == "record":
    event = option("-e")
    if event == "dTLB-load-misses:u":
        sys.stderr.write("The dTLB-load-misses event is not supported.\n")
        sys.exit(255)
    status = child()
    open(option("-o"), "w").write(event + ("|b" if CANDIDATE else ""))
    sys.exit(status)
elif command == "report":
    event, _, variant = open(option("-i")).read().partition("|")
    if variant:
        SELF = SELF.replace("4.32%  [.] ir_validate", "2.32%  [.] ir_validate")
    if sort() == "pid,dso":
        print(TASKS)
    elif "--tid" in args:
        print(IBS_WORKLOAD)
    elif "--children" in args:
        print(CHILDREN.replace("task-clock:uH", event))
    else:
        print(SELF.replace("cycles:u", event))
elif command == "mem":
    if args[1] == "record":
        status = child()
        open(option("-o"), "w").write("ibs_op//")
        sys.exit(status)
    if sort() == "mem":
        print(MEM_LEVELS)
    else:
        print("# Samples: 1K of event 'ibs_op//'\n    40.00%  2157  L1 hit  [.] c_lex_dispatch")
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


class Fakes:
    def fakes(self, mode, stat=STAT_CSV):
        root = tempfile.mkdtemp(prefix="uarch-lab-test-")
        self.addCleanup(shutil.rmtree, root, True)
        ide, perf = os.path.join(root, "ide"), os.path.join(root, "perf")
        write_script(ide, FAKE_IDE, {"SOURCE": SOURCE_METRICS, "METRICS": CC_METRICS, "MODE": mode})
        write_script(perf, FAKE_PERF, {"TOPDOWN": TOPDOWN_CSV, "TOPDOWN_JSON": TOPDOWN_JSON, "INTERVAL": INTERVAL_CSV, "STAT": stat,
                                       "SELF": REPORT_SELF, "CHILDREN": REPORT_CHILDREN, "ANNOTATE": ANNOTATE, "FAULTS": FAULT_SCRIPT,
                                       "UMC_ERROR": UMC_ERROR, "TASKS": TASKS_REPORT, "IBS_WORKLOAD": IBS_OP_WORKLOAD, "MEM_LEVELS": MEM_LEVELS})
        write_script(os.path.join(root, "sudo"), FAKE_SUDO, {})
        return root, ide, perf

    def run_lab(self, mode, stat=STAT_CSV, runs=("--runs", "3")):
        root, ide, perf = self.fakes(mode, stat)
        output = os.path.join(root, "out")
        stdout = sys.stdout
        try:
            sys.stdout = open(os.devnull, "w")
            lab.main(["run", "--ide", ide, "--repo-root", root, "--cpu", "-1", "--output", output, "--perf", perf] + list(runs))
        finally:
            sys.stdout.close()
            sys.stdout = stdout
        with open(os.path.join(output, "lab.json")) as handle:
            meta = json.load(handle)
        with open(os.path.join(output, "report.md")) as handle:
            return meta, handle.read(), output



@unittest.skipIf(os.name != "posix", "fake executables need a POSIX shebang")
class FlowTests(Fakes, unittest.TestCase):
    def test_full_flow_with_phase_metrics(self):
        meta, report, output = self.run_lab("new")
        for step in ("env", "timed", "topdown", "timeline", "sampling", "micro"):
            self.assertEqual(meta["steps"][step]["status"], "ok", (step, meta["steps"][step]))
        self.assertEqual(meta["steps"]["ibs"]["status"], "skipped")
        self.assertTrue(meta["capabilities"]["metrics_out_measured"])
        self.assertIn("3 runs (0 failed), 3 byte-identical", report)
        self.assertIn("Slowest phase (timed median): **analysis**", report)
        self.assertIn("Hot by cycles: `ir_validate_canonical_function` 4.3%", report)
        self.assertIn("**dTLB misses**: NA (The dTLB-load-misses event is not supported.)", report)
        self.assertIn("IPC 1.948", report)
        self.assertIn("| analysis |", report)
        self.assertIn("`7fe9e8`".strip("`"), report)
        self.assertIn("**memory_controller**: unavailable -- uncore PMU absent", report)
        self.assertIn("3 measured, 1 unavailable", report)
        with open(os.path.join(output, "topdown", "groups.json")) as handle:
            groups = {entry["group"]: entry for entry in json.load(handle)}
        self.assertEqual(len(groups["PipelineL1"]["split"]), 4)
        self.assertIn("Dominant top-down level-1 category: **backend_bound** 40.1%", report)
        self.assertIn("alone, running 100.0%; group value 38.2", report)
        self.assertIn("harness span", report)
        self.assertTrue(os.path.exists(os.path.join(output, "timeline", "timeline.html")))
        self.assertIn("BENCH_C_FRONTEND", report)
        rerendered = lab.render_report(output)
        self.assertEqual(rerendered, report)
        with open(os.path.join(output, "summary.json")) as handle:
            summary = json.load(handle)
        self.assertEqual(summary["schema"], lab.RUN_SCHEMA)
        self.assertEqual(set(summary), RUN_SUMMARY_KEYS)
        self.assertEqual(set(summary["timed"]["metrics"]), set(COMPARE_METRIC_NAMES))
        self.assertEqual(summary["timed"]["metrics"]["instructions"]["median"], 16934571004)
        self.assertEqual(set(summary["timed"]["metrics"]["wall"]), set(lab.SUMMARY_KEYS) | {"unit", "label"})
        self.assertEqual(summary["phases"]["analysis"]["median_ms"], 30.0)
        self.assertEqual(summary["dominant_topdown_category"], {"category": "backend_bound", "percent": 40.125})
        self.assertEqual(summary["hot_symbols"]["cycles"][0], {"symbol": "ir_validate_canonical_function", "share": 4.32})
        self.assertEqual(summary["steps"]["timed"]["status"], "ok")
        self.assertEqual(summary["work"]["translated_bytes"], 33560496)

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
        self.assertTrue(note.startswith("ibs_op exit=0; ibs_fetch exit=0; perf mem exit=0; ibs_op: tid 4242"), note)
        problems = []
        text = "\n".join(lab.render_ibs(os.path.join(root, "out"), [], problems))
        self.assertEqual(problems, [])
        self.assertIn("| 4.58% | `ir_validate_canonical_function` |", text)
        self.assertIn("threads 4242 (ide,main_thread)", text)
        self.assertIn("| RAM hit | 45.97% | 13 |", text)
        with open(os.path.join(root, "out", "ibs", "ibs_op.filter.json")) as handle:
            self.assertEqual(json.load(handle)["options"], ["--tid", "4242", "--comms", "ide,main_thread"])

    def test_binary_without_metrics_out_degrades(self):
        meta, report, _ = self.run_lab("old")
        self.assertFalse(meta["capabilities"]["metrics_out"])
        self.assertEqual(meta["steps"]["timed"]["status"], "ok")
        self.assertIn("Phase breakdown: NA -- the binary does not accept -fmetrics-out=", report)
        self.assertIn("Phase attribution: NA", report)


def write_files(root, files):
    for name, text in files.items():
        path = os.path.join(root, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as handle:
            handle.write(text)


TIMED_COMMAND = ("taskset -c 2 perf stat -x, -o /home/u/lab-20261002T222008/timed/run-%04d.csv -e duration_time,task-clock,cycles:u -- "
                 "/b/ide cc -g src/buster/apps/ide/ide.c -fmetrics-out=/home/u/lab-20261002T222008/timed/run-%04d.ccmetrics -o /x/out.exe exit=0 %s\n")


class FindingTests(Fakes, unittest.TestCase):
    """The six findings of the first Zen 5 run (PR #2422 review) plus the
    multiplexing, run-count and dso items, on the excerpts it quoted."""

    def directory(self, files):
        root = tempfile.mkdtemp(prefix="uarch-lab-finding-")
        self.addCleanup(shutil.rmtree, root, True)
        write_files(root, files)
        return root

    def timed_dir(self, csv=DURATION_ZERO_CSV, spans=("1.602s",), metrics=True, source=None):
        files = {"timed/runs.json": json.dumps([{"run": index + 1, "exit": 0, "identical": True} for index in range(len(spans) or 1)]),
                 "commands.log": "".join(TIMED_COMMAND % (index + 1, index + 1, span) for index, span in enumerate(spans))}
        for index in range(len(spans) or 1):
            files["timed/run-%04d.csv" % (index + 1)] = csv
            if metrics:
                files["timed/run-%04d.ccmetrics" % (index + 1)] = CC_METRICS.replace("wall_ns=100000000", "wall_ns=1537100000")
        if source is not None:
            files["timed/source.metrics"] = source
        return self.directory(files)

    # 1. duration_time is not a wall time; spans come from commands.log.
    def test_duration_time_zero_is_not_wall_time(self):
        self.assertNotIn("duration_time", lab.TIMED_EVENTS)
        self.assertEqual(lab.stat_values(lab.parse_stat_csv(DURATION_ZERO_CSV))["duration_time"], 0)
        self.assertEqual(lab.command_spans(TIMED_COMMAND % (7, 7, "1.602s")), {7: 1.602})
        directory = self.timed_dir(spans=("1.602s", "1.551s", "1.549s"))
        problems = []
        text = "\n".join(lab.render_timed(directory, [], problems))
        self.assertEqual(problems, [])
        self.assertIn("primary: **harness span** (spans from commands.log)", text)
        self.assertIn("| harness span (taskset + perf + compile, monotonic, runs.json/commands.log) | 3 | 1.5490 | ", text)
        self.assertIn("| 1.5510 |", text)
        self.assertIn("| compiler wall_ns (-fmetrics-out, the compiler's own clock) | 3 | 1.5371 |", text)
        self.assertIn("| task-clock (CPU time of the process, perf) | 3 | 1.5192 |", text)
        self.assertIn("duration_time: 3 of 3 runs read 0 or NA", text)
        self.assertIn("`0,,duration_time,1,100.00,,`", text)
        self.assertIn("instructions:u median 22,287,659,913, spread (max-min)/median 0.0%", text)

    def test_metric_dividing_by_zero_duration_is_na(self):
        csv = ("0,,duration_time,1,100.00,,\n"
               "1200000,,bp_l1_tlb_miss_l2_tlb_hit:u,1519190989,100.00,0.0,per_sec  lpm_itlb_l2_reqs\n"
               "22287659913,,instructions:u,1519190989,100.00,,\n")
        directory = self.directory({"topdown/groups.json": json.dumps([{"group": "tlb", "exit": 0}]), "topdown/tlb.csv": csv})
        problems = []
        text = "\n".join(lab.render_topdown(directory, [], problems))
        self.assertIn("| lpm_itlb_l2_reqs | NA (unreliable: divides by duration_time, which perf read as 0) |", text)
        self.assertNotIn("0.000", text)

    # 2. Fail closed: no positive wall time degrades the step, quoting the CSV.
    def test_missing_wall_time_degrades_with_raw_line(self):
        directory = self.timed_dir(spans=(), metrics=False)
        write_files(directory, {"commands.log": "", "lab.json": json.dumps({"config": {}, "steps": {"timed": {"status": "ok", "note": "1 runs"}}})})
        problems = []
        lab.render_timed(directory, [], problems)
        self.assertTrue(any("no positive wall time" in problem and "`0,,duration_time,1,100.00,,`" in problem for problem in problems), problems)
        report = lab.render_report(directory)
        self.assertIn("| timed | degraded |", report)
        self.assertIn("Step status: **degraded**", report)

    @unittest.skipIf(os.name != "posix", "fake executables need a POSIX shebang")
    def test_zero_task_clock_degrades_the_run(self):
        meta, report, _ = self.run_lab("new", stat=STAT_CSV.replace("1795.31,msec,task-clock", "0,msec,task-clock"))
        self.assertEqual(meta["steps"]["timed"]["status"], "degraded")
        self.assertIn("`0,msec,task-clock,1795310712,100.00,0.992,CPUs utilized`", meta["steps"]["timed"]["problems"][0])
        self.assertIn("| timed | degraded |", report)

    # 3. Division in rendering never drops the section.
    def test_zero_span_and_bad_value_keep_the_section(self):
        directory = self.timed_dir(spans=("0.000s",), metrics=False, source=SOURCE_METRICS)
        problems = []
        text = "\n".join(lab.render_timed(directory, [], problems))
        self.assertIn("no positive wall time", problems[0])
        self.assertIn("at the wall minimum NA ns/byte, NA MB/s", text)
        self.assertIn("instructions:u median 22,287,659,913", text)
        directory = self.timed_dir(source="lexed.translated_bytes=abc\n")
        problems = []
        text = "\n".join(lab.render_timed(directory, [], problems))
        self.assertIn("- NA (line failed: TypeError", text)
        self.assertIn("instructions:u median 22,287,659,913", text)

    # 4. IBS keeps the workload's threads, not the exec name.
    def test_workload_filter_from_tasks(self):
        rows = lab.parse_task_report(TASKS_REPORT)
        self.assertEqual(rows[0], (92.34, 4242, "main_thread", "ide"))
        self.assertEqual(rows[3], (0.36, 311, "kworker/u64:18-", "[kernel.kallsyms]"))
        selection = lab.workload_filter(rows, "ide")
        self.assertEqual(selection["options"], ["--tid", "4242", "--comms", "ide,main_thread"])
        fallback = lab.workload_filter(rows, "cc1")
        self.assertEqual(fallback["options"], ["--comms", "cc1,main_thread"])
        self.assertEqual(lab.parse_mem_levels(MEM_LEVELS)[0], (45.97, 13, "RAM hit"))
        self.assertEqual(lab.parse_mem_levels(MEM_LEVELS)[1], (24.33, 3249, "N/A"))

    def old_ibs_dir(self):
        return self.directory({"lab.json": json.dumps({"config": {"command": "/b/ide cc x.c"}, "steps": {"ibs": {"status": "ok", "note": ""}}}),
                               "ibs/ibs_op.data": "ibs_op//", "ibs/ibs_op.self.txt": IBS_OP_OLD_COMM,
                               "ibs/ibs_op.report.log": "$ perf report --comm ide\nexit=0\n",
                               "ibs/mem.data": "ibs_op//", "ibs/mem.report.txt": "# Samples: 13K\n"})

    def test_old_ibs_dir_without_perf_is_degraded(self):
        report = lab.render_report(self.old_ibs_dir())
        self.assertIn("| ibs | degraded |", report)
        self.assertIn("pre-fix exec-name report", report)
        self.assertIn("0 report rows while the capture has samples", report)

    @unittest.skipIf(os.name != "posix", "fake executables need a POSIX shebang")
    def test_old_ibs_dir_is_rederived_by_pid(self):
        _, _, perf = self.fakes("new")
        directory = self.old_ibs_dir()
        report = lab.render_report(directory, perf)
        self.assertIn("| ibs | ok |", report)
        self.assertIn("| 4.58% | `ir_validate_canonical_function` |", report)
        self.assertIn("| 3.24% | `machine_fast_placement_build_prepassed` |", report)
        self.assertIn("| 2.82% | `c_type_parse_machine_run` |", report)
        self.assertIn("| RAM hit | 45.97% | 13 | 0.2% |", report)
        self.assertIn("| N/A | 24.33% | 3,249 |", report)
        self.assertIn("threads 4242 (ide,main_thread)", report)
        self.assertEqual(lab.render_report(directory), report)

    # 5. Uncore groups without their PMU are unavailable, not failed.
    def test_unavailable_uncore_group(self):
        directory = self.directory({
            "topdown/groups.json": json.dumps([{"group": "PipelineL1", "exit": 0}, {"group": "memory_controller", "exit": 129}]),
            "topdown/PipelineL1.csv": TOPDOWN_CSV.replace("83.33", "100.00"),
            "topdown/memory_controller.log": "$ perf stat -M memory_controller\nexit=129\n--- stdout\n\n--- stderr\n" + UMC_ERROR})
        problems = []
        text = "\n".join(lab.render_topdown(directory, [], problems))
        self.assertEqual(problems, [])
        self.assertIn("1 measured, 1 unavailable (uncore PMU absent; not failures), 0 failed", text)
        self.assertIn("Unable to find PMU or event on a PMU of 'umc_mem_clk'", text)
        self.assertIn("not a per-process number", text)

    # 6. Rounded metric columns are flagged and recomputed from raw counts.
    def test_rounded_metrics_recomputed(self):
        rows = lab.parse_stat_csv(BRANCH_CSV)
        self.assertEqual(rows[0]["metric_name"], "branch_misprediction_rate")
        self.assertEqual(rows[0]["metric_unit"], "per_branch")
        self.assertEqual(lab.metric_text(rows[0]["metric_value"], rows[0]["metric_decimals"]), "< 0.05 (perf printed 0.0; rounded to 0.1)")
        self.assertEqual(lab.metric_text(0.4, 1), "0.4 (rounded to 0.1)")
        self.assertEqual(lab.metric_text(0.0, 6), "0 (to 6 decimals)")
        value, formula = lab.recompute_metric(rows[0], lab.stat_values(rows), None)
        self.assertAlmostEqual(value, 73900351 / 4120569627)
        # A group run cannot assign events to metrics: no partial recompute.
        self.assertIsNone(lab.recompute_metric(rows[2], lab.stat_values(rows), (22287659913, "instructions:u (timed median)")))
        # A bad_speculation-style metric with "mispredict" in its name is not the branch rate.
        self.assertIsNone(lab.recompute_metric(dict(rows[0], metric_unit="ops", metric_name="bad_speculation_from_mispredicts"),
                                               lab.stat_values(rows), None))
        directory = self.timed_dir()
        write_files(directory, {"topdown/groups.json": json.dumps([{"group": "branch_prediction", "exit": 0}]),
                                "topdown/branch_prediction.csv": BRANCH_CSV})
        problems = []
        text = "\n".join(lab.render_topdown(directory, [], problems))
        self.assertIn("| branch_misprediction_rate | < 0.05 (perf printed 0.0; rounded to 0.1) per_branch | 0.01793 (1.79%) = ex_ret_brn_misp / ex_ret_brn |", text)
        self.assertIn("| l1_dtlb_misses_pti | < 0.05 (perf printed 0.0; rounded to 0.1) per_1k_instr | - |", text)
        self.assertIn("branch misprediction rate 1.79% (ex_ret_brn_misp / ex_ret_brn), branch MPKI 3.316", text)
        self.assertNotIn("0.000 ", text)

    def test_stat_json(self):
        rows = lab.parse_stat(STAT_JSON)
        values = lab.stat_values(rows)
        self.assertAlmostEqual(values["task-clock"], 1.324303)
        self.assertIsNone(values["cycles"])
        self.assertEqual([row["metric_name"] for row in lab.metric_rows(rows)], ["CPUs utilized", "branch_misprediction_rate"])
        self.assertEqual(lab.metric_text(rows[3]["metric_value"], rows[3]["metric_decimals"]), "0.01793")
        self.assertEqual(lab.parse_stat(TOPDOWN_JSON)[2]["metric_name"], "frontend_bound")
        self.assertAlmostEqual(lab.multiplex_percent(lab.parse_stat(TOPDOWN_JSON)), 83.33)

    # 7. A multiplexed group without per-metric values is flagged.
    def test_multiplexed_group_flagged(self):
        directory = self.directory({"topdown/groups.json": json.dumps([{"group": "PipelineL1", "exit": 0}]), "topdown/PipelineL1.csv": TOPDOWN_CSV})
        findings, problems = [], []
        text = "\n".join(lab.render_topdown(directory, findings, problems))
        self.assertIn("multiplexed: 0 of 4 metrics re-measured alone", text)
        self.assertIn("**multiplexed group value** (counters 83.3%), fallback", text)
        self.assertIn("multiplexed group, see the per-metric values", findings[0])

    # 8. The run count is planned once from the pilot runs' wall time.
    def test_choose_run_count(self):
        count, reason = lab.choose_run_count(1.55, 30.0, 15.0, 100)
        self.assertEqual(count, 3 + int((900 - 30 - 155) / 1.55))
        self.assertIn("1.550 s per run", reason)
        self.assertEqual(lab.choose_run_count(1.55, 30.0, 1.0, 100)[0], lab.MIN_TIMED_RUNS)
        self.assertEqual(lab.estimate_other_compiles(["a", "b"], ["sampling"], False), 12 + 3 + 3)

    @unittest.skipIf(os.name != "posix", "fake executables need a POSIX shebang")
    def test_target_minutes_flow(self):
        meta, report, output = self.run_lab("new", runs=("--target-minutes", "0.001", "--skip", "topdown", "sampling", "timeline", "micro"))
        self.assertEqual(meta["config"]["runs"], lab.MIN_TIMED_RUNS)
        with open(os.path.join(output, "timed", "runs.json")) as handle:
            self.assertEqual(len(json.load(handle)), lab.MIN_TIMED_RUNS)
        self.assertIn("Run count: --target-minutes 0.001:", report)

    def test_unresolved_address_named_by_dso(self):
        text = "# Samples: 9 of event 'page-faults:u'\n    12.00%  ide  [.] 0x000000000018f5ce\n     3.00%  libc.so.6  [.] memset\n"
        self.assertEqual([entry[2] for entry in lab.report_entries(text)], ["ide 0x18f5ce", "memset"])



# LAB2 review: a metric measured alone is the sum of its non-instruction
# events, whichever event perf printed the metric beside (raw perf 7.2.4).
SPLIT_ITLB_JSON = "\n".join(json.dumps(record) for record in [
    {"counter-value": "4896.000000", "unit": "", "event": "bp_l1_tlb_miss_l2_tlb_miss.all:u", "event-runtime": 1512561037,
     "pcnt-running": 100.00, "metric-value": "1.017065", "metric-unit": "per_1k_instr  l1_itlb_misses_pti"},
    {"counter-value": "22287208434.000000", "unit": "", "event": "instructions:u", "event-runtime": 1512561037, "pcnt-running": 100.00},
    {"counter-value": "22662652.000000", "unit": "", "event": "bp_l1_tlb_miss_l2_tlb_hit:u", "event-runtime": 1512561037, "pcnt-running": 100.00}])
SPLIT_CCX_JSON = "\n".join(json.dumps(record) for record in [
    {"counter-value": "22287208493.000000", "unit": "", "event": "instructions:u", "event-runtime": 1512561037, "pcnt-running": 100.00,
     "metric-value": "0.548865", "metric-unit": "per_1k_instr  l1_demand_data_cache_fills_from_same_ccx_pti"},
    {"counter-value": "12232670.000000", "unit": "", "event": "ls_dmnd_fills_from_sys.local_ccx:u", "event-runtime": 1512561037, "pcnt-running": 100.00}])
# LAB1/LAB2 review: an address reused by later file mappings.
REUSED_FAULT_SCRIPT = """ 100.000000: PERF_RECORD_MMAP2 1/1: [0x7f0000000000(0x8000) @ 0 fe:00 1 1]: r--p /repo/src/base.h
 100.001000:     7f0000000010 [unknown]     5581eae13000 c_lex_dispatch
 100.002000: PERF_RECORD_MMAP2 1/1: [0x7f0000000000(0x8000) @ 0 fe:00 2 2]: r--p /repo/src/driver_diagnostic.c
 100.003000:     7f0000000020 [unknown]     5581eae13000 c_lex_dispatch
 101.000000: PERF_RECORD_MMAP2 1/1: [0x7f0000000000(0x136000) @ 0 fe:00 3 3]: r--p /usr/lib/libm.so.6
 101.001000:     7f0000000030 [unknown]     5581eae13000 link_step
"""


class Lab2ReviewTests(unittest.TestCase):
    def recompute(self, text):
        rows = lab.parse_stat_json(text)
        metric = [row for row in lab.metric_rows(rows)][0]
        values = lab.stat_values(rows)
        return lab.recompute_metric(metric, values, lab.instruction_count(values), alone=True)

    def test_split_metric_sums_every_non_instruction_event(self):
        value, formula = self.recompute(SPLIT_ITLB_JSON)
        self.assertAlmostEqual(value, (4896 + 22662652) / 22287208434 * 1000, places=6)
        self.assertAlmostEqual(value, 1.0171, places=4)
        self.assertIn("bp_l1_tlb_miss_l2_tlb_hit + bp_l1_tlb_miss_l2_tlb_miss.all", formula)

    def test_metric_printed_beside_instructions(self):
        value, formula = self.recompute(SPLIT_CCX_JSON)
        self.assertAlmostEqual(value, 0.548865, places=5)
        self.assertNotIn("instructions /", formula)

    def test_fault_resolves_against_mapping_live_at_fault_time(self):
        mmaps, faults = lab.parse_fault_script(REUSED_FAULT_SCRIPT)
        self.assertEqual([lab.classify_address(f[1], mmaps, f[0]) for f in faults],
                         [lab.TRANSIENT_FILES, lab.TRANSIENT_FILES, lab.classify_address(faults[2][1], mmaps)])
        self.assertTrue(lab.classify_address(faults[2][1], mmaps, faults[2][0]).startswith("/usr/lib/libm.so.6"))
        summary = lab.fault_summary(REUSED_FAULT_SCRIPT, buckets=2)
        self.assertEqual(dict(summary["regions"])[lab.TRANSIENT_FILES], 2)
        self.assertNotIn("driver_diagnostic", " ".join(label for label, _ in summary["regions"]))


# Golden keys of the machine-readable summaries; a change here is a schema
# change and needs a new schema id (RUN_SCHEMA / COMPARE_SCHEMA).
RUN_SUMMARY_KEYS = {"schema", "directory", "command", "cpu", "ide", "host", "capabilities", "steps", "timed", "phases", "work",
                    "topdown", "dominant_topdown_category", "hot_symbols", "findings"}
COMPARE_SUMMARY_KEYS = {"schema", "directory", "command", "repo_root", "cpu", "host", "baseline", "candidate", "outputs_identical",
                        "plan", "method", "verdict", "metrics", "phases", "checks", "profile", "steps", "warnings"}
COMPARE_METRIC_KEYS = {"unit", "direction", "label", "n", "a_median", "b_median", "a_min", "b_min", "a_mad", "b_mad", "delta", "ratio",
                       "ci_low", "ci_high", "ci_coverage", "geomean_ratio", "bootstrap_ci_low", "bootstrap_ci_high",
                       "ratio_of_medians", "min_ratio", "change_percent", "outcome"}
VARIANT_KEYS = {"path", "sha256", "size_bytes", "runs", "failed", "identical_runs", "deterministic", "metrics_out", "source_metrics"}
VERDICT_KEYS = {"metric", "outcome", "ratio", "ci_low", "ci_high", "ci_coverage", "change_percent", "bound_percent", "n",
                "explanation", "text"}
COMPARE_METRIC_NAMES = ["wall", "task_clock", "compiler_wall", "instructions", "cycles", "ipc", "branch_misses", "branch_mpki",
                        "page_faults", "minor_faults", "major_faults"]


def paired_sample(count, ratio, seed, noise=0.002):
    """Synthetic paired wall times: B = A x ratio with independent noise."""
    generator = lab.random.Random(seed)
    pairs = []
    for _ in range(count):
        base = 1.55 * (1.0 + generator.gauss(0.0, noise))
        pairs.append((base * (1.0 + generator.gauss(0.0, noise)), base * ratio * (1.0 + generator.gauss(0.0, noise))))
    return pairs


class CompareStatisticsTests(unittest.TestCase):
    def test_sign_test_ranks_match_binomial_tables(self):
        self.assertEqual(lab.sign_test_rank(5), (0, None))
        self.assertEqual(lab.sign_test_rank(6), (1, 1 - 2 / 64))
        self.assertEqual(lab.sign_test_rank(10)[0], 2)
        self.assertAlmostEqual(lab.sign_test_rank(10)[1], 1 - 22 / 1024)
        self.assertEqual(lab.sign_test_rank(20)[0], 6)
        self.assertEqual(lab.sign_test_rank(100)[0], 40)
        self.assertGreaterEqual(lab.sign_test_rank(1000)[1], 0.95)
        self.assertEqual(lab.median_ci(list(range(10, 0, -1))), (2, 9, lab.sign_test_rank(10)[1]))
        self.assertEqual(lab.median_ci([1, 2, 3]), (None, None, None))

    def test_ci_brackets_a_known_ratio(self):
        result = lab.compare_series(paired_sample(40, 0.99, 7), "s", "lower", 1, time_metric=True)
        self.assertEqual(result["n"], 40)
        self.assertLess(result["ci_low"], 0.99)
        self.assertGreater(result["ci_high"], 0.99)
        self.assertLess(result["ci_high"], 1.0)
        self.assertEqual(result["outcome"], "faster")
        self.assertAlmostEqual(result["ratio"], 0.99, places=2)
        self.assertLess(result["bootstrap_ci_low"], result["geomean_ratio"])
        self.assertGreater(result["bootstrap_ci_high"], result["geomean_ratio"])
        self.assertEqual(set(result) | {"label"}, COMPARE_METRIC_KEYS)

    def test_ci_coverage_on_synthetic_data(self):
        covered = 0
        for trial in range(300):
            low, high, _ = lab.median_ci([b / a for a, b in paired_sample(20, 1.0, 1000 + trial)])
            covered += low <= 1.0 <= high
        self.assertGreaterEqual(covered / 300, 0.93)

    def test_seeded_bootstrap_is_deterministic(self):
        ratios = [b / a for a, b in paired_sample(30, 1.01, 3)]
        self.assertEqual(lab.bootstrap_geomean_ci(ratios, 5), lab.bootstrap_geomean_ci(ratios, 5))
        self.assertNotEqual(lab.bootstrap_geomean_ci(ratios, 5), lab.bootstrap_geomean_ci(ratios, 6))
        self.assertEqual(lab.bootstrap_geomean_ci([1.0], 5), (None, None))

    def test_deterministic_counts_and_zero_values(self):
        exact = lab.compare_series([(22287659805, 21841906609)] * 8, "count", "lower", 1)
        self.assertAlmostEqual(exact["ratio"], 0.98, places=6)
        self.assertEqual((exact["ci_low"], exact["ci_high"]), (exact["ratio"], exact["ratio"]))
        self.assertEqual(exact["outcome"], "lower")
        zero = lab.compare_series([(0, 0)] * 8, "count", "lower", 1)
        self.assertEqual((zero["outcome"], zero["ratio"], zero["delta"]), ("no ratio (a zero value)", None, 0))
        self.assertEqual(lab.compare_series([(None, 1.0)], "s", "lower", 1)["outcome"], "no data")

    def test_verdict_logic(self):
        self.assertEqual(lab.classify(0.98, 1.01, True), "no detectable difference")
        self.assertEqual(lab.classify(0.97, 0.99, True), "faster")
        self.assertEqual(lab.classify(1.01, 1.02, True), "slower")
        self.assertEqual(lab.classify(1.01, 1.02), "higher")
        self.assertEqual(lab.classify(None, None, True), "inconclusive")
        metrics = {name: lab.compare_series(paired_sample(30, 1.0, 11), "s", "lower", 1, time_metric=True) for name in COMPARE_METRIC_NAMES}
        verdict = lab.compare_verdict(metrics, None)
        self.assertEqual(verdict["outcome"], "no detectable difference")
        self.assertIn("includes 1.0", verdict["text"])
        self.assertEqual(set(verdict), VERDICT_KEYS)
        metrics["wall"] = lab.compare_series(paired_sample(4, 0.9, 1), "s", "lower", 1, time_metric=True)
        self.assertEqual(lab.compare_verdict(metrics, None)["outcome"], "inconclusive")
        metrics["wall"] = lab.compare_series(paired_sample(30, 0.95, 1), "s", "lower", 1, time_metric=True)
        self.assertTrue(lab.compare_verdict(metrics, None)["text"].startswith("Candidate is FASTER: wall time B/A 0.9499 (-5.01%), 95% CI [0.9481, 0.9516] over 30 pairs."))

    def test_proxy_alone_is_not_a_win(self):
        metrics = {name: lab.compare_series(paired_sample(30, 1.0, 11), "s", "lower", 1, time_metric=True) for name in COMPARE_METRIC_NAMES}
        metrics["instructions"] = lab.compare_series([(100.0, 97.0)] * 30, "count", "lower", 1)
        variants = {role: dict.fromkeys(VARIANT_KEYS, 0) | {"deterministic": True, "metrics_out": True} for role in ("baseline", "candidate")}
        warnings = lab.compare_warnings(variants, True, metrics, {}, {"config": {"cpu": 2}, "steps": {}})
        self.assertEqual(warnings, ["instructions changed -3.00% but wall time shows no detectable difference: a proxy is not a win"])

    def test_abba_schedule_and_pair_count(self):
        self.assertEqual(lab.abba_schedule(4), [(1, "a"), (1, "b"), (2, "b"), (2, "a"), (3, "a"), (3, "b"), (4, "b"), (4, "a")])
        count, reason = lab.choose_pair_count(3.2, 1.6, 20.0, 15.0, 0)
        self.assertEqual(count % 2, 0)
        self.assertEqual(count, 2 + int((900 - 20) / 3.2) + (2 + int((900 - 20) / 3.2)) % 2)
        self.assertIn("3.200 s per pair", reason)
        self.assertEqual(lab.choose_pair_count(3.2, 1.6, 20.0, 0.01, 0)[0], lab.MIN_PAIRS)
        self.assertEqual(lab.compare_profile_cost(["g1", "g2"], ["topdown", "sampling"]),
                         2 * (2 * lab.TOPDOWN_COMPILES_PER_GROUP + len(lab.SAMPLE_EVENTS) + 1))

    def test_order_effect_and_drift_checks(self):
        def pairs(values):
            return [{"pair": index + 1, "order": lab.abba_order(index + 1), "metrics_a": {"wall": a}, "metrics_b": {"wall": b}}
                    for index, (a, b) in enumerate(values)]
        clean = lab.compare_checks(pairs(paired_sample(40, 1.0, 21)))
        self.assertFalse(clean["order_effect"]["flag"])
        self.assertFalse(clean["drift"]["flag"])
        self.assertEqual(len(clean["drift"]["tenth_medians_ratio"]), 10)
        # The second run of each pair 2% faster: AB ratios ~0.98, BA ~1.02.
        positional = [(a, b * (0.98 if lab.abba_order(index + 1) == "AB" else 1.02)) for index, (a, b) in enumerate(paired_sample(40, 1.0, 22))]
        checks = lab.compare_checks(pairs(positional))
        self.assertTrue(checks["order_effect"]["flag"])
        self.assertAlmostEqual(checks["order_effect"]["second_position_factor"], 0.98, places=2)
        drifting = [(a, b * (1.0 + 0.002 * index)) for index, (a, b) in enumerate(paired_sample(40, 1.0, 23))]
        self.assertTrue(lab.compare_checks(pairs(drifting))["drift"]["flag"])
        self.assertFalse(lab.compare_checks(pairs(paired_sample(6, 1.0, 1)))["drift"]["checked"])

    def test_symbol_movers(self):
        movers = lab.symbol_movers(REPORT_SELF, REPORT_SELF.replace("4.32%  [.] ir_validate", "2.32%  [.] ir_validate"))
        self.assertEqual(movers["a_event_count"], 5074350792)
        top = movers["movers"][0]
        self.assertEqual((top["symbol"], round(top["delta_share"], 6)), ("ir_validate_canonical_function", -2.0))
        self.assertAlmostEqual(top["delta_estimate"], -0.02 * 5074350792)
        self.assertEqual(movers["movers"][1]["delta_share"], 0.0)


@unittest.skipIf(os.name != "posix", "fake executables need a POSIX shebang")
class CompareFlowTests(Fakes, unittest.TestCase):
    def compare(self, arguments, candidate=None):
        root, ide, perf = self.fakes("new")
        other = os.path.join(root, "ide-b")
        write_script(other, FAKE_IDE, dict({"SOURCE": SOURCE_METRICS, "METRICS": CC_METRICS, "MODE": "new"}, **(candidate or {})))
        output = os.path.join(root, "cmp")
        stdout = sys.stdout
        try:
            sys.stdout = open(os.devnull, "w")
            lab.main(["compare", "--baseline", ide, "--candidate", other, "--repo-root", root, "--cpu", "-1", "--output", output,
                      "--perf", perf] + arguments)
        finally:
            sys.stdout.close()
            sys.stdout = stdout
        with open(os.path.join(output, "summary.json")) as handle:
            summary = json.load(handle)
        with open(os.path.join(output, "report.md")) as handle:
            return summary, handle.read(), output

    def test_slower_candidate_end_to_end(self):
        summary, report, output = self.compare(
            ["--pairs", "6", "--profile-steps", "topdown,sampling"],
            {"DELAY": 0.25, "OUTPUT": b"\x7fELF other bytes", "METRICS": CC_METRICS.replace("analysis_ns=30000000", "analysis_ns=25000000")})
        self.assertEqual(summary["schema"], lab.COMPARE_SCHEMA)
        self.assertEqual(set(summary), COMPARE_SUMMARY_KEYS)
        self.assertEqual(set(summary["metrics"]), set(COMPARE_METRIC_NAMES))
        for row in summary["metrics"].values():
            self.assertEqual(set(row), COMPARE_METRIC_KEYS)
        self.assertEqual(set(summary["baseline"]), VARIANT_KEYS)
        self.assertEqual(set(summary["verdict"]), VERDICT_KEYS)
        self.assertEqual(summary["plan"]["pairs"], 6)
        self.assertEqual(summary["plan"]["complete_pairs"], 6)
        self.assertEqual(summary["plan"]["seed"], lab.DEFAULT_SEED)
        self.assertEqual(summary["verdict"]["outcome"], "slower", summary["verdict"])
        self.assertTrue(summary["verdict"]["text"].startswith("Candidate is SLOWER"))
        self.assertAlmostEqual(summary["metrics"]["instructions"]["ratio"], 16595879584 / 16934571004)
        self.assertEqual(summary["metrics"]["instructions"]["outcome"], "lower")
        self.assertEqual(summary["phases"]["analysis"]["delta"], -5.0)
        self.assertEqual(summary["phases"]["read"]["outcome"], "no ratio (a zero value)")
        self.assertTrue(summary["baseline"]["deterministic"] and summary["candidate"]["deterministic"])
        self.assertFalse(summary["outputs_identical"])
        self.assertIn("baseline and candidate outputs differ (expected for a code-generation change; a pure refactor should be identical)",
                      summary["warnings"])
        self.assertEqual(summary["baseline"]["sha256"], lab.sha256_file(summary["baseline"]["path"]))
        with open(os.path.join(output, "pairs.json")) as handle:
            order = [(record["pair"], record["variant"]) for record in json.load(handle)]
        self.assertEqual(order, lab.abba_schedule(6))
        movers = summary["profile"]["sampling"]["events"]["cycles"]["movers"]
        self.assertEqual((movers[0]["symbol"], round(movers[0]["delta_share"], 6)), ("ir_validate_canonical_function", -2.0))
        self.assertEqual(summary["profile"]["topdown"]["status"], "ok")
        self.assertIn({"group": "PipelineL1", "metric": "backend_bound", "unit": "%", "a": 40.125, "b": 40.125, "delta": 0.0,
                       "relative_change_percent": 0.0, "a_source": "alone", "b_source": "alone"}, summary["profile"]["topdown"]["metrics"])
        self.assertTrue(report.startswith("# A/B compile-time comparison\n\n**Candidate is SLOWER"))
        self.assertIn("| analysis | 30.00 | 25.00 | -5.00 | 0.8333 |", report)
        self.assertIn("## Why: symbol share movers, sampling", report)
        before = open(os.path.join(output, "summary.json")).read()
        self.assertEqual(lab.render_compare(output), report)
        self.assertEqual(open(os.path.join(output, "summary.json")).read(), before)

    def test_identical_compilers_with_too_few_pairs(self):
        summary, report, _ = self.compare(["--pairs", "2"])
        self.assertEqual(summary["verdict"]["outcome"], "inconclusive")
        self.assertTrue(summary["outputs_identical"])
        self.assertEqual(summary["metrics"]["instructions"]["ratio"], 16595879584 / 16934571004)
        self.assertEqual(summary["profile"], {})
        self.assertIn("INCONCLUSIVE", report)

    def test_target_minutes_chooses_whole_blocks(self):
        summary, report, _ = self.compare(["--target-minutes", "0.001"])
        self.assertEqual(summary["plan"]["pairs"], lab.MIN_PAIRS)
        self.assertEqual(summary["plan"]["complete_pairs"], lab.MIN_PAIRS)
        self.assertIn("--target-minutes 0.001:", summary["plan"]["reason"])
        self.assertTrue(summary["checks"]["drift"]["checked"] is False or summary["checks"]["drift"]["subsets"][0]["n"] == 5)

    def test_capabilities_detected_per_binary(self):
        summary, report, output = self.compare(["--pairs", "2"], {"MODE": "old"})
        self.assertTrue(summary["baseline"]["metrics_out"])
        self.assertFalse(summary["candidate"]["metrics_out"])
        self.assertIsNone(summary["phases"])
        self.assertIn("candidate: no measured -fmetrics-out record, so no phase comparison", summary["warnings"])
        self.assertTrue(os.path.exists(os.path.join(output, "pairs", "0001-a.ccmetrics")))
        self.assertFalse(os.path.exists(os.path.join(output, "pairs", "0001-b.ccmetrics")))
        self.assertIn("NA -- a variant wrote no measured `-fmetrics-out` record.", report)

    def test_require_identical_output_stops_before_timing(self):
        with self.assertRaises(SystemExit):
            self.compare(["--pairs", "2", "--require-identical-output"], {"OUTPUT": b"different"})


if __name__ == "__main__":
    unittest.main()
