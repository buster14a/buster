#!/usr/bin/env python3
"""List functions with more than one `return`, per file.

AGENTS.md's "One return per function" core rule counts returns the way the
compiler sees them: mutually exclusive `#if`/`#elif`/`#else` arms contribute the
count of their *heaviest* arm, because exactly one arm is ever compiled. This
scanner reproduces that accounting so a sweep can be measured rather than
guessed at.

Usage:
    tools/scan_multi_return.py [--min N] [--sort name|returns|lines] [paths...]
    tools/scan_multi_return.py --self-test

Paths default to `src`. Directories are walked for `.c` and `.h` files.
"""

import argparse
import os
import sys

IDENTIFIER_CHARACTERS = set(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"
)

# Braced things at file scope that are not function bodies: their signature text
# never ends in `)`.  A function definition always does, modulo attributes we do
# not use in this tree.
SOURCE_EXTENSIONS = (".c", ".h")


def blank_comments_and_literals(text):
    """Return `text` with comments and string/char literal bodies blanked out.

    Lengths and newlines are preserved so line numbers stay exact.  Blanking
    rather than deleting keeps the brace and `return` scans from tripping over a
    `"{"` inside a format string or a `// return` in a comment.
    """
    out = list(text)
    index = 0
    end = len(text)
    while index < end:
        character = text[index]
        if character == "/" and index + 1 < end and text[index + 1] == "/":
            while index < end and text[index] != "\n":
                out[index] = " "
                index += 1
        elif character == "/" and index + 1 < end and text[index + 1] == "*":
            out[index] = out[index + 1] = " "
            index += 2
            while index < end and not (
                text[index] == "*" and index + 1 < end and text[index + 1] == "/"
            ):
                if text[index] != "\n":
                    out[index] = " "
                index += 1
            if index < end:
                out[index] = out[index + 1] = " "
                index += 2
        elif character in "\"'":
            quote = character
            index += 1
            while index < end and text[index] != quote:
                if text[index] == "\\" and index + 1 < end:
                    out[index] = " "
                    index += 1
                if text[index] != "\n":
                    out[index] = " "
                index += 1
            if index < end:
                index += 1
        else:
            index += 1
    return "".join(out)


def logical_lines(text):
    """Split `text` into (line_number, content) pairs with splices folded out.

    A backslash-newline continuation joins into the line it continues, which is
    what makes a multi-line `#define` a single preprocessor line.
    """
    lines = text.split("\n")
    result = []
    index = 0
    while index < len(lines):
        start = index
        content = lines[index]
        while content.endswith("\\") and index + 1 < len(lines):
            content = content[:-1] + lines[index + 1]
            index += 1
        result.append((start + 1, content))
        index += 1
    return result


def count_returns(line):
    """Count `return` keyword occurrences in one line of blanked source."""
    total = 0
    start = 0
    while True:
        found = line.find("return", start)
        if found < 0:
            break
        before = line[found - 1] if found > 0 else ""
        after_index = found + len("return")
        after = line[after_index] if after_index < len(line) else ""
        if before not in IDENTIFIER_CHARACTERS and after not in IDENTIFIER_CHARACTERS:
            total += 1
        start = after_index
    return total


class ConditionalStack:
    """Accumulates return counts with `#if` arms scored as their heaviest arm."""

    def __init__(self):
        self.arm_counts = [0]
        self.group_maxima = []

    def add(self, count):
        self.arm_counts[-1] += count

    def open_group(self):
        self.group_maxima.append(0)
        self.arm_counts.append(0)

    def next_arm(self):
        if not self.group_maxima:
            return
        self.group_maxima[-1] = max(self.group_maxima[-1], self.arm_counts[-1])
        self.arm_counts[-1] = 0

    def close_group(self):
        if not self.group_maxima:
            return
        heaviest = max(self.group_maxima.pop(), self.arm_counts.pop())
        self.arm_counts[-1] += heaviest

    def total(self):
        # An unterminated group (one that spans the end of the function) still
        # contributes its heaviest arm so far.
        total = self.arm_counts[0]
        for depth, group_maximum in enumerate(self.group_maxima):
            total += max(group_maximum, self.arm_counts[depth + 1])
        return total


def function_name_before(signature):
    """Extract the function name from the signature text preceding its `{`."""
    text = signature.rstrip()
    if not text.endswith(")"):
        return None
    depth = 0
    index = len(text) - 1
    while index >= 0:
        if text[index] == ")":
            depth += 1
        elif text[index] == "(":
            depth -= 1
            if depth == 0:
                break
        index -= 1
    if index < 0:
        return None
    end = index
    while end > 0 and text[end - 1].isspace():
        end -= 1
    start = end
    while start > 0 and text[start - 1] in IDENTIFIER_CHARACTERS:
        start -= 1
    name = text[start:end]
    return name or None


def join_signature(existing, addition):
    """Append signature text, keeping a separator so tokens do not fuse."""
    addition = addition.strip()
    if not addition:
        return existing
    return (existing + " " + addition) if existing else addition


def scan_text(text):
    """Yield (name, start_line, end_line, returns) for every function body."""
    blanked = blank_comments_and_literals(text)
    functions = []
    depth = 0
    signature = ""
    start_line = 0
    name = None
    counter = None
    for line_number, line in logical_lines(blanked):
        stripped = line.lstrip()
        if stripped.startswith("#"):
            directive = stripped[1:].lstrip().split(" ", 1)[0].split("\t", 1)[0]
            if counter is not None:
                if directive in ("if", "ifdef", "ifndef"):
                    counter.open_group()
                elif directive in ("elif", "elifdef", "elifndef", "else"):
                    counter.next_arm()
                elif directive == "endif":
                    counter.close_group()
            continue
        # `body_start` tracks where the in-function part of this line begins, so
        # a one-line body (`int f(void) { return 0; }`) counts its return before
        # the closing brace retires the counter.
        body_start = 0 if counter is not None else None
        signature_start = 0 if depth == 0 else None
        for position, character in enumerate(line):
            if character == "{":
                if depth == 0:
                    if signature_start is not None:
                        signature = join_signature(
                            signature, line[signature_start:position]
                        )
                    name = function_name_before(signature)
                    if name is not None:
                        start_line = line_number
                        counter = ConditionalStack()
                        body_start = position + 1
                    signature_start = None
                depth += 1
            elif character == "}":
                depth -= 1
                if depth <= 0:
                    depth = 0
                    if counter is not None:
                        counter.add(count_returns(line[body_start:position]))
                        functions.append(
                            (name, start_line, line_number, counter.total())
                        )
                    name = None
                    counter = None
                    body_start = None
                    signature = ""
                    signature_start = position + 1
            elif character == ";" and depth == 0:
                # A declaration or a file-scope definition ends here; nothing
                # before it belongs to the next function's signature.
                signature = ""
                signature_start = position + 1
        if counter is not None and body_start is not None:
            counter.add(count_returns(line[body_start:]))
        elif depth == 0 and signature_start is not None:
            signature = join_signature(signature, line[signature_start:])
    return functions


def collect_paths(paths):
    files = []
    for path in paths:
        if os.path.isdir(path):
            for root, _, names in os.walk(path):
                for name in sorted(names):
                    if name.endswith(SOURCE_EXTENSIONS):
                        files.append(os.path.join(root, name))
        else:
            files.append(path)
    return sorted(files)


SELF_TEST_SOURCE = r"""
int one(void) { return 0; }

int two(int x)
{
    if (x) {
        return 1;
    }
    return 2;
}

int platform(void)
{
#if defined(A)
    return 1;
#elif defined(B)
    return 2;
#else
    return 3;
#endif
}

int platform_plus_one(void)
{
#if defined(A)
    return 1;
#else
    return 2;
#endif
    return 3;
}

int nested_arms(void)
{
#if defined(A)
#  if defined(B)
    return 1;
#  else
    return 2;
#  endif
    return 3;
#else
    return 4;
#endif
}

const char* text(void)
{
    // return 9;
    /* return 8; */
    return "return 7 { }";
}

struct NotAFunction {
    int field;
};

static const int table[] = {
    1, 2, 3,
};

int returned_identifier(void)
{
    int returned = 0;
    int returning = returned;
    return returning;
}
"""

SELF_TEST_EXPECTED = {
    "one": 1,
    "two": 2,
    "platform": 1,
    "platform_plus_one": 2,
    "nested_arms": 2,
    "text": 1,
    "returned_identifier": 1,
}


def self_test():
    found = {name: returns for name, _, _, returns in scan_text(SELF_TEST_SOURCE)}
    failures = []
    for name, expected in SELF_TEST_EXPECTED.items():
        actual = found.get(name)
        if actual != expected:
            failures.append(f"{name}: expected {expected} returns, got {actual}")
    for name in found:
        if name not in SELF_TEST_EXPECTED:
            failures.append(f"{name}: unexpected function reported")
    for failure in failures:
        print(f"FAIL {failure}")
    if failures:
        return 1
    print(f"ok: {len(SELF_TEST_EXPECTED)} cases")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", default=["src"])
    parser.add_argument(
        "--min",
        type=int,
        default=2,
        help="only report functions with at least this many returns (default 2)",
    )
    parser.add_argument(
        "--sort",
        choices=("name", "returns", "lines"),
        default="returns",
        help="order within each file (default returns, descending)",
    )
    parser.add_argument(
        "--totals-only",
        action="store_true",
        help="print one summary line per file instead of every function",
    )
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    if arguments.self_test:
        return self_test()

    paths = arguments.paths or ["src"]
    grand_total = 0
    grand_returns = 0
    for path in collect_paths(paths):
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        functions = [
            entry for entry in scan_text(text) if entry[3] >= arguments.min
        ]
        if not functions:
            continue
        if arguments.sort == "returns":
            functions.sort(key=lambda entry: (-entry[3], entry[1]))
        elif arguments.sort == "lines":
            functions.sort(key=lambda entry: (-(entry[2] - entry[1]), entry[1]))
        else:
            functions.sort(key=lambda entry: entry[0])
        excess = sum(entry[3] - 1 for entry in functions)
        grand_total += len(functions)
        grand_returns += excess
        print(f"{path}: {len(functions)} functions, {excess} returns to remove")
        if arguments.totals_only:
            continue
        for name, start, end, returns in functions:
            print(f"    {start:>6}  {returns:>4} returns  {end - start + 1:>6} lines  {name}")
    print(f"total: {grand_total} functions, {grand_returns} returns to remove")
    return 0


if __name__ == "__main__":
    sys.exit(main())
