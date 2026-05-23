#!/usr/bin/env python3
import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path


ANSI_ESCAPE_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
DIAGNOSTIC_WARNING_RE = re.compile(r"(^|\n).*?:\d+:\d+: warning:")


OPTIONS_WITH_SEPARATE_ARGUMENT = {
    "-MF",
    "-MJ",
    "-MQ",
    "-MT",
    "-o",
    "--output",
    "-dependency-file",
}

OPTIONS_WITH_JOINED_ARGUMENT = (
    "-MF",
    "-MJ",
    "-MQ",
    "-MT",
    "-o",
    "--output=",
    "-dependency-file=",
)

OPTIONS_TO_DROP = {
    "-c",
    "-fcolor-diagnostics",
    "-MD",
    "-MMD",
    "-MP",
}


def parse_arguments(argv):
    parser = argparse.ArgumentParser(
        allow_abbrev=False,
        description="Run clang --analyze over a CMake compile_commands.json database.",
    )
    parser.add_argument(
        "compile_commands",
        help="Path to compile_commands.json, or a build directory containing it.",
    )
    parser.add_argument(
        "--config",
        default=None,
        help="Analyze only entries for this CMake configuration when present.",
    )
    parser.add_argument(
        "--clang",
        default=None,
        help="Clang executable to use. Defaults to the compiler recorded in each entry.",
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Only print analyzer commands that produce diagnostics or fail.",
    )
    return parser.parse_args(argv)


def command_string(command):
    return " ".join(shlex.quote(str(argument)) for argument in command)


def compile_commands_path(path_argument):
    path = Path(path_argument)
    if path.is_dir():
        path = path / "compile_commands.json"
    return path


def entry_arguments(entry):
    if "arguments" in entry:
        return list(entry["arguments"])

    command = entry.get("command")
    if command is None:
        raise ValueError("compile command entry has neither 'arguments' nor 'command'")

    return shlex.split(command, posix=(os.name != "nt"))


def normalized_config(config):
    if config is None:
        return None
    config = config.strip()
    if not config:
        return None
    return config


def entry_matches_config(entry, arguments, config):
    if config is None:
        return True

    candidates = []
    output = entry.get("output")
    if output:
        candidates.append(output)
    candidates.extend(arguments)

    for candidate in candidates:
        text = str(candidate)
        parts = Path(text.replace("\\", "/")).parts
        if config in parts:
            return True
        if f"CMAKE_INTDIR={config}" in text or f"CMAKE_INTDIR=\\\"{config}\\\"" in text:
            return True

    return False


def is_c_source(path):
    return Path(path).suffix.lower() == ".c"


def skip_option(argument, next_argument):
    if argument in OPTIONS_TO_DROP:
        return 1
    if argument in OPTIONS_WITH_SEPARATE_ARGUMENT:
        return 2 if next_argument is not None else 1
    for prefix in OPTIONS_WITH_JOINED_ARGUMENT:
        if argument.startswith(prefix) and argument != prefix:
            return 1
    return 0


def analyzer_command(entry, arguments, clang):
    if not arguments:
        raise ValueError("empty compile command")

    compiler = clang if clang is not None else arguments[0]
    result = [
        compiler,
        "--analyze",
        "-Xanalyzer",
        "-analyzer-output=text",
        "-fno-color-diagnostics",
        "-Wno-error=unused-command-line-argument",
    ]

    index = 1
    while index < len(arguments):
        argument = arguments[index]
        next_argument = arguments[index + 1] if index + 1 < len(arguments) else None
        skipped = skip_option(argument, next_argument)
        if skipped:
            index += skipped
            continue
        result.append(argument)
        index += 1

    return result


def run_analyzer(command, cwd, quiet):
    if not quiet:
        print(f"+ {command_string(command)}", flush=True)

    completed = subprocess.run(
        command,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    output = ANSI_ESCAPE_RE.sub("", completed.stdout + completed.stderr)
    has_warning = DIAGNOSTIC_WARNING_RE.search(output) is not None

    if quiet and (completed.returncode != 0 or has_warning):
        print(f"+ {command_string(command)}", flush=True)

    if completed.stdout:
        print(completed.stdout, end="")
    if completed.stderr:
        print(completed.stderr, end="", file=sys.stderr)

    return completed.returncode, has_warning


def main(argv):
    arguments = parse_arguments(argv)
    config = normalized_config(arguments.config)
    path = compile_commands_path(arguments.compile_commands)

    if not path.exists():
        print(f"error: compile commands not found: {path}", file=sys.stderr)
        return 2

    try:
        entries = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"error: failed to read {path}: {error}", file=sys.stderr)
        return 2

    analyzed = 0
    warnings = 0
    failures = 0

    for entry in entries:
        file = entry.get("file")
        if file is None or not is_c_source(file):
            continue

        try:
            compile_arguments = entry_arguments(entry)
        except ValueError as error:
            print(f"error: {file}: {error}", file=sys.stderr)
            failures += 1
            continue

        if not entry_matches_config(entry, compile_arguments, config):
            continue

        command = analyzer_command(entry, compile_arguments, arguments.clang)
        returncode, has_warning = run_analyzer(command, entry.get("directory"), arguments.quiet)
        analyzed += 1
        if returncode != 0:
            failures += 1
        if has_warning:
            warnings += 1

    if analyzed == 0:
        config_text = f" for configuration {config}" if config is not None else ""
        print(f"error: no C compile commands found{config_text} in {path}", file=sys.stderr)
        return 2

    print(
        f"clang --analyze checked {analyzed} translation unit(s), "
        f"{warnings} with analyzer warning(s), {failures} failed.",
        flush=True,
    )

    if failures:
        return 1
    if warnings:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
