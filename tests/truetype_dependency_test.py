#!/usr/bin/env python3
"""Verify the headless compiler's TrueType dependency boundary."""

import argparse
from collections import Counter
import json
import os
from pathlib import Path
import shlex
import subprocess


ROOT = Path(__file__).resolve().parents[1]
TRUETYPE_SOURCE = (ROOT / "src/buster/lib/truetype.c").resolve()
IDE_SOURCE = (ROOT / "src/buster/apps/ide/ide.c").resolve()


def run(arguments, **kwargs):
    print("+", shlex.join(str(argument) for argument in arguments), flush=True)
    return subprocess.run([str(argument) for argument in arguments], check=True, **kwargs)


def command_arguments(entry):
    arguments = entry.get("arguments")
    if arguments is None:
        arguments = shlex.split(entry["command"], posix=os.name != "nt")
    return arguments


def entry_configuration(entry):
    command = " ".join(command_arguments(entry)).replace("\\", "/")
    for configuration in ("Debug", "Release"):
        if (f'CMAKE_INTDIR="{configuration}"' in command or
                f"CMAKE_INTDIR={configuration}" in command or
                f"/{configuration}/" in command):
            return configuration
    raise AssertionError(f"cannot identify configuration for {entry['file']}: {command}")


def source_entries(database, source):
    return [entry for entry in database if Path(entry["file"]).resolve() == source]


def is_source_argument(argument, directory, source):
    path = Path(argument)
    if not path.is_absolute():
        path = Path(directory) / path
    return not argument.startswith("-") and path.resolve() == source


def preprocess_entry(entry, output):
    arguments = command_arguments(entry)
    filtered = []
    skip = False
    options_with_values = {"-MF", "-MJ", "-MQ", "-MT", "-o"}
    source = Path(entry["file"]).resolve()
    for argument in arguments:
        if skip:
            skip = False
        elif argument in options_with_values:
            skip = True
        elif argument in {"-c", "-MD", "-MMD", "-MP"} or is_source_argument(argument, entry["directory"], source):
            continue
        elif argument.startswith(("-MF", "-MJ", "-MQ", "-MT")):
            continue
        elif argument.startswith("-o") and argument != "-o":
            continue
        else:
            filtered.append(argument)
    filtered.extend(["-E", str(source)])
    with output.open("wb") as stream:
        run(filtered, cwd=entry["directory"], stdout=stream)


def verify_configuration(driver, output_root, include_tests, unity_requested):
    label = f"tests-{'on' if include_tests else 'off'}-unity-{'on' if unity_requested else 'off'}"
    build = output_root / label
    run([
        driver,
        "generate",
        "--build-directory", build,
        "--cc", "clang",
        "--ci",
        "--no-sanitize",
        "--no-fuzz",
        "--no-lto",
        "--linker", "DEFAULT",
        "--",
        f"-DBUSTER_INCLUDE_TESTS={'ON' if include_tests else 'OFF'}",
        f"-DBUSTER_UNITY_BUILD={'ON' if unity_requested else 'OFF'}",
    ], cwd=ROOT)

    database = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    truetype_configurations = Counter(entry_configuration(entry) for entry in source_entries(database, TRUETYPE_SOURCE))
    expected_split = Counter()
    if include_tests:
        expected_split["Debug"] = 1
        if not unity_requested:
            expected_split["Release"] = 1
    if truetype_configurations != expected_split:
        raise AssertionError(
            f"{label}: truetype.c split configurations {dict(truetype_configurations)}, "
            f"expected {dict(expected_split)}")

    ide_entries = source_entries(database, IDE_SOURCE)
    configurations = Counter(entry_configuration(entry) for entry in ide_entries)
    if configurations != Counter({"Debug": 1, "Release": 1}):
        raise AssertionError(f"{label}: ide.c configurations are {dict(configurations)}")

    if not include_tests and not unity_requested:
        for configuration in ("Debug", "Release"):
            run([
                driver,
                "build",
                "--build-directory", build,
                "--config", configuration,
                "-t", "ide",
                "--", "-j2",
            ], cwd=ROOT)

    if unity_requested:
        release = next(entry for entry in ide_entries if entry_configuration(entry) == "Release")
        arguments = " ".join(command_arguments(release))
        if "BUSTER_UNITY_BUILD=1" not in arguments:
            raise AssertionError(f"{label}: Release ide.c is not an effective unity compile")
        preprocessed = output_root / f"{label}.i"
        preprocess_entry(release, preprocessed)
        implementation_marker = "buster/lib/truetype.c"
        contains_implementation = implementation_marker in preprocessed.read_text(encoding="utf-8", errors="replace").replace("\\", "/")
        if contains_implementation != include_tests:
            raise AssertionError(
                f"{label}: preprocessed unity input contains truetype.c={contains_implementation}, "
                f"expected {include_tests}")

    unity_state = "not-selected" if not unity_requested else "retained" if include_tests else "omitted"
    print(
        "TRUETYPE_DEPENDENCY_V1"
        f" label={label}"
        f" split_debug={'retained' if truetype_configurations['Debug'] else 'omitted'}"
        f" split_release={'retained' if truetype_configurations['Release'] else 'omitted'}"
        f" unity_release={unity_state}",
        flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--driver", required=True, type=Path)
    parser.add_argument("--output-directory", required=True, type=Path)
    arguments = parser.parse_args()
    arguments.output_directory.mkdir(parents=True, exist_ok=False)
    for include_tests in (False, True):
        for unity_requested in (False, True):
            verify_configuration(arguments.driver.resolve(), arguments.output_directory, include_tests, unity_requested)


if __name__ == "__main__":
    main()
