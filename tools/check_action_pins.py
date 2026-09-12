#!/usr/bin/env python3
"""Check the repository's restricted, block-style GitHub action references.

This is a policy scanner, not a YAML parser. Action steps use a plain `uses`
key and a single-line scalar. Nonempty flow mappings, mapping anchors/aliases, quoted
keys and multiline action references are rejected rather than interpreted.
Run-script block contents are data and are skipped. The allowlist is updated
only after source review, together with docs/ci-action-pins.md.
"""

import pathlib
import re
import sys


APPROVED = {
    "actions/checkout": {"11bd71901bbe5b1630ceea73d27597364c9af683"},
    "actions/upload-artifact": {"ea165f8d65b6e75b540449e92b4886f43607fa02"},
    "actions/cache/restore": {"0057852bfaa89a56745cba8c7296529d2fc39830"},
    "actions/cache/save": {"0057852bfaa89a56745cba8c7296529d2fc39830"},
}
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


def main(arguments):
    root = pathlib.Path(__file__).resolve().parents[1]
    paths = [pathlib.Path(argument) for argument in arguments]
    if not paths:
        paths = sorted((root / ".github/workflows").glob("*.yml"))
        paths += sorted((root / ".github/workflows").glob("*.yaml"))
    errors = []
    if not paths:
        errors.append("no GitHub workflows found")
    for path in paths:
        try:
            errors.extend(check_text(path.read_text(encoding="utf-8"), path))
        except (OSError, UnicodeError) as error:
            errors.append(f"{path}: {error}")
    for error in errors:
        print(error, file=sys.stderr)
    if not errors:
        print(f"Approved action references in {len(paths)} GitHub workflows")
    return int(bool(errors))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
