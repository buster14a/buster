#!/usr/bin/env python3
"""Check restricted block-style references in workflows and approved local actions.

This is a policy scanner, not a YAML parser. Action steps use a plain `uses`
key and a single-line scalar. Nonempty flow mappings, mapping anchors/aliases, quoted
keys and multiline action references are rejected rather than interpreted.
Run-script block contents are data and are skipped. The allowlist is updated
only after source review, together with docs/ci-action-pins.md.
"""

import hashlib
import pathlib
import re
import sys


MACHINE_REPORTER_REFERENCE = "buster14a/buster/.github/actions/machine-specifications@a36422384d0334a53d4be73bc306b97ccdba4768"
MACHINE_REPORTER_BLOBS = {".github/actions/machine-specifications/action.yml":"ca7b8666a9cc5fedc1189fd09cc666863f90571a","tools/machine_specifications.c":"db34702e5adb3ddce3ab9c0dc0d93a4ef21c81ff"}

APPROVED = {
    "buster14a/buster/.github/actions/machine-specifications": {"a36422384d0334a53d4be73bc306b97ccdba4768"},
    "actions/upload-pages-artifact": {"fc324d3547104276b827a68afc52ff2a11cc49c9"},
    "actions/deploy-pages": {"368f82528645a54fb793d4d04e342629a3f51346"},
    "actions/checkout": {"11bd71901bbe5b1630ceea73d27597364c9af683"},
    "actions/upload-artifact": {"ea165f8d65b6e75b540449e92b4886f43607fa02", "043fb46d1a93c77aae656e7c1c64a875d1fc6a0a"},
    "actions/cache/restore": {"0057852bfaa89a56745cba8c7296529d2fc39830"},
    "actions/cache/save": {"0057852bfaa89a56745cba8c7296529d2fc39830"},
}
# GitHub binds this literal reusable workflow to the caller's own commit.
# Local composite actions are allowed only by exact path and are scanned below.
APPROVED_LOCAL_WORKFLOWS = {"./.github/workflows/throughput-real-source.yml"}
# Keep the pinned apt qualification's frozen local-workflow contract intact.
APPROVED_COMPILER_THROUGHPUT_WORKFLOW = "./.github/workflows/compiler-throughput.yml"
APPROVED_LOCAL_ACTIONS = {"./.github/actions/native-artifact-upload"}
ACTION = re.compile(r"\s*(?:-\s+)?uses:\s*(.*?)\s*$")
BLOCK = re.compile(r"\s*(?:-\s+)?[A-Za-z_][A-Za-z0-9_-]*:\s*[|>][-+]?\s*$")
QUOTED_KEY = re.compile(r"\s*(?:-\s+)?[\"']")
QUOTED_SCALAR = r"(?:'[^']*(?:''[^']*)*'|\"(?:[^\"\\]|\\.)*\")"
LIST_SCALAR = rf"(?:{QUOTED_SCALAR}|[A-Za-z0-9_.-]+(?: +[A-Za-z0-9_.-]+)*)"
SCALAR_LIST = re.compile(rf"\s*(?:-\s+)?[A-Za-z_][A-Za-z0-9_-]*:\s*\[\s*(?:{LIST_SCALAR}(?:\s*,\s*{LIST_SCALAR})*\s*,?\s*)?\]\s*$")
QUOTED_SEQUENCE_ITEM = re.compile(rf"\s*-\s+{QUOTED_SCALAR}\s*$")
EMPTY_MAPPING = re.compile(r"\s*(?:-\s+)?[A-Za-z_][A-Za-z0-9_-]*:\s*\{\s*\}\s*$")


def without_comment(line):
    quote = None
    escaped = False
    end = len(line)
    for index, character in enumerate(line):
        if escaped:
            escaped = False
        elif quote == '"' and character == "\\":
            escaped = True
        elif quote:
            if character == quote:
                quote = None
        elif character in "'\"":
            quote = character
        elif character == "#" and (index == 0 or line[index - 1].isspace()):
            end = index
            break
    return line[:end].rstrip()


def check_text(text, path):
    errors = []
    block_indent = None
    for number, raw in enumerate(text.splitlines(), 1):
        indent = len(raw) - len(raw.lstrip(" "))
        if block_indent is not None and (not raw.strip() or indent > block_indent):
            continue
        block_indent = None
        line = without_comment(raw)
        if not line.strip():
            continue
        # Expression operators are not YAML tags or flow collections. Keep the
        # original action value below so expressions can never select an action.
        structural = re.sub(r"\$\{\{.*?\}\}", "EXPRESSION", line)
        problem = None
        if "\t" in raw[:len(raw) - len(raw.lstrip())]:
            problem = "tabs are not allowed in workflow indentation"
        elif QUOTED_KEY.match(line) and not QUOTED_SEQUENCE_ITEM.fullmatch(line):
            problem = "mapping keys must be plain scalars"
        elif re.match(r"\s*(?:-\s+)?[?:](?:\s|$)", line):
            problem = "explicit mapping keys are not supported"
        elif re.search(r"(?:^|\s)![!A-Za-z<]", structural):
            problem = "YAML tags are not supported"
        elif re.search(r"(?:^|\s)[&*][A-Za-z0-9_-]+|(?:^|\s)<<\s*:", structural):
            problem = "mapping anchors, aliases and merges are not supported"
        else:
            if ("{" in structural or "}" in structural) and not EMPTY_MAPPING.fullmatch(structural):
                problem = "flow mappings are not supported"
            elif ("[" in structural or "]" in structural) and not SCALAR_LIST.fullmatch(structural):
                problem = "flow sequences must be simple lists of scalar values"
        action = ACTION.fullmatch(line)
        if action and problem is None:
            value = action.group(1)
            if len(value) >= 2 and value[0] in "'\"" and value[-1] == value[0]:
                value = value[1:-1]
            if value not in APPROVED_LOCAL_WORKFLOWS | {APPROVED_COMPILER_THROUGHPUT_WORKFLOW} | APPROVED_LOCAL_ACTIONS:
                match = re.fullmatch(r"([A-Za-z0-9_.-]+/[A-Za-z0-9_./-]+)@([0-9a-f]{40})", value)
                if not match:
                    problem = "uses must contain a GitHub owner/repository action path and full lowercase commit SHA"
                elif match.group(2) not in APPROVED.get(match.group(1), set()):
                    problem = "action path and revision have not been reviewed"
        elif re.search(r"\buses\s*:", line) and problem is None:
            problem = "uses must be a plain block-mapping key"
        if problem:
            errors.append(f"{path}:{number}: {problem}")
        elif not action and BLOCK.fullmatch(line):
            block_indent = indent
    return errors



def structure_lines(text):
    """Existing restricted block YAML contract, excluding script/scalar data."""
    result = []
    block_indent = None
    for raw in text.splitlines():
        indent = len(raw) - len(raw.lstrip(" "))
        if block_indent is not None and (not raw.strip() or indent > block_indent):
            continue
        block_indent = None
        line = without_comment(raw)
        if line.strip():
            result.append(line)
            if BLOCK.fullmatch(line):
                block_indent = indent
    return result


def check_machine_reporting(text, path):
    """Every runs-on job starts with the exact reviewed collector, never a name."""
    errors = []
    lines = structure_lines(text)
    if "jobs:" not in lines:
        return errors
    jobs = {}
    current = None
    for line in lines[lines.index("jobs:") + 1:]:
        header = re.fullmatch(r"  ([A-Za-z0-9_-]+):", line)
        if header:
            current = jobs.setdefault(header.group(1), [])
        elif line and not line.startswith(" "):
            current = None
        elif current is not None:
            current.append(line)
    for job, body in jobs.items():
        prefix = f"{path}: job {job}: "
        executing = any(line.startswith("    runs-on:") for line in body)
        runnerless = any(line.startswith("    uses:") for line in body)
        if executing == runnerless:
            errors.append(prefix + "must be one runs-on job or one reusable-workflow caller")
            continue
        if runnerless:
            if any(line.startswith("    steps:") for line in body):
                errors.append(prefix + "runnerless caller cannot have steps or claim its own machine")
            continue
        if "    steps:" not in body:
            errors.append(prefix + "executing job has no startup reporter")
            continue
        parts = []
        current_step = None
        for line in body[body.index("    steps:") + 1:]:
            if line.startswith("      - "):
                current_step = []
                parts.append(current_step)
            if current_step is not None:
                current_step.append(line)
        if not parts:
            errors.append(prefix + "no executing steps")
            continue
        first = parts[0]
        if (first[0] != "      - name: Machine specifications" or
                "        uses: " + MACHINE_REPORTER_REFERENCE not in first or
                "          requested-runner: >-" not in first or
                any(line.startswith(("        if:", "        continue-on-error:", "        run:",
                                     "          mode:", "        env:")) for line in first)):
            errors.append(prefix + "first step must unconditionally invoke the reviewed startup reporter")
        startup_count = 0
        for index, step in enumerate(parts):
            reporter = "        uses: " + MACHINE_REPORTER_REFERENCE in step
            if reporter and not any(line in step for line in ("          mode: source", "          mode: retain")):
                startup_count += 1
                if index != 0:
                    errors.append(prefix + "startup reporting must precede all work")
            if reporter and any(line.startswith(("        env:", "        continue-on-error:")) for line in step):
                errors.append(prefix + "reporter cannot override environment or ignore failure")
            checkout = any(re.fullmatch(r"\s*(?:- )?uses: actions/checkout@[0-9a-f]{40}", line) for line in step)
            if checkout:
                following = parts[index + 1] if index + 1 < len(parts) else []
                condition = [line for line in step if line.startswith("        if:")]
                following_condition = [line for line in following if line.startswith("        if:")]
                source_directory = next((line.strip().split(": ", 1)[1]
                                         for line in step if line.startswith("          path: ")), ".")
                if ("        uses: " + MACHINE_REPORTER_REFERENCE not in following or
                        "          mode: source" not in following or
                        "          source-directory: " + source_directory not in following or
                        condition != following_condition):
                    errors.append(prefix + "every checkout needs immediate matching actual-source reporting")
        if startup_count != 1:
            errors.append(prefix + "requires exactly one startup report")
    return errors


def check_machine_reporter_implementation(root):
    errors = []
    for relative, expected in MACHINE_REPORTER_BLOBS.items():
        path = root / relative
        try:
            raw = path.read_bytes()
            digest = hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()
            if digest != expected:
                errors.append(f"{relative}: differs from reviewed reporter pin; refresh the immutable pin")
        except OSError as error:
            errors.append(f"{relative}: required reporter implementation is unavailable: {error}")
    return errors


def main(arguments):
    root = pathlib.Path(__file__).resolve().parents[1]
    paths = [pathlib.Path(argument) for argument in arguments]
    if not paths:
        paths = sorted((root / ".github/workflows").glob("*.yml"))
        paths += sorted((root / ".github/workflows").glob("*.yaml"))
        paths += sorted((root / ".github/actions").rglob("action.yml"))
        paths += sorted((root / ".github/actions").rglob("action.yaml"))
    errors = []
    if not paths:
        errors.append("no GitHub workflows found")
    for path in paths:
        try:
            text = path.read_text(encoding="utf-8")
            errors.extend(check_text(text, path))
            errors.extend(check_machine_reporting(text, path))
        except (OSError, UnicodeError) as error:
            errors.append(f"{path}: {error}")
    errors.extend(check_machine_reporter_implementation(root))
    for error in errors:
        print(error, file=sys.stderr)
    if not errors:
        print(f"Approved action references in {len(paths)} GitHub workflows/action definitions")
    return int(bool(errors))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
