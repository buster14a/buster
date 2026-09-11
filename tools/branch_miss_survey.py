#!/usr/bin/env python3
"""Rank the branch mispredictions of one run by function and by source line.

A `perf record -e branch-misses` profile answers *which function* mispredicts,
and every audit that started from one had to re-learn the same limit: the event
is not precise, so a sample lands some distance past the branch that caused it
and the histogram names the function, not the line
(`docs/performance-audits/2026-08-22b.md` records that lesson).  Zen 3 and later
expose the AMD Last Branch Record extension, which `perf record -j any,u`
captures: every sample carries the last sixteen branches with a per-entry
mispredict flag, so the *branch instruction's own address* is recorded and the
count can be attributed to a line instead of a symbol.

The two instruments are independent and this script runs both: the LBR tally is
the ranking, and `--cross-check` re-measures the same run with
`perf record -e branch-misses:u` so the function-level shares can be compared.
They agree on the top of the list when the measurement is sound; they diverge
when the address mapping is wrong, which is the failure this script exists to
prevent:

  **A branch record's address is a runtime address in a PIE.**  Turning it into
  something `llvm-symbolizer` and the ELF symbol table accept needs the map's
  file offset *and* the `p_vaddr - p_offset` skew of the text segment (`0x1000`
  for a clang-built `ide`).  Getting that wrong shifts every address by one
  page, which does not fail — it silently reports neighbouring functions, and a
  function with 0.01% of cycles appears to own 3% of the mispredictions.
  `mapping_static_address` derives the skew from the program headers, and
  `--self-test` covers it.

Sampling error is what it is: the estimate for a site with `n` samples carries
roughly `sqrt(n)` of noise, so a 40-sample line is +/-16%, and only the ordering
of the top entries is meaningful.  Re-run with `--repeat` to pool recordings.

Usage:
    tools/branch_miss_survey.py                       # the stage-1 self-host compile
    tools/branch_miss_survey.py --repeat 3 --cross-check
    tools/branch_miss_survey.py --binary build/Release/ide -- build/Release/ide bench
    tools/branch_miss_survey.py --self-test

`perf`, `llvm-symbolizer` and `readelf` must be on `PATH`, and the binary wants
`BUSTER_DEBUG_INFO` (on by default outside `--ci`) for the line attribution.
Run it on a quiet machine: branch-miss counts move several percent under load.
"""

import argparse
import bisect
import collections
import os
import re
import shutil
import subprocess
import sys
import tempfile

# The stage-1 self-host compile: the benchmark every audit trends, and the one
# workload whose instruction count is reproducible to a few thousand.  Kept in
# step with `self_host_compile_add` in build.c and the recipe in AGENTS.md.
DEFAULT_BINARY = "build/Release/ide"
DEFAULT_COMMAND = [
    DEFAULT_BINARY,
    "cc",
    "-Isrc",
    "-Ibuild/generated",
    "-DBUSTER_UNITY_BUILD=1",
    "-DBUSTER_INCLUDE_TESTS=0",
    "-g",
    "-v",
    "-fsource-metrics=build/ide-self.metrics",
    "src/buster/apps/ide/ide.c",
    "-lm",
    "-o",
    "build/ide-self",
]

# One sample every 200k cycles yields ~20k samples and ~330k branch records over
# a one-second compile, of which ~10k are mispredicted: enough that the top
# thirty lines are separated by more than their sampling noise, and cheap enough
# that the recording does not perturb the run it measures.
DEFAULT_PERIOD = 200003
# A prime period for the cross-check too, so it cannot lock onto a loop.
CROSS_CHECK_PERIOD = 1009

PAGE_SIZE = 4096

STAT_EVENTS = ["instructions:u", "branches:u", "branch-misses:u", "cycles:u"]


class Mapping:
    """One executable mapping of the profiled binary, with its PIE skew."""

    def __init__(self, pid, start, length, page_offset, vaddr_skew):
        self.pid = pid
        self.start = start
        self.end = start + length
        self.page_offset = page_offset
        self.vaddr_skew = vaddr_skew

    def contains(self, address):
        return self.start <= address < self.end

    def static_address(self, address):
        return address - self.start + self.page_offset + self.vaddr_skew


def load_segments(binary):
    """Return the `(file_offset, vaddr, size)` of each PT_LOAD of `binary`."""
    text = subprocess.run(
        ["readelf", "-lW", binary], capture_output=True, text=True, check=True
    ).stdout
    segments = []
    for line in text.splitlines():
        fields = line.split()
        if len(fields) < 6 or fields[0] != "LOAD":
            continue
        segments.append((int(fields[1], 16), int(fields[2], 16), int(fields[5], 16)))
    return segments


def mapping_static_address(segments, page_offset, address, start):
    """Map a runtime `address` back to the static virtual address.

    `page_offset` is the mapping's *file* offset, which is what perf reports;
    the segment that covers it says what virtual address that file offset was
    linked at.  For a clang PIE the two differ by `0x1000`, and using the file
    offset alone is the one-page error described in the module docstring.
    """
    # A mapping's file offset is page-aligned down, so it can land inside the
    # *previous* segment's file range as well; the segment actually mapped is
    # the one whose own aligned start it equals, which is the last match.
    skew = 0
    for file_offset, vaddr, size in segments:
        aligned = file_offset & ~(PAGE_SIZE - 1)
        if aligned <= page_offset < file_offset + size:
            skew = vaddr - file_offset
    return address - start + page_offset + skew


def parse_mmap_events(script_output, binary):
    """Collect every executable mapping of `binary` from a perf script dump."""
    wanted = os.path.realpath(binary)
    pattern = re.compile(
        r"PERF_RECORD_MMAP2\s+(\d+)/\d+:\s+\[(0x[0-9a-f]+)\((0x[0-9a-f]+)\)\s+@\s+"
        r"(0x[0-9a-f]+|0)\s.*?\]:\s+\S*x\S*\s+(\S+)"
    )
    mappings = []
    for line in script_output.splitlines():
        match = pattern.search(line)
        if not match:
            continue
        path = match.group(5)
        if os.path.realpath(path) != wanted:
            continue
        mappings.append(
            (
                int(match.group(1)),
                int(match.group(2), 16),
                int(match.group(3), 16),
                int(match.group(4), 16),
            )
        )
    return mappings


def parse_branch_records(line):
    """Yield `(from_address, mispredicted)` for one `-F pid,brstack` line.

    A record is `FROM/TO/M|P/...`; entries perf could not resolve print as
    `[unknown]` and are skipped rather than counted as address zero.
    """
    for record in line.split():
        fields = record.split("/")
        if len(fields) < 3 or not fields[0].startswith("0x"):
            continue
        try:
            source = int(fields[0], 16)
        except ValueError:
            continue
        yield source, fields[2] == "M"


def tally(script_lines, mappings):
    """Count mispredicted branch records per static address."""
    by_pid = collections.defaultdict(list)
    for mapping in mappings:
        by_pid[mapping.pid].append(mapping)
    counts = collections.Counter()
    records = 0
    mispredicted = 0
    attributed = 0
    for line in script_lines:
        fields = line.split(None, 1)
        if len(fields) != 2 or not fields[0].isdigit():
            continue
        pid = int(fields[0])
        for source, missed in parse_branch_records(fields[1]):
            records += 1
            if not missed:
                continue
            mispredicted += 1
            for mapping in by_pid.get(pid, ()):
                if mapping.contains(source):
                    counts[mapping.static_address(source)] += 1
                    attributed += 1
                    break
    return counts, records, mispredicted, attributed


def function_symbols(binary):
    """Return the sorted `(address, size, name)` of every FUNC symbol."""
    text = subprocess.run(
        ["readelf", "-sW", binary], capture_output=True, text=True, check=True
    ).stdout
    symbols = []
    for line in text.splitlines():
        fields = line.split()
        if len(fields) < 8 or fields[3] != "FUNC":
            continue
        try:
            symbols.append((int(fields[1], 16), int(fields[2], 0), fields[7]))
        except ValueError:
            continue
    symbols.sort()
    return symbols


def containing_function(symbols, starts, address):
    index = bisect.bisect_right(starts, address) - 1
    if index < 0:
        return "[unknown]"
    start, size, name = symbols[index]
    if size and address >= start + size:
        return "[unknown]"
    return name


def symbolize(binary, addresses):
    """Return `{address: [(function, file:line), ...]}`, innermost frame first.

    `--inlines` is what makes the result useful in a unity build compiled at
    `-O3`: the containing ELF symbol is often a whole compiler phase, and the
    branch belongs to a small helper inlined into it.
    """
    if not addresses:
        return {}
    query = "".join("0x%x\n" % address for address in addresses)
    text = subprocess.run(
        [
            "llvm-symbolizer",
            "--obj=" + binary,
            "--functions=linkage",
            "--demangle",
            "--inlines",
        ],
        input=query,
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    frames = {}
    for address, block in zip(addresses, text.split("\n\n")):
        lines = [line for line in block.splitlines() if line.strip()]
        frames[address] = [
            (lines[index], lines[index + 1]) for index in range(0, len(lines) - 1, 2)
        ]
    return frames


def short_location(location):
    """`/abs/path/c_parse.c:7607:9` -> `c_parse.c:7607`."""
    fields = location.split(":")
    return "%s:%s" % (os.path.basename(fields[0]), fields[1] if len(fields) > 1 else "?")


def run_perf(command):
    """A failed workload or perf command never produces a measurement."""
    environment = dict(os.environ)
    environment["LC_ALL"] = "C"
    process = subprocess.run(
        command,
        capture_output=True,
        text=True,
        env=environment,
    )
    if process.returncode:
        raise SystemExit(
            "error: %s failed (exit %d):\n%s%s"
            % (" ".join(command), process.returncode, process.stdout, process.stderr)
        )
    return process


def run_perf_stat(command):
    """Return complete `{event: count}` data from a successful workload."""
    process = run_perf(
        ["perf", "stat", "-x,", "-e", ",".join(STAT_EVENTS), "--"] + command
    )
    counts = {}
    for line in process.stderr.splitlines():
        fields = line.split(",")
        if len(fields) < 3 or not fields[0].isdigit():
            continue
        counts[fields[2]] = int(fields[0])
    missing = [event for event in STAT_EVENTS if event not in counts]
    if missing:
        raise SystemExit(
            "error: perf stat did not count %s; no valid survey:\n%s"
            % (", ".join(missing), process.stderr)
        )
    return counts


def record_branch_stacks(command, binary, period, output):
    run_perf(
        [
            "perf",
            "record",
            "-q",
            "-e",
            "cycles:u",
            "-c",
            str(period),
            "-j",
            "any,u",
            "-o",
            output,
            "--",
        ]
        + command,
    )
    stacks = run_perf(
        ["perf", "script", "-i", output, "-F", "pid,brstack"],
    ).stdout
    mmaps = run_perf(
        ["perf", "script", "-i", output, "--show-mmap-events", "-F", "comm,pid,event"],
    ).stdout
    segments = load_segments(binary)
    mappings = []
    for pid, start, length, page_offset in parse_mmap_events(mmaps, binary):
        skew = mapping_static_address(segments, page_offset, start, start) - page_offset
        mappings.append(Mapping(pid, start, length, page_offset, skew))
    return stacks.splitlines(), mappings


def cross_check(command, binary, output):
    """Function shares from a plain `branch-misses:u` profile of the same run."""
    run_perf(
        [
            "perf",
            "record",
            "-q",
            "-e",
            "branch-misses:u",
            "-c",
            str(CROSS_CHECK_PERIOD),
            "-o",
            output,
            "--",
        ]
        + command,
    )
    text = run_perf(
        [
            "perf",
            "report",
            "-q",
            "-i",
            output,
            "--no-children",
            "--percent-limit",
            "0",
            "-F",
            "overhead,sym",
            "--sort",
            "sym",
            "--stdio",
        ],
    ).stdout
    shares = collections.Counter()
    for line in text.splitlines():
        match = re.match(r"\s*([\d.]+)%\s+\[\.\]\s+(.*)$", line)
        if match:
            shares[match.group(2).strip()] += float(match.group(1))
    return shares


def report(counts, symbols, binary, totals, top, sampled, shares):
    starts = [symbol[0] for symbol in symbols]
    attributed = sum(counts.values())
    misses = totals.get("branch-misses:u")
    scale = (misses / attributed) if (misses and attributed) else 0.0

    def absolute(count):
        return "{:>12,.0f}".format(count * scale)

    print("branch misses by function")
    print("-" * 78)
    by_function = collections.Counter()
    for address, count in counts.items():
        by_function[containing_function(symbols, starts, address)] += count
    cumulative = 0.0
    for name, count in by_function.most_common(top):
        share = 100.0 * count / attributed
        cumulative += share
        line = "%6.2f%% %s  %-48s cum %5.1f%%" % (
            share,
            absolute(count),
            name,
            cumulative,
        )
        if shares:
            line += "  event %5.2f%%" % shares.get(name, 0.0)
        print(line)

    print()
    print("branch misses by source line (innermost inlined frame)")
    print("-" * 78)
    frames = symbolize(binary, [address for address, _ in counts.most_common(4 * top)])
    by_line = collections.Counter()
    for address, count in counts.most_common(4 * top):
        chain = frames.get(address) or []
        if not chain:
            by_line[("[unknown]", "?")] += count
            continue
        outer = chain[-1][0]
        inner_function, inner_location = chain[0]
        label = (
            inner_function
            if inner_function == outer
            else "%s in %s" % (inner_function, outer)
        )
        by_line[(label, short_location(inner_location))] += count
    for (label, location), count in by_line.most_common(top):
        print(
            "%6.2f%% %s  %-28s %s"
            % (100.0 * count / attributed, absolute(count), location, label)
        )
    print()
    print(
        "%d mispredicted records over %d recording(s); one record is ~%.0f misses, "
        "so a row's sampling error is about 1/sqrt(records): +/-10%% at 100 records"
        % (attributed, sampled, scale)
    )


def survey(arguments):
    binary = arguments.binary
    command = arguments.command or list(DEFAULT_COMMAND)
    if not arguments.command:
        command[0] = binary
    if not os.path.exists(binary):
        sys.stderr.write(
            "error: %s does not exist; build it with "
            "./build.sh build --config Release -t ide\n" % binary
        )
        return 1
    for tool in ("perf", "llvm-symbolizer", "readelf"):
        if not shutil.which(tool):
            sys.stderr.write("error: %s is not on PATH\n" % tool)
            return 1

    totals = run_perf_stat(command)
    if totals:
        print("perf stat over one run of the workload")
        print("-" * 78)
        for event in STAT_EVENTS:
            if event in totals:
                print("{:<16} {:>16,}".format(event, totals[event]))
        if totals.get("branches:u"):
            print(
                "%-16s %15.3f%%"
                % (
                    "mispredict rate",
                    100.0 * totals["branch-misses:u"] / totals["branches:u"],
                )
            )
        print()

    counts = collections.Counter()
    records = mispredicted = attributed = 0
    directory = arguments.keep or tempfile.mkdtemp(prefix="branch-miss-survey-")
    os.makedirs(directory, exist_ok=True)
    for index in range(arguments.repeat):
        lines, mappings = record_branch_stacks(
            command,
            binary,
            arguments.period,
            os.path.join(directory, "lbr%d.data" % index),
        )
        if not mappings:
            sys.stderr.write(
                "error: no executable mapping of %s in the recording; is the "
                "profiled command actually running that binary?\n" % binary
            )
            return 1
        run_counts, run_records, run_misses, run_attributed = tally(lines, mappings)
        counts.update(run_counts)
        records += run_records
        mispredicted += run_misses
        attributed += run_attributed
    if not attributed:
        sys.stderr.write("error: no mispredicted branch records were captured\n")
        return 1
    print(
        "%d branch records sampled, %d mispredicted (%.2f%%), %d inside %s"
        % (records, mispredicted, 100.0 * mispredicted / records, attributed, binary)
    )
    print()

    shares = {}
    if arguments.cross_check:
        shares = cross_check(
            command, binary, os.path.join(directory, "branch-misses.data")
        )

    report(
        counts,
        function_symbols(binary),
        binary,
        totals,
        arguments.top,
        arguments.repeat,
        shares,
    )
    if arguments.keep:
        print("recordings kept in %s" % directory)
    return 0


def self_test():
    """Cover the address math and the parsing; no perf required."""
    from unittest.mock import patch

    failures = []

    def check(name, actual, expected):
        if actual != expected:
            failures.append("%s: %r != %r" % (name, actual, expected))

    # A clang PIE: the text segment's file offset and virtual address differ by
    # one page, which is exactly the skew a naive mapping drops.
    segments = [(0x000000, 0x000000, 0xE23030), (0xE23030, 0xE24030, 0x645000)]
    check(
        "pie skew applied",
        mapping_static_address(segments, 0xE23000, 0x55CA45DA59E0, 0x55CA45DA5000),
        0xE249E0,
    )
    check(
        "no skew for the first segment",
        mapping_static_address(segments, 0x0, 0x7F0000001000, 0x7F0000000000),
        0x1000,
    )

    mapping = Mapping(7, 0x55CA45DA5000, 0x645000, 0xE23000, 0x1000)
    check("mapping contains", mapping.contains(0x55CA45DA59E0), True)
    check("mapping excludes", mapping.contains(0x55CA46500000), False)
    check("mapping maps", mapping.static_address(0x55CA45DA59E0), 0xE249E0)

    record = "0x55ca45da59e0/0x55ca45da5a00/M/-/-/0//NON_SPEC_CORRECT_PATH"
    predicted = "0x55ca45da5a10/0x55ca45da5a30/P/-/-/0//NON_SPEC_CORRECT_PATH"
    unknown = "[unknown]/[unknown]/P/-/-/0//NON_SPEC_CORRECT_PATH"
    check(
        "branch records parsed",
        list(parse_branch_records(" ".join([record, predicted, unknown]))),
        [(0x55CA45DA59E0, True), (0x55CA45DA5A10, False)],
    )

    counts, records, mispredicted, attributed = tally(
        ["      7  %s %s" % (record, predicted), "      9  %s" % record],
        [mapping],
    )
    check("tally counts the mapped pid", counts[0xE249E0], 1)
    check("tally sees every record", records, 3)
    check("tally counts mispredictions", mispredicted, 2)
    check("tally drops the unmapped pid", attributed, 1)

    mmap_line = (
        "             ide    6966   765.639750: PERF_RECORD_MMAP2 6966/6966: "
        "[0x55ca45da5000(0x645000) @ 0xe23000 <f8e2fd37>]: r-xp /usr/bin/ide"
    )
    other = mmap_line.replace("/usr/bin/ide", "/usr/lib/libm.so.6")
    check(
        "mmap events filtered to the binary",
        parse_mmap_events(mmap_line + "\n" + other + "\n", "/usr/bin/ide"),
        [(6966, 0x55CA45DA5000, 0x645000, 0xE23000)],
    )

    symbols = [(0x1000, 0x40, "early"), (0x1040, 0, "sized_zero"), (0x2000, 0x10, "late")]
    starts = [symbol[0] for symbol in symbols]
    check("symbol lookup", containing_function(symbols, starts, 0x1020), "early")
    check("symbol lookup past the end", containing_function(symbols, starts, 0x2020), "[unknown]")
    check("symbol lookup below the first", containing_function(symbols, starts, 0x100), "[unknown]")
    check("short location", short_location("/a/b/c_parse.c:7607:9"), "c_parse.c:7607")

    stat_text = "".join("100,,%s,1,100.00\n" % event for event in STAT_EVENTS)
    for status, output, valid in (
        (0, stat_text, True),
        (1, stat_text + "link failed\n", False),
        (0, stat_text.replace("100,,branch-misses:u", "<not supported>,,branch-misses:u"), False),
        (0, stat_text.replace("100,,cycles:u", "<not counted>,,cycles:u"), False),
        (0, "", False),
    ):
        completed = subprocess.CompletedProcess(["perf"], status, "", output)
        accepted = False
        with patch.object(subprocess, "run", return_value=completed):
            try:
                measured = run_perf_stat(["workload"])
                accepted = measured == {event: 100 for event in STAT_EVENTS}
            except SystemExit:
                pass
        check("only complete successful stat results accepted", accepted, valid)
    check("stage 1 links libm", "-lm" in DEFAULT_COMMAND, True)
    check("stage 1 enables verbose metrics", "-v" in DEFAULT_COMMAND, True)

    for failure in failures:
        sys.stderr.write("FAIL %s\n" % failure)
    print("%d checks failed" % len(failures) if failures else "self-test passed")
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--binary", default=DEFAULT_BINARY, help="the profiled ELF")
    parser.add_argument("--repeat", type=int, default=1, help="recordings to pool")
    parser.add_argument("--period", type=int, default=DEFAULT_PERIOD, help="cycles per sample")
    parser.add_argument("--top", type=int, default=25, help="rows per table")
    parser.add_argument("--keep", help="directory to keep the recordings in")
    parser.add_argument(
        "--cross-check",
        action="store_true",
        help="also profile branch-misses:u and print each function's share",
    )
    parser.add_argument("--self-test", action="store_true", help="run the unit checks")
    parser.add_argument("command", nargs=argparse.REMAINDER, help="-- command to profile")
    arguments = parser.parse_args()
    if arguments.repeat < 1:
        parser.error("--repeat must be positive")
    if arguments.self_test:
        return self_test()
    if arguments.command and arguments.command[0] == "--":
        arguments.command = arguments.command[1:]
    return survey(arguments)


if __name__ == "__main__":
    sys.exit(main())
