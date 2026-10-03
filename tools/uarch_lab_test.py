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
import struct
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
if globals().get("ARGV_LOG"):
    import os
    info = os.stat(sys.argv[0])
    open(ARGV_LOG, "a").write("%s %d %d\n" % (sys.argv[0], info.st_ino, info.st_nlink))
# ALLOC: touch this many bytes, so the peak RSS is known to exceed it.
hold = b"\x01" * globals().get("ALLOC", 0)
delay = globals().get("DELAY", 0.0)
if globals().get("SPEED_BY_EXE"):
    # A fake stage-1 compiler (a copied Python interpreter running this file
    # as `cc`): its speed is the trailing byte of the interpreter it runs in.
    with open("/proc/self/exe", "rb") as handle:
        handle.seek(-1, 2)
        delay = SPEED_BY_EXE[handle.read(1)]
time.sleep(delay)
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
output = globals().get("OUTPUT", b"\x7fELF same bytes")
if globals().get("OUTPUT_FILE"):
    # The stage-1 "executable": a real ELF (the Python interpreter) plus a pad.
    output = open(OUTPUT_FILE, "rb").read() + OUTPUT_PAD
open(out, "wb").write(output)
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
        # The default fake allocates nothing: its RSS is not above the floor.
        self.assertIn("- peak RSS (wait4 ru_maxrss of taskset/perf and the compiler", report)
        self.assertIn("3 of 3 runs NA", report)
        self.assertIn("- output code sections (elf): NA bytes", report)
        with open(os.path.join(output, "timed", "runs.json")) as handle:
            self.assertTrue(all(run["maxrss_bytes"] > 0 and run["harness_rss_bytes"] > 0 for run in json.load(handle)))
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

    def test_ibs_step_with_fresh_copies(self):
        root, ide, perf = self.fakes("new")
        real_isdir = os.path.isdir
        saved_path = os.environ["PATH"]
        os.environ["PATH"] = root + os.pathsep + saved_path
        output = os.path.join(root, "out")
        try:
            with mock.patch.object(lab.os.path, "isdir", lambda path: path.startswith("/sys/bus/event_source/") or real_isdir(path)):
                note = lab.step_ibs(lab.Lab(output, perf, None, ide, root, (), True, True))
        finally:
            os.environ["PATH"] = saved_path
        self.assertTrue(note.startswith("ibs_op exit=0; ibs_fetch exit=0; perf mem exit=0; ibs_op: tid 4242"), note)
        with open(os.path.join(output, "commands.log")) as handle:
            records = [line for line in handle if " record " in line]
        self.assertEqual(len(records), 3)
        self.assertTrue(all("--no-buildid-cache" in line and os.path.join(output, "instances") in line for line in records), records)
        self.assertEqual(len({line.split(" -- ")[-1] for line in records}), 3)
        self.assertFalse(os.path.exists(os.path.join(output, "instances")))

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


# Golden keys of the machine-readable summaries.  A removed key or a change
# of meaning needs a new schema id (RUN_SCHEMA / COMPARE_SCHEMA /
# RETIREMENT_SCHEMA); an added key is listed here and documented.
RUN_SUMMARY_KEYS = {"schema", "directory", "command", "cpu", "ide", "host", "capabilities", "steps", "timed", "phases", "work",
                    "topdown", "dominant_topdown_category", "hot_symbols", "findings"}
COMPARE_SUMMARY_KEYS = {"schema", "directory", "command", "repo_root", "cpu", "host", "baseline", "candidate", "outputs_identical",
                        "code_bytes", "plan", "method", "verdict", "metrics", "phases", "checks", "profile", "steps", "warnings"}
CODE_BYTES_KEYS = {"a_value", "b_value", "ratio", "a_format", "b_format", "a_file_bytes", "b_file_bytes", "a_sections", "b_sections", "note"}
RETIREMENT_SUMMARY_KEYS = {"schema", "directory", "decision", "contract", "verdict", "baseline", "candidate", "stage1", "repo_root", "cpu",
                           "host", "plan", "limits", "cells", "aggregates", "external_checks", "warnings"}
RETIREMENT_VERDICT_KEYS = {"outcome", "text", "failed", "inconclusive", "missing_cells"}
RETIREMENT_BINARY_KEYS = {"path", "sha256", "size_bytes", "revision", "label"}
RETIREMENT_PLAN_KEYS = {"modes", "required_cells", "pairs", "target_minutes_per_cell", "estimated_minutes", "seed", "warmups",
                        "profile_steps", "total_s"}
RETIREMENT_CELL_KEYS = {"cell", "kind", "extra_args", "status", "note", "directory", "summary", "summary_schema", "baseline", "candidate",
                        "complete_pairs", "outputs_identical", "checks", "diagnostics", "outcome"}
RETIREMENT_CHECK_KEYS = {"label", "kind", "ratio", "ci_low", "ci_high", "ci_coverage", "n", "a_value", "b_value", "limit", "outcome", "reason"}
COMPILER_CHECKS = {"compiler_wall_time", "peak_rss", "code_bytes", "runs_succeeded", "deterministic", "measurement_complete", "measurement_stable"}
RUNTIME_CHECKS = {"generated_runtime", "generated_compilers_agree", "runs_succeeded", "deterministic", "measurement_complete", "measurement_stable"}
COMPARE_METRIC_KEYS = {"unit", "direction", "label", "n", "a_median", "b_median", "a_min", "b_min", "a_mad", "b_mad", "delta", "ratio",
                       "ci_low", "ci_high", "ci_coverage", "geomean_ratio", "bootstrap_ci_low", "bootstrap_ci_high",
                       "ratio_of_medians", "min_ratio", "change_percent", "outcome", "note"}
VARIANT_KEYS = {"path", "sha256", "size_bytes", "runs", "failed", "identical_runs", "deterministic", "metrics_out", "source_metrics"}
VERDICT_KEYS = {"metric", "outcome", "ratio", "ci_low", "ci_high", "ci_coverage", "change_percent", "bound_percent",
                "min_effect_percent", "n", "explanation", "text"}
PLAN_KEYS = {"pairs", "reason", "order", "fresh_copy", "seed", "confidence", "bootstrap_resamples", "complete_pairs"}
MOVERS_KEYS = {"a_event_count", "b_event_count", "a_samples", "b_samples", "reliable", "note", "movers"}
MOVER_ROW_KEYS = {"symbol", "a_share", "b_share", "delta_share", "noise_pp", "beyond_noise", "exceeds_bound", "a_estimate",
                  "b_estimate", "delta_estimate"}
COMPARE_METRIC_NAMES = ["wall", "task_clock", "compiler_wall", "instructions", "cycles", "ipc", "branch_misses", "branch_mpki",
                        "page_faults", "minor_faults", "major_faults", "peak_rss"]


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
        self.assertTrue(lab.compare_verdict(metrics, None)["text"].startswith(
            "Candidate is FASTER: wall time B/A 0.9499 (-5.01%), 95% CI [0.9481, 0.9516] over 30 pairs, beyond the 0.5% practical floor."))

    def test_proxy_alone_is_not_a_win(self):
        metrics = {name: lab.compare_series(paired_sample(30, 1.0, 11), "s", "lower", 1, time_metric=True) for name in COMPARE_METRIC_NAMES}
        metrics["instructions"] = lab.compare_series([(100.0, 97.0)] * 30, "count", "lower", 1)
        variants = {role: dict.fromkeys(VARIANT_KEYS, 0) | {"deterministic": True, "metrics_out": True} for role in ("baseline", "candidate")}
        warnings = lab.compare_warnings(variants, True, metrics, {}, {"config": {"cpu": 2, "fresh_copy": True}, "steps": {}})
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
    def compare(self, arguments, candidate=None, baseline=None):
        root, ide, perf = self.fakes("new")
        if baseline:
            write_script(ide, FAKE_IDE, dict({"SOURCE": SOURCE_METRICS, "METRICS": CC_METRICS, "MODE": "new"}, **baseline))
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
        self.assertEqual(set(summary["code_bytes"]), CODE_BYTES_KEYS)
        self.assertIsNone(summary["code_bytes"]["ratio"])
        self.assertIn("baseline: elf unknown ELF class or data encoding", summary["code_bytes"]["note"])
        self.assertEqual(set(summary["metrics"]), set(COMPARE_METRIC_NAMES))
        for row in summary["metrics"].values():
            self.assertEqual(set(row), COMPARE_METRIC_KEYS)
        self.assertEqual(set(summary["baseline"]), VARIANT_KEYS)
        self.assertEqual(set(summary["verdict"]), VERDICT_KEYS)
        self.assertEqual(set(summary["plan"]), PLAN_KEYS)
        self.assertTrue(summary["plan"]["fresh_copy"])
        self.assertEqual(summary["verdict"]["min_effect_percent"], lab.DEFAULT_MIN_EFFECT)
        self.assertEqual(set(summary["profile"]["sampling"]["events"]["cycles"]), MOVERS_KEYS)
        for row in summary["profile"]["sampling"]["events"]["cycles"]["movers"]:
            self.assertEqual(set(row), MOVER_ROW_KEYS)
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
        before = lab.read_text(os.path.join(output, "summary.json"))
        self.assertEqual(lab.render_compare(output), report)
        self.assertEqual(lab.read_text(os.path.join(output, "summary.json")), before)

    @unittest.skipIf(not sys.platform.startswith("linux"), "wait4 ru_maxrss and an ELF interpreter")
    def test_peak_rss_and_code_bytes(self):
        common = {"OUTPUT_FILE": sys.executable}
        summary, report, output = self.compare(["--pairs", "6"], dict(common, OUTPUT_PAD=b"B"), dict(common, ALLOC=160 << 20, OUTPUT_PAD=b"A"))
        with open(os.path.join(output, "pairs.json")) as handle:
            records = json.load(handle)
        self.assertTrue(all(record["maxrss_bytes"] >= (160 << 20) for record in records if record["variant"] == "a"))
        self.assertTrue(all(record["wrapper_rss_bytes"] and record["harness_rss_bytes"] for record in records))
        rss = summary["metrics"]["peak_rss"]
        # B allocates nothing, so its value is not above the floor: NA, and
        # the pair has no ratio (never a win for the smaller variant).
        self.assertEqual((rss["unit"], rss["n"], rss["outcome"]), ("bytes", 0, "no data"))
        self.assertIn("candidate: peak RSS NA in 6 of 6 runs", " ".join(summary["warnings"]))
        code = lab.code_sections(sys.executable)["code_bytes"]
        self.assertEqual((summary["code_bytes"]["a_value"], summary["code_bytes"]["b_value"], summary["code_bytes"]["ratio"]), (code, code, 1.0))
        self.assertFalse(summary["outputs_identical"])
        self.assertIn("- generated code bytes (executable sections, exact): A %s, B %s, B/A 1.000000" % (lab.fmt(code), lab.fmt(code)), report)
        self.assertIn("| peak RSS (wait4 ru_maxrss) | bytes |", report)

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

    def logged_runs(self, log):
        with open(log) as handle:
            return [(path, int(inode), int(links)) for path, inode, links in (line.split() for line in handle)]

    # LAB3: each timed run and capture executes a new copy of its binary (a
    # new inode, never a link), fsync'd before the run and deleted after it.
    def test_fresh_copy_per_run(self):
        log_dir = tempfile.mkdtemp(prefix="uarch-lab-argv-")
        self.addCleanup(shutil.rmtree, log_dir, True)
        logs = {key: os.path.join(log_dir, key + ".log") for key in ("a", "b")}
        real_fsync = os.fsync
        with mock.patch.object(lab.os, "fsync", side_effect=real_fsync) as fsync:
            summary, report, output = self.compare(["--pairs", "6", "--profile-steps", "sampling"],
                                                   {"ARGV_LOG": logs["b"]}, {"ARGV_LOG": logs["a"]})
        originals = {"a": summary["baseline"]["path"], "b": summary["candidate"]["path"]}
        for key in ("a", "b"):
            runs = self.logged_runs(logs[key])
            original_inode = os.stat(originals[key]).st_ino
            fresh = [run for run in runs if run[0] != originals[key]]
            # 6 timed runs, the sampling captures but dTLB (the fake perf
            # rejects it before starting the workload) and the fault capture.
            self.assertEqual(len(fresh), 6 + len(lab.SAMPLE_EVENTS) - 1 + 1, runs)
            self.assertEqual(len({path for path, _, _ in fresh}), len(fresh))
            for path, inode, links in fresh:
                self.assertTrue(path.startswith(os.path.join(output, key, "instances") + os.sep), path)
                self.assertEqual(os.path.basename(path), os.path.basename(originals[key]))
                self.assertNotEqual(inode, original_inode)
                self.assertEqual(links, 1)
                self.assertFalse(os.path.exists(path))
            self.assertFalse(os.path.exists(os.path.join(output, key, "instances")))
        # One fsync'd copy per timed run and per capture attempt, both variants.
        self.assertEqual(fsync.call_count, 2 * (6 + len(lab.SAMPLE_EVENTS) + 1))
        self.assertTrue(summary["plan"]["fresh_copy"])
        self.assertTrue(summary["baseline"]["deterministic"] and summary["candidate"]["deterministic"])
        self.assertTrue(summary["outputs_identical"])
        self.assertFalse(any("ran in place" in warning for warning in summary["warnings"]))
        self.assertIn("binary instances: a fresh copy per timed run and capture", report)
        with open(os.path.join(output, "compare.json")) as handle:
            self.assertTrue(json.load(handle)["plan"]["fresh_copy"])

    def test_no_fresh_copy_runs_in_place(self):
        log_dir = tempfile.mkdtemp(prefix="uarch-lab-argv-")
        self.addCleanup(shutil.rmtree, log_dir, True)
        log = os.path.join(log_dir, "a.log")
        summary, report, output = self.compare(["--pairs", "6", "--no-fresh-copy"], None, {"ARGV_LOG": log})
        self.assertEqual({path for path, _, _ in self.logged_runs(log)}, {summary["baseline"]["path"]})
        self.assertFalse(summary["plan"]["fresh_copy"])
        self.assertTrue(any(warning.startswith("binaries ran in place") for warning in summary["warnings"]))
        self.assertIn("**run in place**", report)

    def test_min_effect_is_recorded(self):
        summary, report, _ = self.compare(["--pairs", "6", "--min-effect", "2.5"])
        self.assertEqual(summary["verdict"]["min_effect_percent"], 2.5)
        self.assertIn("practical floor 2.5%", report)
        with self.assertRaises(SystemExit):
            self.compare(["--pairs", "6", "--min-effect", "-1"])


@unittest.skipIf(os.name != "posix", "fake executables need a POSIX shebang")
class RunFreshCopyTests(Fakes, unittest.TestCase):
    def test_run_mode_uses_fresh_copies(self):
        root, ide, perf = self.fakes("new")
        log = os.path.join(root, "argv.log")
        write_script(ide, FAKE_IDE, {"SOURCE": SOURCE_METRICS, "METRICS": CC_METRICS, "MODE": "new", "ARGV_LOG": log})
        output = os.path.join(root, "out")
        stdout = sys.stdout
        try:
            sys.stdout = open(os.devnull, "w")
            lab.main(["run", "--ide", ide, "--repo-root", root, "--cpu", "-1", "--output", output, "--perf", perf, "--runs", "3",
                      "--skip", "topdown", "timeline", "sampling", "micro"])
        finally:
            sys.stdout.close()
            sys.stdout = stdout
        with open(log) as handle:
            paths = [line.split()[0] for line in handle]
        fresh = [path for path in paths if path != ide]
        self.assertEqual(len(fresh), 3)
        self.assertEqual(len(set(fresh)), 3)
        self.assertFalse(any(os.path.exists(path) for path in fresh))
        with open(os.path.join(output, "summary.json")) as handle:
            summary = json.load(handle)
        self.assertTrue(summary["timed"]["plan"]["fresh_copy"])
        self.assertEqual(summary["timed"]["identical"], 3)

    def test_fresh_binary_copy_is_a_new_executable_file(self):
        root = tempfile.mkdtemp(prefix="uarch-lab-copy-")
        self.addCleanup(shutil.rmtree, root, True)
        source = os.path.join(root, "ide")
        with open(source, "wb") as handle:
            handle.write(os.urandom(3 << 20))
        os.chmod(source, 0o755)
        destination = os.path.join(root, "copy")
        with mock.patch.object(lab.os, "fsync", side_effect=os.fsync) as fsync:
            lab.fresh_binary_copy(source, destination)
        self.assertEqual(fsync.call_count, 1)
        self.assertTrue(lab.filecmp.cmp(source, destination, shallow=False))
        self.assertNotEqual(os.stat(source).st_ino, os.stat(destination).st_ino)
        self.assertEqual(os.stat(destination).st_mode & 0o777, 0o755)
        with self.assertRaises(FileExistsError):
            lab.fresh_binary_copy(source, destination)


def aa_pairs(count, offset, seed, noise=0.0005):
    """Pairs whose B carries a fixed offset with small pair-to-pair noise."""
    return paired_sample(count, offset, seed, noise)


class Lab3ReviewTests(unittest.TestCase):
    def metrics(self, wall, instructions=None, cycles=None):
        metrics = {name: lab.compare_series(paired_sample(30, 1.0, 11), "s", "lower", 1, time_metric=True, floor=0.01)
                   for name in COMPARE_METRIC_NAMES}
        metrics["wall"] = wall
        metrics["instructions"] = instructions or lab.compare_series([(22287659872, 22287659808)] * 30, "count", "lower", 1)
        if cycles:
            metrics["cycles"] = cycles
        return metrics

    def test_classify_with_floor(self):
        self.assertEqual(lab.classify(0.97, 0.989, True, 0.01), "faster")
        self.assertEqual(lab.classify(0.985, 0.995, True, 0.01), "below-floor")
        self.assertEqual(lab.classify(1.005, 1.02, False, 0.01), "below-floor")
        self.assertEqual(lab.classify(1.011, 1.02, False, 0.01), "higher")
        self.assertEqual(lab.classify(0.995, 1.001, True, 0.01), "no detectable difference")
        self.assertEqual(lab.classify(0.985, 0.995, True), "faster")
        self.assertEqual(lab.metric_floor("instructions", 1.0), 0.0)
        self.assertEqual(lab.metric_floor("cycles", 1.0), 0.01)

    def test_verdict_beyond_floor_is_faster(self):
        wall = lab.compare_series(paired_sample(30, 0.95, 1), "s", "lower", 1, time_metric=True, floor=0.01)
        verdict = lab.compare_verdict(self.metrics(wall), None, 1.0)
        self.assertEqual(verdict["outcome"], "faster")
        self.assertIsNone(verdict["bound_percent"])
        self.assertEqual(verdict["min_effect_percent"], 1.0)

    def test_verdict_inside_floor_is_below_floor(self):
        # The LAB3 shape: a stable -0.5% offset, CI far narrower than the offset.
        wall = lab.compare_series(aa_pairs(190, 0.995, 5), "s", "lower", 1, time_metric=True, floor=0.01)
        self.assertLess(wall["ci_high"], 1.0)
        self.assertEqual(wall["outcome"], "below-floor")
        verdict = lab.compare_verdict(self.metrics(wall), None, 1.0)
        self.assertEqual(verdict["outcome"], "below-floor")
        self.assertIn("is below the 1% practical floor", verdict["text"])
        self.assertIn("measurement-instance effects of ~0.5%", verdict["text"])
        self.assertAlmostEqual(verdict["bound_percent"], (1.0 - wall["ci_low"]) * 100.0)
        self.assertEqual(set(verdict), VERDICT_KEYS)

    def test_verdict_ci_with_one_is_no_detectable_difference(self):
        wall = lab.compare_series(paired_sample(30, 1.0, 11), "s", "lower", 1, time_metric=True, floor=0.01)
        verdict = lab.compare_verdict(self.metrics(wall), None, 1.0)
        self.assertEqual(verdict["outcome"], "no detectable difference")
        self.assertAlmostEqual(verdict["bound_percent"], max(1 - wall["ci_low"], wall["ci_high"] - 1) * 100.0)

    def test_phase_outcomes_use_the_floor(self):
        phase = lab.compare_series([(a * 1000, b * 1000) for a, b in aa_pairs(60, 0.99, 3)], "ms", "lower", 1, time_metric=True,
                                   floor=0.005)
        self.assertEqual(phase["outcome"], "faster")
        phase = lab.compare_series([(a * 1000, b * 1000) for a, b in aa_pairs(60, 0.997, 3)], "ms", "lower", 1, time_metric=True,
                                   floor=0.005)
        self.assertEqual(phase["outcome"], "below-floor")

    def test_instance_warning(self):
        wall = lab.compare_series(aa_pairs(190, 0.995, 5), "s", "lower", 1, time_metric=True, floor=0.01)
        variants = {role: dict.fromkeys(VARIANT_KEYS, 0) | {"deterministic": True, "metrics_out": True} for role in ("baseline", "candidate")}
        meta = {"config": {"cpu": 2, "fresh_copy": True}, "steps": {}}
        warnings = lab.compare_warnings(variants, True, self.metrics(wall), {}, meta)
        self.assertEqual([warning for warning in warnings if warning.startswith("identical work, different time")],
                         ["identical work, different time: instructions B/A 1.000000 but the wall CI excludes 1.0; likely a "
                          "placement/instance effect, not a code effect (a layout-only change can do this too; confirm with an A/A run)"])
        changed = lab.compare_series([(100.0, 99.0)] * 30, "count", "lower", 1)
        warnings = lab.compare_warnings(variants, True, self.metrics(wall, instructions=changed), {}, meta)
        self.assertFalse(any(warning.startswith("identical work") for warning in warnings))
        same = lab.compare_series(paired_sample(30, 1.0, 11), "s", "lower", 1, time_metric=True, floor=0.01)
        warnings = lab.compare_warnings(variants, True, self.metrics(same), {}, meta)
        self.assertFalse(any(warning.startswith("identical work") for warning in warnings))

    def test_bimodal_count_note(self):
        series = [(2532, 2532)] * 9 + [(2332, 2332)] * 9 + [(2532, 2332)] * 2
        row = lab.compare_series(series, "count", "lower", 1, floor=0.01)
        self.assertAlmostEqual(row["ratio_of_medians"], 2332 / 2532)
        self.assertEqual(row["ratio"], 1.0)
        self.assertTrue(row["note"].startswith("bimodal counts: compare medians, not the paired ratio"), row["note"])
        self.assertIsNone(lab.compare_series([(100, 101)] * 10, "count", "lower", 1)["note"])
        self.assertIsNone(lab.compare_series(series, "ms", "lower", 1)["note"])

    def test_mover_gating(self):
        few = REPORT_SELF.replace("# Samples: 15K", "# Samples: 506")
        movers = lab.symbol_movers(few, few.replace("4.32%  [.] ir_validate", "2.32%  [.] ir_validate"))
        self.assertFalse(movers["reliable"])
        self.assertEqual(movers["movers"], [])
        self.assertTrue(movers["note"].startswith("too few samples: shares unreliable"))
        candidate = REPORT_SELF.replace("4.32%  [.] ir_validate", "3.62%  [.] ir_validate").replace("     0.50%  [k] asm_exc_page_fault\n", "")
        movers = lab.symbol_movers(REPORT_SELF, candidate)
        self.assertTrue(movers["reliable"])
        rows = {row["symbol"]: row for row in movers["movers"]}
        moved = rows["ir_validate_canonical_function"]
        self.assertTrue(moved["beyond_noise"])
        self.assertFalse(moved["exceeds_bound"])
        self.assertLess(abs(moved["delta_share"]), lab.MOVER_BOUND_FACTOR * moved["noise_pp"])
        one_na = rows["asm_exc_page_fault"]
        self.assertIsNone(one_na["noise_pp"])
        self.assertIsNone(one_na["exceeds_bound"])
        self.assertEqual(lab.mover_reading(one_na), "-")
        self.assertEqual(lab.mover_reading(moved), "within 3x bound")
        far = lab.symbol_movers(REPORT_SELF, REPORT_SELF.replace("4.32%  [.] ir_validate", "1.32%  [.] ir_validate"))
        self.assertTrue(far["movers"][0]["exceeds_bound"])
        self.assertEqual(lab.mover_reading(far["movers"][0]), "exceeds bound")

    def test_unresolved_symbols_named_alike(self):
        sampling = "# Samples: 4K of event 'dTLB-load-misses:u'\n    17.15%  [unknown]  [k] 0xffffffff85a85cd7\n"
        ibs = "# Samples: 5K of event 'ibs_op//'\n     4.71%  [k] 0xffffffff868c8a3b\n     1.00%  [.] 0x00000000000123ab\n"
        self.assertEqual([entry[2] for entry in lab.report_entries(sampling)], ["[unknown] 0xffffffff85a85cd7"])
        self.assertEqual([entry[2] for entry in lab.report_entries(ibs)], ["[unknown] 0xffffffff868c8a3b", "[unknown] 0x123ab"])



class CliTests(unittest.TestCase):
    # LAB3 (Python 3.14 validates help strings when a subparser is added): a
    # bare `%` in any help text made every subcommand fail before dispatch.
    # Rendering top-level and per-subcommand help expands every help string on
    # all supported Python versions.
    def test_every_help_string_renders(self):
        import subprocess
        script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "uarch_lab.py")
        for arguments in ([], ["run"], ["compare"], ["retirement"], ["report"]):
            result = subprocess.run([sys.executable, script] + arguments + ["--help"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, (arguments, result.stderr))



class Lab4ReviewTests(unittest.TestCase):
    # LAB4: adjacent unsymbolised addresses of one routine moved together and
    # each read "exceeds bound"; they are now one row per binary.
    def test_unresolved_addresses_collapse_per_binary(self):
        shares = {"libc.so.6 0x18f622": 3.57, "libc.so.6 0x18f628": 3.50, "[unknown] 0xffffffff85a85cd7": 1.0,
                  "c_lex_dispatch": 39.91, "ide 0x18f5ce": 0.5}
        collapsed = lab.collapse_unresolved(shares)
        self.assertAlmostEqual(collapsed["libc.so.6 (unresolved addresses)"], 7.07)
        self.assertEqual(collapsed["[unknown] (unresolved addresses)"], 1.0)
        self.assertEqual(collapsed["ide (unresolved addresses)"], 0.5)
        self.assertEqual(collapsed["c_lex_dispatch"], 39.91)

    # LAB4: an estimate that rounds to zero printed as "-0".
    def test_no_negative_zero_estimate(self):
        self.assertEqual(lab.whole_or_zero(-0.3), 0.0)
        self.assertEqual(str(lab.whole_or_zero(-0.0)), "0.0")
        self.assertEqual(lab.whole_or_zero(-80.0), -80.0)


def elf_fixture(wide=True, endian="<", sections=None, extended=False):
    """A minimal ELF (header, payload, section header table) with the given
    [(name, sh_type, sh_flags, size)] after the null section and before
    .shstrtab, built with struct."""
    sections = sections if sections is not None else [
        (".text", 1, 0x6, 100), (".init", 1, 0x6, 20), (".data", 1, 0x3, 50), (".tbss", 8, 0x6, 1000), (".rodata", 1, 0x2, 30)]
    names = b"\0" + b"".join(name.encode() + b"\0" for name, _, _, _ in sections) + b".shstrtab\0"
    header_size, entry_size = (64, 64) if wide else (52, 40)
    entry = struct.Struct(endian + ("IIQQQQIIQQ" if wide else "IIIIIIIIII"))
    payload, rows, offset, name_offset = b"", [(0, 0, 0, 0, 0, 0)], header_size, 1
    for name, kind, flags, size in sections:
        rows.append((name_offset, kind, flags, offset, size, 0))
        name_offset += len(name) + 1
        if kind != 8:
            payload += b"\xcc" * size
            offset += size
    rows.append((name_offset, 3, 0, offset, len(names), 0))
    payload += names
    shoff = header_size + len(payload)
    count = len(rows)
    if extended:  # section 0 carries the count and the string-table index
        rows[0] = (0, 0, 0, 0, count, count - 1)
    table_bytes = b"".join(entry.pack(name, kind, flags, 0, file_offset, size, link, 0, 1, 0)
                           for name, kind, flags, file_offset, size, link in rows)
    ident = b"\x7fELF" + bytes([2 if wide else 1, 1 if endian == "<" else 2, 1]) + b"\0" * 9
    header = struct.pack(endian + ("16sHHIQQQIHHHHHH" if wide else "16sHHIIIIIHHHHHH"), ident, 2, 62, 1, 0, 0, shoff, 0, header_size,
                         0, 0, entry_size, 0 if extended else count, 0xffff if extended else count - 1)
    return header + payload + table_bytes


class CodeBytesTests(unittest.TestCase):
    def write(self, data):
        handle, path = tempfile.mkstemp(prefix="uarch-lab-elf-")
        os.close(handle)
        self.addCleanup(os.remove, path)
        with open(path, "wb") as writer:
            writer.write(data)
        return path

    # Executable sections with file bytes count (.text, .init); writable data,
    # read-only data and an executable SHT_NOBITS section (.tbss) do not.
    def test_elf64_little_endian(self):
        code = lab.code_sections(self.write(elf_fixture()))
        self.assertEqual((code["format"], code["code_bytes"], code["reason"]), ("elf64", 120, None))
        self.assertEqual(code["sections"], [{"name": ".text", "size": 100}, {"name": ".init", "size": 20}])
        self.assertEqual(code["file_bytes"], len(elf_fixture()))

    def test_elf_variants(self):
        for wide, endian, extended in ((True, ">", False), (False, "<", False), (False, ">", False), (True, "<", True)):
            code = lab.code_sections(self.write(elf_fixture(wide, endian, extended=extended)))
            self.assertEqual(code["code_bytes"], 120, (wide, endian, extended))
            self.assertEqual(code["format"], "elf64" if wide else "elf32")

    def test_zero_code_payload_is_a_value(self):
        code = lab.code_sections(self.write(elf_fixture(sections=[(".data", 1, 0x3, 8)])))
        self.assertEqual((code["code_bytes"], code["sections"]), (0, []))

    def test_executable_payload_must_be_inside_file(self):
        data = bytearray(elf_fixture())
        section_table = struct.unpack_from("<Q", data, 40)[0]
        struct.pack_into("<Q", data, section_table + 64 + 24, len(data) + 1)
        code = lab.code_sections(self.write(data))
        self.assertIsNone(code["code_bytes"])
        self.assertEqual(code["reason"], "executable section outside the file")

    def test_unsupported_and_malformed_are_na(self):
        for data, fmt_name, reason in ((b"\xcf\xfa\xed\xfe" + b"\0" * 60, "mach-o", "no validated Mach-O"),
                                       (b"MZ" + b"\0" * 62, "pe", "no validated PE"), (b"#!/bin/sh\n", "unknown", "not an ELF"),
                                       (b"\x7fELF\x02\x01", "elf64", "truncated ELF header"), (b"\x7fELF", "elf", "truncated"),
                                       (elf_fixture()[:200], "elf64", "section header table")):
            code = lab.code_sections(self.write(data))
            self.assertIsNone(code["code_bytes"], data[:8])
            self.assertEqual(code["format"], fmt_name)
            self.assertIn(reason, code["reason"])
        self.assertIn("unreadable", lab.code_sections("/nonexistent/uarch-lab")["reason"])

    def test_summary_ratio_is_exact(self):
        a = self.write(elf_fixture())
        b = self.write(elf_fixture(sections=[(".text", 1, 0x6, 121)]))
        code = lab.code_bytes_summary([a, b])
        self.assertEqual((code["a_value"], code["b_value"], code["note"]), (120, 121, None))
        self.assertEqual(code["ratio"], 121 / 120)
        zero = self.write(elf_fixture(sections=[(".data", 1, 0x3, 8)]))
        self.assertEqual(lab.code_bytes_summary([a, zero])["ratio"], 0.0)
        empty = lab.code_bytes_summary([zero, a])
        self.assertIsNone(empty["ratio"])
        self.assertEqual(empty["note"], "zero baseline code payload: no ratio denominator")
        self.assertIn("candidate: pe", lab.code_bytes_summary([a, self.write(b"MZ" + b"\0" * 62)])["note"])


@unittest.skipIf(not hasattr(os, "wait4") or not sys.platform.startswith("linux"), "wait4 ru_maxrss in KiB is Linux behaviour")
class PeakRssTests(unittest.TestCase):
    def run_child(self, argv, timeout=60):
        return lab.run_measured(argv, os.getcwd(), dict(os.environ), timeout)

    def test_known_allocation(self):
        status, _, _, rss, harness = self.run_child([sys.executable, "-c", "hold = b'1' * (48 << 20); print('done')"])
        self.assertEqual(status, 0)
        self.assertGreaterEqual(rss, 48 << 20)
        self.assertLess(rss, harness + (200 << 20))

    # Record current residence; previous large allocations have been released
    # before spawn and must not discard later low-memory measurements.
    def test_harness_resident_footprint_is_recorded(self):
        before = lab.harness_resident_bytes()
        status, _, _, rss, harness = self.run_child(["true"])
        self.assertEqual(status, 0)
        self.assertGreater(harness, 0)
        self.assertAlmostEqual(harness / before, 1.0, delta=0.1)
        self.assertGreater(rss, 0)

    def test_success_without_new_output_is_rejected(self):
        root = tempfile.mkdtemp(prefix="uarch-lab-no-output-")
        self.addCleanup(shutil.rmtree, root, True)
        output = os.path.join(root, "out.exe")
        with open(output, "w") as handle:
            handle.write("previous result")
        measured = lab.Lab(root, repo_root=root)
        status, _, err = measured.run_command([sys.executable, "-c", "pass", "-o", output])
        self.assertEqual(status, 1)
        self.assertIn("did not create", err)
        self.assertFalse(os.path.exists(output))

    # The maximum over the waited tree: a shell parent that reaps a large
    # child reports the child's high-water mark (the perf-wrapper case).
    def test_reaped_descendant_counts(self):
        status, out, _, rss, _ = self.run_child(["sh", "-c", "%s -c \"hold = b'1' * (64 << 20)\"; echo ok" % sys.executable])
        self.assertEqual((status, out.strip()), (0, b"ok"))
        self.assertGreaterEqual(rss, 64 << 20)

    def test_exit_status_timeout_and_missing_binary(self):
        self.assertEqual(self.run_child([sys.executable, "-c", "import sys; sys.stderr.write('x'); sys.exit(3)"])[:3], (3, b"", b"x"))
        status, _, err, _, _ = self.run_child([sys.executable, "-c", "import time; time.sleep(30)"], timeout=0.5)
        self.assertEqual(status, 124)
        self.assertTrue(err.endswith(b"timeout\n"))
        self.assertEqual(self.run_child(["/nonexistent/uarch-lab-binary"])[0], 127)

    def test_timeout_terminates_descendants(self):
        child = "import subprocess,time; p=subprocess.Popen(['sleep','60']); print(p.pid,flush=True); time.sleep(60)"
        status, out, _, _, _ = self.run_child([sys.executable, "-c", child], timeout=0.5)
        self.assertEqual(status, 124)
        pid = int(out.strip())
        try:
            with open("/proc/%d/stat" % pid) as handle:
                state = handle.read().split()[2]
            self.assertEqual(state, "Z", "a live descendant survived the timeout")
        except (FileNotFoundError, ProcessLookupError):
            pass

    def test_rss_value_needs_a_clear_margin_over_the_wrapper(self):
        run = {"maxrss_bytes": 300 << 20, "wrapper_rss_bytes": 12 << 20, "harness_rss_bytes": 40 << 20}
        self.assertEqual(lab.rss_floor(run), 40 << 20)
        self.assertEqual(lab.rss_value(run), 300 << 20)
        self.assertIsNone(lab.rss_value(dict(run, maxrss_bytes=17 << 20, harness_rss_bytes=12 << 20)))
        self.assertIsNone(lab.rss_value(dict(run, harness_rss_bytes=250 << 20)))
        self.assertIsNone(lab.rss_value(dict(run, wrapper_rss_bytes=None)))
        self.assertIsNone(lab.rss_value(dict(run, harness_rss_bytes=None)))
        self.assertIsNone(lab.rss_value(dict(run, maxrss_bytes=None)))
        self.assertIsNone(lab.rss_value({}))


def interval_row(ratio, low, high, n=20):
    return {"ratio": ratio, "ci_low": low, "ci_high": high, "ci_coverage": 0.9586, "n": n, "a_median": 1.0, "b_median": ratio}


def cell_summary(wall=(1.0, 0.99, 1.01), rss=(1.0, 0.999, 1.001), code=(1000, 1005), deterministic=True, failed=0, identical=True):
    variant = {"path": "/x/ide", "sha256": "0" * 64, "runs": 20, "failed": failed, "deterministic": deterministic}
    return {"schema": lab.COMPARE_SCHEMA, "host": {"cpu_model": "fake"}, "warnings": [],
            "plan": {"pairs": 20, "complete_pairs": 20, "fresh_copy": True}, "steps": {"timed": "ok"}, "checks": {},
            "baseline": dict(variant, deterministic=True), "candidate": variant, "outputs_identical": identical,
            "metrics": {"wall": interval_row(*wall), "peak_rss": interval_row(*rss) if rss else lab.compare_series([], "bytes", "lower", 1)},
            "code_bytes": {"a_value": code[0], "b_value": code[1], "ratio": code[1] / code[0] if code[0] and code[1] is not None else None,
                           "note": None if code[0] and code[1] is not None else "candidate: pe no validated PE code-section parser"}}


class RetirementLimitTests(unittest.TestCase):
    def test_incomplete_or_unstable_series_cannot_pass(self):
        summary = cell_summary()
        summary["steps"]["timed"] = "failed"
        self.assertEqual(lab.cell_outcome(lab.cell_checks("compiler", summary)), "INCONCLUSIVE")
        summary = cell_summary()
        summary["plan"]["pairs"] = 100
        self.assertEqual(lab.cell_outcome(lab.cell_checks("compiler", summary)), "INCONCLUSIVE")
        summary = cell_summary()
        summary["metrics"]["peak_rss"]["n"] = 6
        self.assertEqual(lab.cell_outcome(lab.cell_checks("compiler", summary)), "INCONCLUSIVE")
        summary = cell_summary()
        summary["checks"]["drift"] = {"flag": True}
        self.assertEqual(lab.cell_outcome(lab.cell_checks("compiler", summary)), "INCONCLUSIVE")

    def test_interval_outcomes(self):
        self.assertEqual(lab.limit_outcome(0.98, 1.05, 1.05)[0], "PASS")
        self.assertEqual(lab.limit_outcome(1.0501, 1.07, 1.05)[0], "FAIL")
        self.assertEqual(lab.limit_outcome(1.04, 1.06, 1.05), ("INCONCLUSIVE", "CI [1.0400, 1.0600] crosses 1.05"))
        self.assertEqual(lab.limit_outcome(None, None, 1.05)[0], "INCONCLUSIVE")

    def test_exact_outcomes(self):
        self.assertEqual(lab.exact_outcome(1.01, 1.01)[0], "PASS")
        self.assertEqual(lab.exact_outcome(1.0101, 1.01)[0], "FAIL")
        self.assertEqual(lab.exact_outcome(None, 1.01, "candidate: pe"), ("INCONCLUSIVE", "candidate: pe"))

    def test_limits_match_the_contract(self):
        self.assertEqual(lab.RETIREMENT_LIMITS, {"compiler_wall_time": 1.05, "peak_rss": 1.05, "code_bytes": 1.01, "generated_runtime": 1.03})
        contract = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "native-retirement-performance-contract.md")
        with open(contract) as handle:
            text = handle.read()
        for row in ("| Compiler wall time |", "| Compiler peak RSS |"):
            self.assertIn("Upper simultaneous bound at most `1.05` |", next(line for line in text.splitlines() if line.startswith(row)))
        self.assertIn("Exact ratio at most `1.01` in every cell |", next(line for line in text.splitlines() if line.startswith("| Generated code bytes |")))
        self.assertIn("Upper simultaneous bound at most `1.03` |",
                      next(line for line in text.splitlines() if line.startswith("| Generated-program runtime |")))

    def test_compiler_cell_checks(self):
        checks = lab.cell_checks("compiler", cell_summary())
        self.assertEqual(set(checks), COMPILER_CHECKS)
        for check in checks.values():
            self.assertEqual(set(check), RETIREMENT_CHECK_KEYS)
        self.assertEqual(lab.cell_outcome(checks), "PASS")
        self.assertEqual(checks["code_bytes"]["ratio"], 1.005)
        self.assertEqual(lab.cell_outcome(lab.cell_checks("compiler", cell_summary(wall=(1.08, 1.06, 1.10)))), "FAIL")
        self.assertEqual(lab.cell_outcome(lab.cell_checks("compiler", cell_summary(rss=(1.03, 1.0, 1.06)))), "INCONCLUSIVE")
        self.assertEqual(lab.cell_outcome(lab.cell_checks("compiler", cell_summary(rss=None))), "INCONCLUSIVE")
        self.assertEqual(lab.cell_checks("compiler", cell_summary(code=(1000, 1011)))["code_bytes"]["outcome"], "FAIL")
        self.assertEqual(lab.cell_checks("compiler", cell_summary(code=(1000, None)))["code_bytes"]["outcome"], "INCONCLUSIVE")
        self.assertEqual(lab.cell_checks("compiler", cell_summary(deterministic=False))["deterministic"]["outcome"], "FAIL")
        self.assertEqual(lab.cell_checks("compiler", cell_summary(failed=1))["runs_succeeded"]["outcome"], "FAIL")

    def test_runtime_cell_checks(self):
        checks = lab.cell_checks("generated-runtime", cell_summary(wall=(1.01, 1.0, 1.03)))
        self.assertEqual(set(checks), RUNTIME_CHECKS)
        self.assertEqual(lab.cell_outcome(checks), "PASS")
        self.assertEqual(lab.cell_checks("generated-runtime", cell_summary(wall=(1.02, 1.01, 1.04)))["generated_runtime"]["outcome"], "INCONCLUSIVE")
        self.assertEqual(lab.cell_checks("generated-runtime", cell_summary(identical=False))["generated_compilers_agree"]["outcome"], "FAIL")

    def directory(self, summaries, statuses=None):
        root = tempfile.mkdtemp(prefix="uarch-lab-retire-")
        self.addCleanup(shutil.rmtree, root, True)
        cells = []
        for name in list(lab.RETIREMENT_MODES) + [lab.RUNTIME_CELL]:
            status = (statuses or {}).get(name, "ok" if name in summaries else "not run")
            cells.append({"cell": name, "kind": "generated-runtime" if name == lab.RUNTIME_CELL else "compiler", "extra": [],
                          "status": status, "note": "" if status == "ok" else "stopped"})
            if name in summaries:
                os.makedirs(os.path.join(root, name))
                with open(os.path.join(root, name, "summary.json"), "w") as handle:
                    json.dump(summaries[name], handle)
        binary = {"path": "/x/ide", "sha256": "0" * 64, "size_bytes": 1, "revision": None, "label": "A"}
        with open(os.path.join(root, "retirement-config.json"), "w") as handle:
            json.dump({"cells": cells, "baseline": binary, "candidate": dict(binary, label="B"), "cpu": 2, "pairs": None,
                       "modes": list(lab.RETIREMENT_MODES), "target_minutes_per_cell": 12.0, "seed": 1, "repo_root": root}, handle)
        lab.render_retirement(root)
        with open(os.path.join(root, "retirement.json")) as handle:
            return json.load(handle), root

    def all_cells(self, **override):
        summaries = {name: cell_summary() for name in lab.RETIREMENT_MODES}
        summaries[lab.RUNTIME_CELL] = cell_summary(wall=(1.0, 0.99, 1.02))
        summaries.update(override)
        return summaries

    def test_overall_pass_and_schema(self):
        summary, root = self.directory(self.all_cells())
        self.assertEqual(summary["schema"], lab.RETIREMENT_SCHEMA)
        self.assertEqual(set(summary), RETIREMENT_SUMMARY_KEYS)
        self.assertEqual(set(summary["verdict"]), RETIREMENT_VERDICT_KEYS)
        self.assertEqual(set(summary["baseline"]), RETIREMENT_BINARY_KEYS)
        self.assertEqual(set(summary["plan"]), RETIREMENT_PLAN_KEYS)
        for cell in summary["cells"]:
            self.assertEqual(set(cell), RETIREMENT_CELL_KEYS)
        self.assertEqual(summary["verdict"]["outcome"], "PASS", summary["verdict"])
        self.assertEqual([cell["cell"] for cell in summary["cells"]], ["none", "mir-stack", "fast", "quality", "generated-runtime"])
        self.assertEqual(summary["cells"][0]["summary"], os.path.join("none", "summary.json"))
        self.assertEqual(summary["limits"]["code_bytes"]["limit"], 1.01)
        self.assertEqual(summary["aggregates"]["compiler_wall_time"]["cells"], 4)
        self.assertAlmostEqual(summary["aggregates"]["compiler_wall_time"]["geomean_upper_bound"], 1.01)
        self.assertFalse(summary["aggregates"]["peak_rss"]["gating"])
        with open(os.path.join(root, "retirement.md")) as handle:
            text = handle.read()
        self.assertTrue(text.startswith("# Native-retirement performance gate (#512)\n\n**PASS: every required cell"))
        self.assertIn("| quality | generated code bytes | 1.0050 | exact | 1.01 | **PASS** |", text)

    def test_any_failure_fails(self):
        summary, _ = self.directory(self.all_cells(quality=cell_summary(code=(1000, 1020))))
        self.assertEqual(summary["verdict"]["outcome"], "FAIL")
        self.assertEqual(summary["verdict"]["failed"], ["quality code_bytes (1.020000 > 1.01)"])

    def test_partial_or_imprecise_is_inconclusive(self):
        partial = self.all_cells()
        del partial["none"]
        summary, _ = self.directory(partial)
        self.assertEqual(summary["verdict"]["outcome"], "INCONCLUSIVE")
        self.assertEqual(summary["verdict"]["missing_cells"], ["none"])
        self.assertEqual(summary["cells"][0]["checks"]["cell_measured"]["outcome"], "INCONCLUSIVE")
        summary, _ = self.directory(self.all_cells(fast=cell_summary(wall=(1.03, 1.01, 1.06))))
        self.assertEqual(summary["verdict"]["outcome"], "INCONCLUSIVE")
        self.assertEqual(summary["verdict"]["inconclusive"], ["fast compiler_wall_time (CI [1.0100, 1.0600] crosses 1.05)"])
        summary, _ = self.directory(self.all_cells(), {"quality": "failed"})
        self.assertEqual(summary["verdict"]["outcome"], "INCONCLUSIVE")
        self.assertIn("quality sub-run failed (stopped)", summary["verdict"]["inconclusive"])


@unittest.skipIf(not sys.platform.startswith("linux"), "the fake stage-1 compiler is a copied Linux ELF interpreter")
class RetirementFlowTests(Fakes, unittest.TestCase):
    # The fakes: A and B write the Python interpreter (a real ELF64, so code
    # bytes are measured) plus a pad byte as their stage-1 output.  In the
    # generated-runtime cell that output runs as a compiler: the copied
    # interpreter executes the repository root's `cc` script (the default
    # command's first argument), whose speed is the pad byte.  A is slower
    # and larger than B, so every interval check passes clearly.
    def setUp(self):
        self.root, self.ide, self.perf = self.fakes("new")
        common = {"SOURCE": SOURCE_METRICS, "METRICS": CC_METRICS, "MODE": "new", "OUTPUT_FILE": sys.executable}
        write_script(self.ide, FAKE_IDE, dict(common, DELAY=0.25, ALLOC=192 << 20, OUTPUT_PAD=b"S"))
        self.candidate = os.path.join(self.root, "ide-b")
        write_script(self.candidate, FAKE_IDE, dict(common, ALLOC=128 << 20, OUTPUT_PAD=b"F"))
        write_script(os.path.join(self.root, "cc"), FAKE_IDE, {"SOURCE": SOURCE_METRICS, "METRICS": CC_METRICS, "MODE": "new",
                                                               "SPEED_BY_EXE": {b"S": 0.25, b"F": 0.0}})

    def retire(self, arguments):
        output = os.path.join(self.root, "gate")
        stdout = sys.stdout
        try:
            sys.stdout = open(os.devnull, "w")
            lab.main(["retirement", "--baseline", self.ide, "--candidate", self.candidate, "--repo-root", self.root, "--cpu", "-1",
                      "--output", output, "--perf", self.perf, "--baseline-rev", "main@abc", "--candidate-rev", "522@def"] + arguments)
        finally:
            sys.stdout.close()
            sys.stdout = stdout
        with open(os.path.join(output, "retirement.json")) as handle:
            return json.load(handle), output

    def test_fast_cell_and_generated_runtime_end_to_end(self):
        summary, output = self.retire(["--modes", "fast", "--pairs", "6"])
        self.assertEqual(set(summary), RETIREMENT_SUMMARY_KEYS)
        cells = {cell["cell"]: cell for cell in summary["cells"]}
        self.assertEqual(list(cells), ["fast", "generated-runtime"])
        fast, runtime = cells["fast"], cells["generated-runtime"]
        self.assertEqual(fast["extra_args"], ["-fregister-allocator=fast"])
        self.assertEqual({name: check["outcome"] for name, check in fast["checks"].items()}, dict.fromkeys(COMPILER_CHECKS, "PASS"),
                         fast["checks"])
        self.assertEqual({name: check["outcome"] for name, check in runtime["checks"].items()}, dict.fromkeys(RUNTIME_CHECKS, "PASS"),
                         runtime["checks"])
        code = lab.code_sections(sys.executable)["code_bytes"]
        self.assertEqual((fast["checks"]["code_bytes"]["a_value"], fast["checks"]["code_bytes"]["ratio"]), (code, 1.0))
        self.assertGreaterEqual(fast["checks"]["peak_rss"]["a_value"], 192 << 20)
        self.assertLess(fast["checks"]["peak_rss"]["ratio"], 1.0)
        self.assertLess(runtime["checks"]["generated_runtime"]["ci_high"], 1.0)
        # Partial run: three allocator modes were not measured, so never PASS.
        self.assertEqual(summary["verdict"]["outcome"], "INCONCLUSIVE")
        self.assertEqual(summary["verdict"]["missing_cells"], ["none", "mir-stack", "quality"])
        self.assertEqual((summary["baseline"]["revision"], summary["candidate"]["revision"]), ("main@abc", "522@def"))
        self.assertEqual(summary["baseline"]["sha256"], lab.sha256_file(self.ide))
        stage1 = summary["stage1"]
        self.assertEqual(stage1["baseline"]["sha256"], lab.sha256_file(os.path.join(output, "fast", "a", "reference.exe")))
        self.assertEqual(runtime["baseline"]["path"], stage1["baseline"]["path"])
        self.assertEqual(runtime["summary_schema"], lab.COMPARE_SCHEMA)
        with open(os.path.join(output, "fast", "compare.json")) as handle:
            self.assertEqual(json.load(handle)["config"]["extra"], ["-fregister-allocator=fast"])
        self.assertIn("pair count fixed by --pairs 6", summary["warnings"][1])
        with open(os.path.join(output, "retirement.md")) as handle:
            report = handle.read()
        self.assertTrue(report.startswith("# Native-retirement performance gate (#512)\n\n**INCONCLUSIVE: cells not measured: none, mir-stack, quality"))
        before = lab.read_text(os.path.join(output, "retirement.json"))
        stdout = sys.stdout
        try:
            sys.stdout = open(os.devnull, "w")
            lab.main(["report", output])
        finally:
            sys.stdout.close()
            sys.stdout = stdout
        self.assertEqual(lab.read_text(os.path.join(output, "retirement.json")), before)

    def test_runtime_cell_needs_the_fast_cell(self):
        summary, _ = self.retire(["--modes", "none", "--pairs", "2"])
        runtime = summary["cells"][1]
        self.assertEqual((runtime["status"], runtime["note"]), ("not run", "needs the fast cell (--modes)"))
        self.assertEqual(summary["verdict"]["outcome"], "INCONCLUSIVE")
        self.assertEqual(summary["cells"][0]["checks"]["compiler_wall_time"]["outcome"], "INCONCLUSIVE")


if __name__ == "__main__":
    unittest.main()
