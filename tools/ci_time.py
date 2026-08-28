#!/usr/bin/env python3
"""Aggregate buster's Forgejo CI time over a window: runner hours and push latency.

Two independent series, both read from the Actions API under
`https://code.buster14a.com/api/v1/repos/buster/buster`:

  runner time  -- `actions/tasks`, one row per job leg (its `name` is the
                  runner), duration `updated_at - run_started_at`. This is the
                  machine time the matrix costs.
  push latency -- `actions/runs`, `stopped - started`, i.e. how long a push
                  waits for a verdict. Joined to tasks by
                  `run_number == index_in_repo`.

`actions/tasks` is the only per-job timing source: `actions/runs/{id}/jobs`
answers here with `started_at` and `completed_at` always null.

Three properties of this API make a naive aggregate wrong, and the whole point
of this script is that it encodes them:

  **`task.updated_at` is not the end of the job.** Forgejo's log-retention
  sweep re-touches the row long after the job finished, and whole batches share
  a single end second — every row of one day stamped `23:45:3x`, another day's
  at `00:00:0x`. About a quarter of a 30-day window is affected and the raw sum
  overstates it by ~30x (a single "5-minute" Windows job appearing to run for
  33 hours). Two physical ceilings repair those rows: a job cannot outlive its
  run, so its end clamps to the run's `stopped`; and it cannot outlive the
  `timeout-minutes` in `.forgejo/workflows/ci.yml`, which is why exact
  7201-second rows are real timeout kills and anything past that is not. What
  survives both ceilings (~0.3%) has no recoverable end time and is imputed
  from that runner's median, counted in the `imputed` column rather than
  hidden.

  **Zero timestamps end a paged walk early.** Runs that never dispatched carry
  `0001-01-01`, so a cutoff test against the *tail* of an id-descending page
  stops hundreds of runs short — that silently halved the run window here while
  the task window stayed full, which inflates every job whose run cannot be
  found. The test reads the head of the page instead.

  **`limit` caps at 50** regardless of what is asked for, and the server does
  not honour `Accept-Encoding: gzip`, so a month is ~120 task pages of 23 KB
  and ~30 run pages of 496 KB (a run row re-serializes the whole repository
  object and the push `event_payload`). Bytes on a cold window are not
  negotiable; the number of round trips is. Pages are fetched `--jobs` at a
  time after a serial probe of page 1, and every terminal row is cached under
  `build/ci-time-cache.json`, keeping only the eight fields this script reads.
  A month costs 154 requests / 23 MB / ~18 s cold and 2 requests / 0.5 MB /
  ~1 s against a warm cache; widening `--days` past what the cache covers
  re-walks. Raising `--jobs` past 8 does not pay -- 16 measured *slower*
  (21 s), the forge being the bottleneck while it is also serving runners.
  The cache is additionally *more* accurate than the API: it preserves end
  times recorded before the retention sweep destroyed them, so a window run
  regularly degrades more slowly than one reconstructed cold.

Usage:

    tools/ci_time.py --days 30
    tools/ci_time.py --days 30 --by week      # also: day, runner, branch
    tools/ci_time.py --days 30 --no-cache     # ignore and do not write the cache
    tools/ci_time.py --self-test              # duration repair, no network

Authentication reuses the git credential for the forge (the same token `fj`
uses); no separate setup.
"""
import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import urllib.request
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timedelta, timezone

HOST = "code.buster14a.com"
REPO = "buster/buster"
WORKFLOW = ".forgejo/workflows/ci.yml"
CACHE = "build/ci-time-cache.json"
DEFAULT_TIMEOUT_S = 120 * 60
PAGE = 50                      # the API caps `limit` here whatever is asked
TERMINAL = ("success", "failure", "cancelled", "skipped")

# Only the fields this script reads are cached; a raw run row is ~10 KB of
# repository blob and push payload that nothing here looks at.
TASK_FIELDS = ("id", "name", "status", "created_at", "run_started_at", "updated_at",
               "run_number", "head_branch")
RUN_FIELDS = ("index_in_repo", "status", "created", "started", "stopped")

SERIES = {
    # endpoint: (identity field, timestamp field to page by, cached fields)
    "actions/tasks": ("id", "created_at", TASK_FIELDS),
    "actions/runs": ("index_in_repo", "created", RUN_FIELDS),
}


def job_timeout_seconds():
    """The `timeout-minutes` ceiling a job cannot exceed, read from the workflow."""
    try:
        with open(WORKFLOW) as f:
            minutes = [int(m) for m in re.findall(r"^\s*timeout-minutes:\s*(\d+)", f.read(), re.M)]
        return max(minutes) * 60 if minutes else DEFAULT_TIMEOUT_S
    except OSError:
        return DEFAULT_TIMEOUT_S


def repaired_duration(start, end, run_stop, timeout_s):
    """Seconds the job ran, or None when neither ceiling can recover its end.

    `end` is `task.updated_at`, which the log-retention sweep may have moved
    hours or days past the truth; `run_stop` is the run's `stopped`, which the
    job cannot outlive.
    """
    if start is None or end is None:
        return None
    if run_stop is not None and start <= run_stop < end:
        end = run_stop
    seconds = (end - start).total_seconds()
    return seconds if 0 <= seconds <= timeout_s else None


def stamp(text):
    """Parse an API timestamp, treating the zero date as absent."""
    when = datetime.fromisoformat(text) if text else None
    return when if when is not None and when.year > 2000 else None


class Forge:
    """Paged reads against the Actions API, fetched `jobs` pages at a time."""

    def __init__(self, token, jobs):
        self.token = token
        self.jobs = jobs
        self.requests = 0
        self.bytes = 0

    def page(self, path, number):
        request = urllib.request.Request(
            "https://%s/api/v1/repos/%s/%s?limit=%d&page=%d" % (HOST, REPO, path, PAGE, number),
            headers={"Authorization": "token " + self.token})
        body = urllib.request.urlopen(request, timeout=30).read()
        self.requests += 1
        self.bytes += len(body)
        return json.loads(body).get("workflow_runs") or []

    def walk(self, path, cutoff, cached):
        """Rows newer than `cutoff`, merged over `cached` (id -> row).

        Two stop conditions, tested against the *head* of a page because tails
        carry zero timestamps for runs that never dispatched: the page predates
        the window, or -- when the cache already reaches past `cutoff` -- the
        page has descended into ids the cache already holds.
        """
        key, when, fields = SERIES[path]
        floor = min((stamp(r[when]) for r in cached.values() if stamp(r[when])), default=None)
        newest_cached = max(cached, default=None) if floor and floor <= cutoff else None

        def spent(rows):
            head = stamp(rows[0].get(when))
            if head is not None and head < cutoff:
                return True
            return newest_cached is not None and rows[0][key] <= newest_cached

        def take(rows):
            """Absorb one page; True when the walk has reached its stop."""
            if not rows:
                return True
            fresh.update({r[key]: {f: r.get(f) for f in fields} for r in rows})
            return spent(rows)

        # Page 1 alone first: a warm cache usually stops right here, and a
        # speculative batch would have cost eight run pages at ~496 KB each.
        fresh = {}
        done = take(self.page(path, 1))
        page = 2
        with ThreadPoolExecutor(max_workers=self.jobs) as pool:
            while not done:
                for rows in list(pool.map(lambda n: self.page(path, n),
                                          range(page, page + self.jobs))):
                    if take(rows):
                        done = True
                        break
                page += self.jobs

        merged = dict(cached)
        merged.update(fresh)
        return ([r for r in merged.values() if (stamp(r.get(when)) or cutoff) >= cutoff],
                {i: r for i, r in merged.items() if r.get("status") in TERMINAL})


def load_cache(use_cache):
    if not use_cache or not os.path.exists(CACHE):
        return {p: {} for p in SERIES}
    try:
        with open(CACHE) as f:
            stored = json.load(f)
    except (OSError, ValueError):
        return {p: {} for p in SERIES}
    # JSON object keys are strings; the ids they hold are integers.
    return {p: {int(i): r for i, r in stored.get(p, {}).items()} for p in SERIES}


def save_cache(cache):
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    with open(CACHE, "w") as f:
        json.dump({p: {str(i): r for i, r in rows.items()} for p, rows in cache.items()}, f)


def forge_token():
    filled = subprocess.run(["git", "credential", "fill"],
                            input="protocol=https\nhost=%s\n\n" % HOST,
                            capture_output=True, text=True)
    for line in filled.stdout.splitlines():
        if line.startswith("password="):
            return line[len("password="):]
    sys.exit("no git credential for %s — see AGENTS.md, 'Forge, issues, and pull requests'" % HOST)


def bucket_of(task, start, by):
    if by == "runner":
        return task["name"]
    if by == "day":
        return start.astimezone().strftime("%Y-%m-%d")
    if by == "week":
        return start.astimezone().strftime("%G-W%V")
    return task.get("head_branch") or "?"


def report(tasks, runs, by, timeout_s, days, cutoff):
    stopped = {r["index_in_repo"]: stamp(r.get("stopped")) for r in runs}

    measured = defaultdict(list)          # runner -> durations that survived both ceilings
    rows = []
    for task in tasks:
        start = stamp(task.get("run_started_at"))
        if start is None:
            continue
        seconds = repaired_duration(start, stamp(task.get("updated_at")),
                                    stopped.get(task.get("run_number")), timeout_s)
        if seconds is not None:
            measured[task["name"]].append(seconds)
        rows.append((bucket_of(task, start, by), task["name"], seconds))

    median = {runner: statistics.median(v) for runner, v in measured.items() if v}
    overall = statistics.median([d for v in measured.values() for d in v] or [0])

    totals = defaultdict(lambda: [0, 0.0, 0, []])   # jobs, seconds, imputed, measured
    for key, runner, seconds in rows:
        entry = totals[key]
        entry[0] += 1
        if seconds is None:
            entry[1] += median.get(runner, overall)
            entry[2] += 1
        else:
            entry[1] += seconds
            entry[3].append(seconds)

    print("buster CI, %g days back to %s" % (days, cutoff.astimezone().strftime("%Y-%m-%d %H:%M")))
    print("%-28s%7s%9s%9s%9s%9s" % ("", "jobs", "hours", "med min", "avg min", "imputed"))
    order = sorted(totals.items(), key=lambda kv: -kv[1][1]) if by in ("runner", "branch") \
        else sorted(totals.items())
    for key, (jobs, seconds, imputed, values) in order:
        print("%-28s%7d%9.1f%9.1f%9.1f%9d"
              % (key[:27], jobs, seconds / 3600,
                 statistics.median(values) / 60 if values else 0, seconds / jobs / 60, imputed))
    jobs = sum(v[0] for v in totals.values())
    seconds = sum(v[1] for v in totals.values())
    print("%-28s%7d%9.1f%9s%9.1f%9d"
          % ("TOTAL runner time", jobs, seconds / 3600, "",
             seconds / max(jobs, 1) / 60, sum(v[2] for v in totals.values())))

    # Push latency is never polluted: a run's own start and stop are not swept.
    latency = sorted(
        seconds for seconds in
        ((stamp(r.get("stopped")) - stamp(r.get("started"))).total_seconds()
         for r in runs if stamp(r.get("stopped")) and stamp(r.get("started")))
        if 0 < seconds <= timeout_s)
    if latency:
        print("\npush latency, %d runs: total %.1f h, median %.1f min, mean %.1f min, p90 %.1f min"
              % (len(latency), sum(latency) / 3600, latency[len(latency) // 2] / 60,
                 sum(latency) / len(latency) / 60, latency[int(len(latency) * 0.9)] / 60))


def self_test():
    """Cover the duration repair — the part three separate attempts got wrong."""
    base = datetime(2026, 8, 11, 13, 7, 39, tzinfo=timezone.utc)
    minute = timedelta(minutes=1)
    timeout_s = 7200

    # an untouched row is its own duration
    assert repaired_duration(base, base + 7 * minute, base + 8 * minute, timeout_s) == 420
    # a swept row (end 35 hours later) clamps to the run's stop
    assert repaired_duration(base, base + timedelta(hours=35), base + 8 * minute, timeout_s) == 480
    # ... and is unrecoverable when the run's stop was swept too
    assert repaired_duration(base, base + timedelta(hours=35),
                             base + timedelta(hours=40), timeout_s) is None
    # a run that never stopped leaves the task's own end in place
    assert repaired_duration(base, base + 7 * minute, None, timeout_s) == 420
    # a timeout kill is real and survives
    assert repaired_duration(base, base + timedelta(seconds=7201), None, 7201) == 7201
    # a clamp never runs backwards off a run that stopped before the job started
    assert repaired_duration(base, base + 7 * minute, base - minute, timeout_s) == 420
    assert repaired_duration(base, None, None, timeout_s) is None
    assert stamp("0001-01-01T00:00:00+00:00") is None
    print("self-test ok")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--days", type=float, default=30, help="window size (default 30)")
    parser.add_argument("--by", default="runner", choices=["runner", "day", "week", "branch"])
    parser.add_argument("--jobs", type=int, default=8, help="pages to fetch at a time")
    parser.add_argument("--no-cache", action="store_true", help="ignore and do not write %s" % CACHE)
    parser.add_argument("--self-test", action="store_true", help="check the repair, no network")
    args = parser.parse_args()

    if args.self_test:
        self_test()
        return

    timeout_s = job_timeout_seconds()
    cache = load_cache(not args.no_cache)
    forge = Forge(forge_token(), max(1, args.jobs))
    cutoff = datetime.now(timezone.utc) - timedelta(days=args.days)

    runs, cache["actions/runs"] = forge.walk("actions/runs", cutoff, cache["actions/runs"])
    tasks, cache["actions/tasks"] = forge.walk("actions/tasks", cutoff, cache["actions/tasks"])
    if not args.no_cache:
        save_cache(cache)

    report(tasks, runs, args.by, timeout_s, args.days, cutoff)
    print("%d requests, %.1f MB" % (forge.requests, forge.bytes / 1e6))


main()
