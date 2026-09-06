#!/usr/bin/env python3
"""Survey where a compile's cache misses are, and what they cost.

This is the cache-miss counterpart to the branch-miss survey that produced the
`2026-08-21`/`2026-08-22` audit series.  It answers four questions in order,
because answering them out of order is how a cache "hotspot" turns into a week
of work for nothing:

1. **Is there a miss budget worth attacking at all?**  `perf stat` counts the
   whole hierarchy over the reference workload — L1d accesses and misses, the
   share of L1d fills L2 answers, the demand fills that reach DRAM/IO, both
   TLBs, and page faults.  A miss count means nothing without its latency:
   120 M L1d misses that all hit L2 are a different program from 7 M that reach
   memory.
2. **Which symbols own the misses?**  One `perf record` per event, aggregated by
   leaf symbol, printed beside the same symbol's `cycles` share.  The ratio
   (miss share / cycles share) is the column to read: a symbol at 1.0 misses in
   proportion to the time it spends, a symbol at 3.0 is waiting on memory.
3. **Which lines?**  Leaf addresses of the top symbols are symbolized through
   `llvm-symbolizer`, inline chain included, so a line inside an inlined callee
   is reported under the callee's name rather than the 40 KB function it was
   inlined into.
4. **And for the misses whose leaf is `memcpy`/`memset`, which call site?**  On
   this tree that is a fifth of all of them, and "17% of misses are in
   `__memset_avx512_unaligned_erms`" is not a finding until the code doing the
   zeroing is named.  `--dwarf` takes a second, sparser capture that unwinds
   with DWARF and reports the exact call site.

Method notes that are easy to get wrong, all of them paid for at least once:

- **Frame-pointer unwinding skips the caller of a libc routine.**  `memcpy` and
  `memset` never push `rbp`, so the first callchain entry after the leaf is the
  return address of *its caller's caller*: the site named is the one where the
  function that called `memcpy` was itself called.  That is still useful — the
  callee named on that line is where to look — but it is one level out, which is
  why `--dwarf` exists.  The fp tables say `via` to keep the distinction visible.
- **The sampled events are not precise.**  AMD's precise facility is IBS, which
  only samples system-wide and therefore needs root or
  `perf_event_paranoid <= 0`; this script deliberately stays unprivileged, so
  every sample carries skid.  Symbol attribution survives that (skid is a few
  instructions, hot loops are longer) and line attribution mostly does — read a
  line table as "this loop", not "this instruction".  `--ibs` prints the
  privileged recipe for when the question is *which object* misses.
- **Symbolize `sym+offset`, never `perf script -F srcline`.**  `srcline`
  resolves a callchain's raw return address, which points *after* the call and
  reports the following line.  A static virtual address is the `readelf` symbol
  value plus the offset perf prints; caller frames are symbolized one byte
  earlier, leaf frames as-is.  `dsoff` is a *file* offset and differs from the
  virtual address by `p_vaddr - p_offset` (0x1000 for this tree's PIE), so it is
  only used for foreign DSOs, and only after that delta is applied.
- **`perf script` without `--no-inline` walks DWARF for every frame** and took
  170 s on this tree where `--no-inline` takes under a second.  Inline chains
  come from `llvm-symbolizer` instead, batched over the deduplicated addresses
  that actually matter.
- **Subprocesses run under `LC_ALL=C`**, or `perf stat` prints `10.234.173.202`
  in a locale that groups with dots and the parser silently reads a float.
- Percentages are shares of the samples in the *whole* capture, libc and loader
  included, so two symbols' event columns can be compared directly.

Usage:
    tools/cache_miss_survey.py                        # budget, record, report
    tools/cache_miss_survey.py --dwarf                # + exact mem* call sites
    tools/cache_miss_survey.py --stage budget
    tools/cache_miss_survey.py --stage record
    tools/cache_miss_survey.py --stage report         # reuse captures in --out
    tools/cache_miss_survey.py --workload bench
    tools/cache_miss_survey.py --command ./build/Release/ide test
    tools/cache_miss_survey.py --self-test

The default workload is self-host stage 1: the Release `ide` compiling the unity
`src/buster/apps/ide/ide.c`, which is the measurement AGENTS.md trusts for
compiler throughput.  Build it first with

    ./build.sh build --config Release -t ide

and run the survey from the repository root on an otherwise idle machine.
"""

import argparse
import collections
import os
import re
import shutil
import statistics
import subprocess
import sys

BINARY = os.path.join("build", "Release", "ide")
BUILD_DIRECTORY = "build"
UNITY_SOURCE = os.path.join("src", "buster", "apps", "ide", "ide.c")

# perf stat groups.  Each is opened as one group (`{...}`) so its members share a
# multiplexing window and their ratios are exact; five counters fit the six
# general-purpose Zen counters with room for the group leader.  `instructions`
# anchors every group, which is also how a group that silently got multiplexed is
# spotted: its instruction count stops matching the others.
COUNTER_GROUPS = [
    (
        "core and L1d",
        [
            "instructions",
            "cycles",
            "L1-dcache-loads",
            "L1-dcache-load-misses",
            "l2_cache_req_stat.dc_access_in_l2",
        ],
    ),
    (
        "fills beyond L1d",
        [
            "instructions",
            "l2_cache_req_stat.ls_rd_blk_c",
            "ls_any_fills_from_sys.all",
            "ls_any_fills_from_sys.local_l2",
            "ls_any_fills_from_sys.dram_io_all",
        ],
    ),
    (
        "TLBs, instruction side, faults",
        [
            "instructions",
            "ls_l1_d_tlb_miss.all",
            "ls_l1_d_tlb_miss.all_l2_miss",
            "l1-icache-load-misses",
            "page-faults",
        ],
    ),
]

# The AMD-specific groups above are what make the budget readable; on any other
# vendor they fail to open.  This is the portable subset to fall back to.
PORTABLE_GROUPS = [
    (
        "core and L1d",
        ["instructions", "cycles", "L1-dcache-loads", "L1-dcache-load-misses"],
    ),
    (
        "last level, TLB, faults",
        [
            "instructions",
            "cache-references",
            "cache-misses",
            "dTLB-load-misses",
            "page-faults",
        ],
    ),
]

# Sampled events.  Periods are chosen for 20-60 K samples over a ~0.9 s stage-1
# compile: enough that a 1% share is 200-600 samples, few enough that the capture
# does not perturb the run.  Prime periods avoid locking onto a loop whose miss
# pattern repeats with a power-of-two stride.
SampledEvent = collections.namedtuple("SampledEvent", "key event period title note")

SAMPLED_EVENTS = [
    SampledEvent(
        "cycles",
        "cycles",
        100003,
        "cycles",
        "the denominator: what the time is actually spent on",
    ),
    SampledEvent(
        "l1d",
        "L1-dcache-load-misses",
        2003,
        "L1d load misses",
        "mostly answered by L2 (~14 cycles); the bulk count",
    ),
    SampledEvent(
        "l2miss",
        "l2_cache_req_stat.ls_rd_blk_c",
        211,
        "L2 read misses",
        "data reads that leave the core complex",
    ),
    SampledEvent(
        "dram",
        "ls_any_fills_from_sys.dram_io_all",
        251,
        "fills from DRAM/IO",
        "the expensive ones (~250+ cycles); rank leads by this",
    ),
]

PORTABLE_SAMPLED_EVENTS = [
    SAMPLED_EVENTS[0],
    SAMPLED_EVENTS[1],
    SampledEvent(
        "llc",
        "cache-misses",
        1009,
        "last-level cache misses",
        "the portable stand-in for the fill events",
    ),
]

# A DWARF capture costs about 4 KB of stack per sample, so it is taken at a tenth
# of the counter rate: it exists to attribute the mem* leaves, which are a fifth
# of every event, not to rank symbols.
DWARF_PERIOD_FACTOR = 4
DWARF_STACK_BYTES = 4096

# `perf script --no-inline -F comm,ip,sym,symoff,dso,dsoff` frame lines:
#     \t    564b1fe3897a c_lex_dispatch+0xeca (/abs/path/ide+0x1233ab0)
# An unresolved frame drops the `+0x...` after the symbol but keeps the dsoff.
FRAME = re.compile(
    r"^\s+(?P<ip>[0-9a-f]+)\s+(?P<symbol>.+?)(?:\+0x(?P<offset>[0-9a-f]+))?"
    r"\s+\((?P<dso>[^)+]*)(?:\+0x(?P<dsoff>[0-9a-f]+))?\)\s*$"
)
# `perf stat` under LC_ALL=C: "    10,234,173,202      instructions:u"
STAT_LINE = re.compile(
    r"^\s+(?P<value>[0-9][0-9,]*|<not supported>|<not counted>)\s+(?P<event>\S+)"
)
READELF_SYMBOL = re.compile(
    r"^\s*\d+:\s+(?P<value>[0-9a-f]+)\s+(?P<size>\d+)\s+(?P<type>\S+)\s+\S+\s+\S+\s+\S+\s+(?P<name>\S+)"
)
READELF_LOAD = re.compile(
    r"^\s+LOAD\s+0x(?P<offset>[0-9a-f]+)\s+0x(?P<vaddr>[0-9a-f]+)\s+\S+\s+0x(?P<filesz>[0-9a-f]+)"
)
OBJDUMP_SYMBOL = re.compile(r"<([^>+]+)(?:\+0x[0-9a-f]+)?>:")


def run(command, **keywords):
    """Run `command` with a C locale, returning its completed process."""
    environment = dict(os.environ)
    environment["LC_ALL"] = "C"
    return subprocess.run(
        command,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        **keywords,
    )


def workload_command(arguments, output):
    """The command whose cache behaviour is being surveyed.

    Stage 1 includes the link and source metrics used by the audit workload.
    It is pinned to one core: the compile is single-threaded, and pinning removes
    migrations that otherwise appear as cold-cache samples in whatever function
    happened to run first after the move.
    """
    if arguments.command:
        command = list(arguments.command)
    elif arguments.workload == "bench":
        command = [arguments.binary, "bench"]
    else:
        command = [
            arguments.binary,
            "cc",
            "-Isrc",
            "-I" + os.path.join(arguments.build_directory, "generated"),
            "-DBUSTER_UNITY_BUILD=1",
            "-DBUSTER_INCLUDE_TESTS=0",
            "-g",
            "-v",
            "-fsource-metrics=" + output + ".metrics",
            UNITY_SOURCE,
            "-lm",
            "-o",
            output,
        ]
    if arguments.cpu is not None:
        command = ["taskset", "-c", str(arguments.cpu)] + command
    return command


def check_environment(arguments):
    """Fail early on the things that silently produce a plausible wrong profile."""
    problems = []
    for tool in ("perf", "llvm-symbolizer", "readelf", "objdump"):
        if shutil.which(tool) is None:
            problems.append(f"{tool} is not on PATH")
    if not arguments.command and not os.path.exists(arguments.binary):
        problems.append(
            f"{arguments.binary} does not exist; "
            "./build.sh build --config Release -t ide"
        )
    paranoid = "/proc/sys/kernel/perf_event_paranoid"
    if os.path.exists(paranoid):
        with open(paranoid, encoding="utf-8") as handle:
            level = int(handle.read().strip())
        if level > 2:
            problems.append(
                f"perf_event_paranoid is {level}; user-space sampling needs <= 2"
            )
    if problems:
        raise SystemExit("error: " + "\n       ".join(problems))
    # A contended box does not fail, it lies: samples land wherever the process
    # resumed.  Warn rather than refuse, since a load average lags a machine that
    # has just gone quiet.
    load = os.getloadavg()[0]
    if load > 1.5:
        print(
            f"warning: load average is {load:.2f}; miss shares from a busy "
            "machine are not comparable across runs",
            file=sys.stderr,
        )


def parse_stat(text):
    """`perf stat` output -> {event: count}, dropping the `:u` modifier."""
    counts = {}
    for line in text.splitlines():
        match = STAT_LINE.match(line)
        if not match:
            continue
        value = match.group("value")
        if value.startswith("<"):
            continue
        counts[match.group("event").split(":")[0]] = int(value.replace(",", ""))
    return counts


def run_measurement(command, capture=None):
    """Reject failed workloads and remove incomplete or stale captures."""
    if capture is not None and os.path.exists(capture):
        os.unlink(capture)
    completed = run(command)
    if completed.returncode:
        if capture is not None and os.path.exists(capture):
            os.unlink(capture)
        raise SystemExit(
            f"error: {' '.join(command)} failed (exit {completed.returncode}):\n"
            + completed.stdout
        )
    if capture is not None and (
        not os.path.exists(capture) or os.path.getsize(capture) == 0
    ):
        if os.path.exists(capture):
            os.unlink(capture)
        raise SystemExit(f"error: perf produced no capture at {capture}")
    return completed


def measure_budget(arguments, output, groups):
    """Median counts per event over `--repeat` runs of the workload."""
    command = workload_command(arguments, output)
    samples = collections.defaultdict(list)
    for title, events in groups:
        for _ in range(arguments.repeat):
            completed = run_measurement(
                ["perf", "stat", "-e", "{" + ",".join(events) + "}:u", "--"] + command
            )
            counts = parse_stat(completed.stdout)
            missing = [event for event in events if event not in counts]
            if missing:
                raise SystemExit(
                    f"error: group '{title}' did not count {', '.join(missing)}; "
                    "no valid budget (try --portable if these events are "
                    f"unavailable on this CPU):\n{completed.stdout}"
                )
            for event in events:
                samples[event].append(counts[event])
    return {event: int(statistics.median(values)) for event, values in samples.items()}


def elf_symbols(path):
    """{name: (value, size)} for the defined function symbols of an ELF file."""
    completed = run(["readelf", "-sW", path])
    symbols = {}
    for line in completed.stdout.splitlines():
        match = READELF_SYMBOL.match(line)
        if not match or match.group("type") != "FUNC":
            continue
        value = int(match.group("value"), 16)
        if value:
            symbols.setdefault(
                match.group("name").split("@")[0], (value, int(match.group("size")))
            )
    return symbols


def file_offset_to_vaddr(path, offset):
    """Translate an ELF file offset into a virtual address.

    Feeding a `dsoff` straight to a disassembler or symbolizer yields
    wrong-but-believable answers whenever the two differ, which they do for
    every PIE this tree builds.
    """
    completed = run(["readelf", "-lW", path])
    for line in completed.stdout.splitlines():
        match = READELF_LOAD.match(line)
        if not match:
            continue
        start = int(match.group("offset"), 16)
        size = int(match.group("filesz"), 16)
        if start <= offset < start + size:
            return offset + int(match.group("vaddr"), 16) - start
    return offset


class ForeignSymbols:
    """Names for addresses in a DSO whose symbols perf could not resolve.

    A distribution's libc keeps the ifunc-selected `__memcpy_avx512_*` bodies out
    of `.dynsym`, so perf prints a bare address for the routine that owns a fifth
    of this workload's misses.  `objdump` finds the name through whatever debug
    file the distribution ships, and one probe per 64-byte block is enough
    because the question is only which routine, not which instruction.
    """

    def __init__(self):
        self.cache = {}

    def name(self, path, offset):
        if offset is None or not os.path.exists(path):
            return "[unknown]"
        block = offset & ~0x3F
        key = (path, block)
        if key not in self.cache:
            address = file_offset_to_vaddr(path, block)
            completed = run(
                [
                    "objdump",
                    "-d",
                    f"--start-address={address}",
                    f"--stop-address={address + 1}",
                    path,
                ]
            )
            match = OBJDUMP_SYMBOL.search(completed.stdout)
            self.cache[key] = match.group(1) if match else "[unknown]"
        return self.cache[key]


Sample = collections.namedtuple("Sample", "leaf callers")
Frame = collections.namedtuple("Frame", "symbol offset dso dso_path dsoff")


def parse_script(text):
    """`perf script` output -> [Sample], leaf frame first, callers outward."""
    samples = []
    frames = []
    for line in text.splitlines():
        if not line.strip():
            if frames:
                samples.append(Sample(frames[0], tuple(frames[1:])))
            frames = []
            continue
        match = FRAME.match(line)
        if not match:
            # The comm/pid header line that opens each sample block.
            continue
        offset = match.group("offset")
        dsoff = match.group("dsoff")
        path = match.group("dso")
        frames.append(
            Frame(
                match.group("symbol").strip(),
                int(offset, 16) if offset else None,
                os.path.basename(path),
                path,
                int(dsoff, 16) if dsoff else None,
            )
        )
    if frames:
        samples.append(Sample(frames[0], tuple(frames[1:])))
    return samples


def attribute(samples, binary_name, foreign):
    """Per-symbol counts, plus the caller table for leaves outside the binary.

    A leaf in a stripped libc is counted under its resolved routine name *and*
    charged to the nearest frame back inside the surveyed binary, because the fix
    for a `memcpy` miss is always in the code that asked for the copy.
    """
    leaves = collections.Counter()
    charged = collections.Counter()
    foreign_callers = collections.defaultdict(collections.Counter)
    for sample in samples:
        leaf = sample.leaf
        if leaf.dso == binary_name:
            leaves[leaf.symbol] += 1
            charged[leaf.symbol] += 1
            continue
        name = leaf.symbol
        if name == "[unknown]":
            name = foreign.name(leaf.dso_path, leaf.dsoff)
        name = f"{leaf.dso}:{name}"
        leaves[name] += 1
        for frame in sample.callers:
            if frame.dso == binary_name:
                charged[frame.symbol] += 1
                foreign_callers[name][frame.symbol] += 1
                break
    return leaves, charged, foreign_callers


def leaf_addresses(samples, binary_name, symbols, wanted):
    """{static vaddr: count} for leaf frames of the `wanted` symbols.

    The leaf frame is where the sample was taken, so it is symbolized as-is; only
    a callchain's return addresses need the byte-before treatment.
    """
    addresses = collections.Counter()
    for sample in samples:
        leaf = sample.leaf
        if leaf.dso != binary_name or leaf.symbol not in wanted:
            continue
        if leaf.offset is None or leaf.symbol not in symbols:
            continue
        addresses[symbols[leaf.symbol][0] + leaf.offset] += 1
    return addresses


def foreign_call_sites(samples, binary_name, symbols, foreign):
    """{(routine, return address): count} for leaves outside the binary.

    The address is the return address minus one, ready to symbolize: it is a call
    site, so the raw value points at the instruction *after* the call and would
    report the following line.
    """
    sites = collections.Counter()
    for sample in samples:
        leaf = sample.leaf
        if leaf.dso == binary_name:
            continue
        routine = leaf.symbol
        if routine == "[unknown]":
            routine = foreign.name(leaf.dso_path, leaf.dsoff)
        for frame in sample.callers:
            if frame.dso == binary_name and frame.offset is not None:
                if frame.symbol in symbols:
                    sites[(routine, symbols[frame.symbol][0] + frame.offset - 1)] += 1
                break
    return sites


def symbolize(path, addresses):
    """{address: [(function, file:line), ...]}, innermost inline frame first."""
    if not addresses:
        return {}
    request = "".join(f"CODE {path} 0x{address:x}\n" for address in addresses)
    completed = run(
        [
            "llvm-symbolizer",
            "--functions=linkage",
            "--demangle",
            "--inlining",
            "--output-style=LLVM",
        ],
        input=request,
    )
    resolved = {}
    for address, block in zip(addresses, completed.stdout.split("\n\n")):
        lines = [line for line in block.splitlines() if line.strip()]
        frames = []
        for index in range(0, len(lines) - 1, 2):
            file_name, _, rest = lines[index + 1].partition(":")
            frames.append(
                (
                    lines[index],
                    f"{os.path.basename(file_name)}:{rest.split(':')[0]}",
                )
            )
        resolved[address] = frames
    return resolved


def share(count, total):
    return 100.0 * count / total if total else 0.0


def format_budget(counts):
    """The counter budget, as ratios rather than raw counts where it helps."""
    lines = []
    instructions = counts.get("instructions", 0)
    cycles = counts.get("cycles", 0)
    lines.append(f"  instructions              {instructions:>15,}")
    if cycles:
        lines.append(
            f"  cycles                    {cycles:>15,}"
            f"   (IPC {instructions / cycles:.2f})"
        )
    loads = counts.get("L1-dcache-loads", 0)
    l1_misses = counts.get("L1-dcache-load-misses", 0)
    if loads and instructions:
        lines.append(f"  L1d loads                 {loads:>15,}")
        lines.append(
            f"  L1d load misses           {l1_misses:>15,}"
            f"   ({share(l1_misses, loads):.2f}% of loads,"
            f" {1000.0 * l1_misses / instructions:.1f}/kinstr)"
        )
    for event, label in (
        ("l2_cache_req_stat.dc_access_in_l2", "L1d fills asked of L2"),
        ("ls_any_fills_from_sys.local_l2", "  ... answered by L2"),
        ("l2_cache_req_stat.ls_rd_blk_c", "L2 read misses"),
        ("ls_any_fills_from_sys.dram_io_all", "fills from DRAM/IO"),
        ("cache-references", "last-level references"),
        ("cache-misses", "last-level misses"),
        ("ls_l1_d_tlb_miss.all", "L1 dTLB misses"),
        ("ls_l1_d_tlb_miss.all_l2_miss", "  ... page walks"),
        ("dTLB-load-misses", "dTLB load misses"),
        ("l1-icache-load-misses", "L1i misses"),
        ("page-faults", "page faults"),
    ):
        if event not in counts:
            continue
        value = counts[event]
        suffix = ""
        if l1_misses and event in (
            "l2_cache_req_stat.ls_rd_blk_c",
            "ls_any_fills_from_sys.dram_io_all",
            "cache-misses",
        ):
            suffix = f"   ({share(value, l1_misses):.1f}% of L1d misses)"
        lines.append(f"  {label:<24}  {value:>15,}{suffix}")
    dram = counts.get("ls_any_fills_from_sys.dram_io_all") or counts.get("cache-misses")
    if dram and cycles:
        # 250 cycles is the conventional loaded-DRAM figure for this class of
        # part; the product is an upper bound on what perfect locality could
        # return, since out-of-order execution and the prefetchers already hide
        # some of it.  It exists to size the prize before anyone spends a week.
        lines.append(
            f"  DRAM latency upper bound  {250 * dram:>15,}"
            f"   cycles at 250/fill = {share(250 * dram, cycles):.1f}% of the run"
        )
    return "\n".join(lines)


def format_symbol_table(profiles, totals, order_key, limit):
    """One row per symbol: every event's share, and miss intensity vs cycles."""
    events = list(profiles)
    header = f"  {'symbol':<48}" + "".join(f"{event:>12}" for event in events)
    if "cycles" in profiles:
        header += f"{'x cycles':>10}"
    rows = [header, "  " + "-" * (48 + 12 * len(events) + 10)]
    for symbol, _ in profiles[order_key].most_common(limit):
        row = f"  {symbol[:46]:<48}"
        for event in events:
            row += f"{share(profiles[event][symbol], totals[event]):>11.2f}%"
        if "cycles" in profiles:
            cycles_share = share(profiles["cycles"][symbol], totals["cycles"])
            miss_share = share(profiles[order_key][symbol], totals[order_key])
            row += (
                f"{miss_share / cycles_share:>10.2f}" if cycles_share else f"{'-':>10}"
            )
        rows.append(row)
    return "\n".join(rows)


def record(arguments, output, events):
    """One capture per event; the workload is re-run for each."""
    command = workload_command(arguments, output)
    # A new batch cannot reuse an old event or an old optional DWARF capture.
    for event in events:
        for suffix in ("data", "dwarf.data"):
            path = os.path.join(arguments.out, f"{event.key}.{suffix}")
            if os.path.exists(path):
                os.unlink(path)
    for event in events:
        path = os.path.join(arguments.out, f"{event.key}.data")
        run_measurement(
            ["perf", "record", "-e", event.event + ":u", "-c", str(event.period)]
            + ["-g", "--call-graph", "fp", "-o", path, "--"]
            + command,
            capture=path,
        )
        if not arguments.dwarf or event.key == "cycles":
            continue
        dwarf_path = os.path.join(arguments.out, f"{event.key}.dwarf.data")
        run_measurement(
            ["perf", "record", "-e", event.event + ":u"]
            + ["-c", str(event.period * DWARF_PERIOD_FACTOR)]
            + ["-g", "--call-graph", f"dwarf,{DWARF_STACK_BYTES}"]
            + ["-o", dwarf_path, "--"]
            + command,
            capture=dwarf_path,
        )


def read_capture(path):
    """`perf script` a capture into samples.

    `--no-inline` is not a detail: with inline expansion perf resolves DWARF for
    every frame of every sample, which measured 170 s against under a second
    here.  Inline chains are recovered later, for the few hundred addresses that
    matter, through llvm-symbolizer.
    """
    completed = run_measurement(
        ["perf", "script", "-i", path, "--no-demangle", "--no-inline"]
        + ["-F", "comm,ip,sym,symoff,dso,dsoff"]
    )
    samples = parse_script(completed.stdout)
    if not samples:
        raise SystemExit(f"error: no samples in {path}; no valid profile")
    return samples


def format_call_sites(samples, binary, binary_name, symbols, foreign, total, limit, note):
    """The `memcpy`/`memset` call-site table for one capture."""
    sites = foreign_call_sites(samples, binary_name, symbols, foreign)
    if not sites:
        return []
    ranked = sites.most_common(limit * 4)
    resolved = symbolize(binary, [address for (_, address), _ in ranked])
    aggregated = collections.Counter()
    for (routine, address), count in ranked:
        frames = resolved.get(address) or [("[unknown]", "?")]
        function, location = frames[0]
        aggregated[(routine, f"{function} @ {location}")] += count
    out = [
        "",
        f"Call sites of the leaves outside {binary_name} "
        f"({share(sum(sites.values()), total):.2f}% of samples) - {note}",
        "",
    ]
    for (routine, site), count in aggregated.most_common(limit):
        out.append(f"  {share(count, total):>6.2f}%  {routine:<34}{site}")
    return out


def report(arguments, events, budget):
    binary_name = os.path.basename(arguments.binary)
    symbols = elf_symbols(arguments.binary) if os.path.exists(arguments.binary) else {}
    foreign = ForeignSymbols()
    profiles, totals, leaf_tables, parsed = {}, {}, {}, {}
    for event in events:
        path = os.path.join(arguments.out, f"{event.key}.data")
        if not os.path.exists(path):
            continue
        samples = read_capture(path)
        leaves, charged, callers = attribute(samples, binary_name, foreign)
        parsed[event.key] = samples
        profiles[event.key] = charged
        totals[event.key] = sum(leaves.values())
        leaf_tables[event.key] = (leaves, callers)

    out = ["=" * 100, "CACHE MISS SURVEY", "=" * 100]
    if budget:
        out += ["", "Counter budget (medians over the workload)", "", format_budget(budget)]
    if not profiles:
        raise SystemExit("error: no captures found; run with --stage record first")

    out += ["", "Samples per capture", ""]
    for event in events:
        if event.key in totals:
            out.append(
                f"  {event.title:<26} {totals[event.key]:>8,} samples"
                f"  (every {event.period} events) - {event.note}"
            )

    order = "dram" if "dram" in profiles else ("llc" if "llc" in profiles else "l1d")
    out += [
        "",
        f"Hotspots by {order}, as shares of that event's whole capture.  A leaf in",
        "libc is charged to the nearest frame back inside the binary.  'x cycles' is the",
        "miss share over the same symbol's cycles share: 1.0 misses in proportion to its",
        "time, above 2.0 is waiting on memory.",
        "",
        format_symbol_table(profiles, totals, order, arguments.limit),
    ]
    if order != "l1d" and "l1d" in profiles:
        out += [
            "",
            "Same table ordered by L1d misses (the bulk count)",
            "",
            format_symbol_table(profiles, totals, "l1d", arguments.limit),
        ]

    for key in dict.fromkeys((order, "l1d")):
        if key not in leaf_tables:
            continue
        leaves, callers = leaf_tables[key]
        rows = [
            (name, count)
            for name, count in leaves.most_common()
            if ":" in name and count * 200 >= totals[key]
        ]
        if rows:
            out += ["", f"Leaves outside {binary_name} for {key}", ""]
            for name, count in rows:
                out.append(f"  {name}  {share(count, totals[key]):.2f}%")

        dwarf_path = os.path.join(arguments.out, f"{key}.dwarf.data")
        if os.path.exists(dwarf_path):
            dwarf_samples = read_capture(dwarf_path)
            out += format_call_sites(
                dwarf_samples,
                arguments.binary,
                binary_name,
                symbols,
                foreign,
                len(dwarf_samples),
                arguments.limit,
                "DWARF unwind, so this is the exact site of the call",
            )
        else:
            out += format_call_sites(
                parsed[key],
                arguments.binary,
                binary_name,
                symbols,
                foreign,
                totals[key],
                arguments.limit,
                "frame-pointer unwind, so this is the site that called the function "
                "that called it; --dwarf resolves one level in",
            )

    if not symbols:
        return "\n".join(out)
    for key in dict.fromkeys((order, "l1d")):
        if key not in profiles:
            continue
        wanted = [symbol for symbol, _ in profiles[key].most_common(arguments.lines)]
        addresses = leaf_addresses(parsed[key], binary_name, symbols, set(wanted))
        resolved = symbolize(arguments.binary, list(addresses))
        per_line = collections.defaultdict(collections.Counter)
        for address, count in addresses.items():
            frames = resolved.get(address)
            if frames:
                function, location = frames[0]
                per_line[frames[-1][0]][f"{location}  {function}"] += count
        out += [
            "",
            f"Lines inside the top {arguments.lines} {key} symbols "
            "(skid: read a line as its loop, not its instruction)",
            "",
        ]
        for symbol in wanted:
            lines = per_line.get(symbol)
            if not lines:
                continue
            out.append(
                f"  {symbol}  ({share(profiles[key][symbol], totals[key]):.2f}%)"
            )
            for location, count in lines.most_common(6):
                out.append(f"      {share(count, totals[key]):>6.2f}%  {location}")
    return "\n".join(out)


def self_test():
    """Parser checks; the pipeline itself needs a machine with perf on it."""
    import tempfile
    from unittest.mock import patch

    script = (
        "main_thread \n"
        "\t    564b1fe3897a c_lex_dispatch+0xeca (/abs/build/Release/ide+0x1233ab0)\n"
        "\t    564b1fe19eb7 c_preprocess+0x82c7 (/abs/build/Release/ide+0x1214f2a)\n"
        "\n"
        "main_thread \n"
        "\t    7f4d61b8fd07 [unknown] (/usr/lib/libc.so.6+0x18fd07)\n"
        "\t    564b1fa4a546 arena_allocate_bytes+0xa16 (/abs/build/Release/ide+0xe249b0)\n"
        "\n"
    )
    samples = parse_script(script)
    assert len(samples) == 2, samples
    assert samples[0].leaf.symbol == "c_lex_dispatch", samples[0]
    assert samples[0].leaf.offset == 0xECA, samples[0]
    assert samples[0].leaf.dso == "ide", samples[0]
    assert samples[0].callers[0].symbol == "c_preprocess"
    assert samples[1].leaf.offset is None and samples[1].leaf.dsoff == 0x18FD07

    class Stub:
        def name(self, path, offset):
            return f"__memset@{offset:x}"

    leaves, charged, callers = attribute(samples, "ide", Stub())
    assert charged["c_lex_dispatch"] == 1, charged
    assert charged["arena_allocate_bytes"] == 1, charged
    assert leaves["libc.so.6:__memset@18fd07"] == 1, leaves
    assert callers["libc.so.6:__memset@18fd07"]["arena_allocate_bytes"] == 1, callers

    symbols = {"c_lex_dispatch": (0x1233AB0, 8257), "arena_allocate_bytes": (0xE249B0, 64)}
    addresses = leaf_addresses(samples, "ide", symbols, {"c_lex_dispatch"})
    assert addresses == {0x1233AB0 + 0xECA: 1}, addresses
    sites = foreign_call_sites(samples, "ide", symbols, Stub())
    # The return address is symbolized one byte earlier: it is a call site.
    assert sites == {("__memset@18fd07", 0xE249B0 + 0xA16 - 1): 1}, sites

    counts = parse_stat(
        "    10,234,173,202      instructions:u\n"
        "       124,535,322      L1-dcache-load-misses:u\n"
        "   <not supported>      LLC-loads:u\n"
    )
    assert counts["instructions"] == 10234173202, counts
    assert counts["L1-dcache-load-misses"] == 124535322, counts
    assert "LLC-loads" not in counts, counts

    arguments = argparse.Namespace(
        command=None, workload="stage1", binary=BINARY,
        build_directory=BUILD_DIRECTORY, cpu=None, repeat=1,
    )
    command = workload_command(arguments, "out")
    assert "-lm" in command and "-v" in command, command
    assert "-fsource-metrics=out.metrics" in command, command
    group = [("test", ["instructions", "cache-misses"])]
    stat_text = "  1,000 instructions:u\n  10 cache-misses:u\n"
    for status, output, valid in (
        (0, stat_text, True),
        (1, stat_text + "link failed\n", False),
        (0, stat_text.replace("10 cache-misses", "<not supported> cache-misses"), False),
        (0, stat_text.replace("10 cache-misses", "<not counted> cache-misses"), False),
        (0, "", False),
    ):
        completed = subprocess.CompletedProcess(["perf"], status, output)
        accepted = False
        with patch.object(subprocess, "run", return_value=completed) as mocked_run:
            try:
                measured = measure_budget(arguments, "out", group)
                accepted = measured == {"instructions": 1000, "cache-misses": 10}
            except SystemExit:
                pass
        assert mocked_run.call_args.args[0][3] == "{instructions,cache-misses}:u"
        assert accepted == valid, (status, output, accepted)

    with tempfile.TemporaryDirectory(prefix="cache-survey-self-test-") as directory:
        capture = os.path.join(directory, "stale.data")
        for status in (0, 1):
            with open(capture, "wb") as handle:
                handle.write(b"old capture")
            completed = subprocess.CompletedProcess(["perf"], status, "no new capture")
            rejected = False
            with patch.object(subprocess, "run", return_value=completed):
                try:
                    run_measurement(["perf", "record"], capture=capture)
                except SystemExit:
                    rejected = True
            assert rejected and not os.path.exists(capture), status

        arguments.out = directory
        arguments.dwarf = False
        stale_paths = []
        for event in PORTABLE_SAMPLED_EVENTS:
            for suffix in ("data", "dwarf.data"):
                path = os.path.join(directory, f"{event.key}.{suffix}")
                stale_paths.append(path)
                with open(path, "wb") as handle:
                    handle.write(b"old capture")
        completed = subprocess.CompletedProcess(["perf"], 1, "recording failed")
        with patch.object(subprocess, "run", return_value=completed) as mocked_run:
            try:
                record(arguments, "out", PORTABLE_SAMPLED_EVENTS)
            except SystemExit:
                pass
        assert mocked_run.call_args.args[0][3] == "cycles:u"
        assert not any(os.path.exists(path) for path in stale_paths), stale_paths

        for stage in ("all", "budget", "record", "report"):
            budget_path = os.path.join(directory, "budget.txt")
            report_path = os.path.join(directory, "report.txt")
            for path in (budget_path, report_path):
                with open(path, "w", encoding="utf-8") as handle:
                    handle.write("old measurement")
            with patch(__name__ + ".check_environment", side_effect=SystemExit("blocked")):
                try:
                    main(["--out", directory, "--stage", stage])
                except SystemExit:
                    pass
            assert not os.path.exists(report_path), stage
            assert os.path.exists(budget_path) == (stage in ("record", "report")), stage

    for status in (0, 1):
        completed = subprocess.CompletedProcess(["perf"], status, "")
        rejected = False
        with patch.object(subprocess, "run", return_value=completed):
            try:
                read_capture("empty.data")
            except SystemExit:
                rejected = True
        assert rejected, status

    match = READELF_SYMBOL.match(
        "  1747: 0000000001233ab0  8257 FUNC    LOCAL  DEFAULT   28 c_lex_dispatch"
    )
    assert match and match.group("name") == "c_lex_dispatch"
    assert int(match.group("value"), 16) == 0x1233AB0
    match = READELF_LOAD.match(
        "  LOAD           0x024000 0x0000000000024000 0x0000000000024000 "
        "0x17a139 0x17a139 R E 0x1000"
    )
    assert match and int(match.group("vaddr"), 16) == 0x24000
    assert OBJDUMP_SYMBOL.search(
        "000000000018fd07 <__memset_avx512_unaligned_erms+0x87>:"
    ).group(1) == "__memset_avx512_unaligned_erms"

    print("self-test: ok")


IBS_HELP = """\
Data-address profiling needs AMD IBS, which only samples system-wide and so needs
root (or perf_event_paranoid <= 0).  This script stays unprivileged; run this by
hand when a symbol table is not enough and the question is *which object* misses:

    sudo perf record -a -e ibs_op/l3missonly=1/ -c 5000 -d --  <workload>
    sudo perf report -i perf.data --sort=symbol,dso,mem --stdio

`-d` records the data address of each sampled load and `--sort ...,mem` groups by
data source (L2, L3, DRAM).  Lower `-c` until the sample count is useful: IBS tags
one micro-op per period, so its periods are much smaller than a counter's.
"""


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", default=BINARY)
    parser.add_argument("--build-directory", default=BUILD_DIRECTORY)
    parser.add_argument(
        "--workload",
        choices=("stage1", "bench"),
        default="stage1",
        help="stage1 compiles the unity ide.c (default); bench runs `ide bench`",
    )
    parser.add_argument(
        "--command",
        nargs=argparse.REMAINDER,
        help="run this command instead of a named workload (must be last)",
    )
    parser.add_argument(
        "--stage", choices=("all", "budget", "record", "report"), default="all"
    )
    parser.add_argument("--out", default=os.path.join("build", "cache-miss-survey"))
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--cpu", type=int, default=2, help="core to pin to; -1 for none")
    parser.add_argument("--limit", type=int, default=25, help="rows per table")
    parser.add_argument(
        "--lines", type=int, default=8, help="symbols to break down by source line"
    )
    parser.add_argument(
        "--dwarf",
        action="store_true",
        help="take a second, sparser DWARF capture so mem* call sites are exact",
    )
    parser.add_argument(
        "--portable",
        action="store_true",
        help="use only architecture-neutral events (no AMD fill/TLB detail)",
    )
    parser.add_argument("--ibs", action="store_true", help="print the IBS recipe and exit")
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.repeat < 1:
        parser.error("--repeat must be positive")

    if arguments.self_test:
        self_test()
        return 0
    if arguments.ibs:
        print(IBS_HELP)
        return 0
    if arguments.cpu is not None and arguments.cpu < 0:
        arguments.cpu = None

    # A failed new measurement must not leave a previous report or budget
    # looking current. Explicit budget -> record -> report reuse keeps its budget.
    stale_names = ["report.txt"]
    if arguments.stage in ("all", "budget"):
        stale_names.append("budget.txt")
    for name in stale_names:
        path = os.path.join(arguments.out, name)
        if os.path.exists(path):
            os.unlink(path)
    check_environment(arguments)
    os.makedirs(arguments.out, exist_ok=True)
    output = os.path.join(arguments.out, "workload-output")
    groups = PORTABLE_GROUPS if arguments.portable else COUNTER_GROUPS
    events = PORTABLE_SAMPLED_EVENTS if arguments.portable else SAMPLED_EVENTS

    budget = {}
    budget_path = os.path.join(arguments.out, "budget.txt")
    if arguments.stage in ("all", "budget"):
        budget = measure_budget(arguments, output, groups)
        with open(budget_path, "w", encoding="utf-8") as handle:
            for event, value in sorted(budget.items()):
                handle.write(f"{event}={value}\n")
    elif os.path.exists(budget_path):
        with open(budget_path, encoding="utf-8") as handle:
            budget = {
                line.split("=")[0]: int(line.split("=")[1])
                for line in handle.read().splitlines()
                if "=" in line
            }

    if arguments.stage in ("all", "record"):
        record(arguments, output, events)

    if arguments.stage in ("all", "report", "budget"):
        if arguments.stage == "budget":
            text = "Counter budget (medians over the workload)\n\n" + format_budget(budget)
        else:
            text = report(arguments, events, budget)
        print(text)
        with open(
            os.path.join(arguments.out, "report.txt"), "w", encoding="utf-8"
        ) as handle:
            handle.write(text + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
