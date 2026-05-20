#!/usr/bin/env python3
import argparse
import json
import sys
from collections import defaultdict


DEFAULT_LIMIT = 25


def positive_integer(value):
    result = int(value, 10)
    if result <= 0:
        raise argparse.ArgumentTypeError(f"expected a positive integer, got {value!r}")
    return result


def parse_arguments(argv):
    parser = argparse.ArgumentParser(
        allow_abbrev=False,
        description="Print the slowest entries from a CMake google-trace profile.",
    )
    parser.add_argument("profile", help="CMake profiling JSON file.")
    parser.add_argument(
        "--limit",
        default=DEFAULT_LIMIT,
        type=positive_integer,
        metavar="N",
        help="Number of entries to print.",
    )
    return parser.parse_args(argv)


def summarize_profile(events):
    stacks = defaultdict(list)
    rows = []

    for event in events:
        key = (event.get("pid"), event.get("tid"))
        phase = event.get("ph")

        if phase == "B":
            stacks[key].append(event)
        elif phase == "E" and stacks[key]:
            start = stacks[key].pop()
            duration_ms = (event["ts"] - start["ts"]) / 1000.0
            arguments = start.get("args") or {}
            rows.append(
                (
                    duration_ms,
                    start.get("cat", ""),
                    start.get("name", ""),
                    arguments.get("location", ""),
                    arguments.get("functionArgs", ""),
                )
            )

    rows.sort(reverse=True)
    return rows


def main(argv):
    arguments = parse_arguments(argv)

    try:
        with open(arguments.profile, encoding="utf-8") as profile_file:
            events = json.load(profile_file)
    except OSError as error:
        print(f"error: failed to read {arguments.profile}: {error}", file=sys.stderr)
        return 1
    except json.JSONDecodeError as error:
        print(f"error: failed to parse {arguments.profile}: {error}", file=sys.stderr)
        return 1

    rows = summarize_profile(events)
    if not rows:
        print("No complete CMake profiling events found.")
        return 0

    print("Slowest CMake configure/generate entries:")
    for duration_ms, category, name, location, function_arguments in rows[: arguments.limit]:
        print(
            f"{duration_ms:8.3f} ms  "
            f"{category[:10]:10} "
            f"{name[:30]:30} "
            f"{location} "
            f"{function_arguments[:100]}"
        )

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
