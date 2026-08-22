#!/usr/bin/env python3
"""Rewrite multi-return functions toward AGENTS.md's one-return rule.

Two mechanical passes, applied per function until neither fires:

  gate     A guard `if (C) { return R; }` whose remaining region falls
           forward to a tail `return R;` of the same expression becomes
           `if (!C) { region }`.  Works at any depth: a deeper guard gates
           the rest of its enclosing block when only closing braces stand
           between that block and the tail return.  Every block closed on
           the way must be owned by `if`/`else` or be a bare scope — falling
           out of a loop body is not falling forward, so `for`/`while`/`do`
           and `switch` owners refuse the rewrite, and so does any owner the
           scanner cannot name (an `EACH_*` macro header, a compound
           literal).  Ownership is decided by scanning backwards from the
           block's opening brace across the whole header, so a loop header
           that spans several lines is still recognised as a loop; the
           reverted first version of this pass read only the single line
           above the brace, which is how a multi-line `for` header slipped
           through and broke the aarch64-macos self-host.

  cascade  A trailing run of guards `if (C) { return X; }` immediately
           before the tail return becomes one if/else-if chain assigning an
           undecorated `Type result;`.  No initializer: every arm assigns,
           and clang_analyze fails the Release tree on the dead store.

Shared refusals, each learned the hard way:
  - Preprocessor balance: the whole affected span must start and end at the
    same conditional depth, never dip below it, and carry no `#else`/`#elif`
    at that depth — a rewrite that spans an `#if` boundary balances its
    braces on one platform only.
  - Structural negation (De Morgan, flipped relational operators, dropped
    double negations) is discarded for plain `!( )` unless it preserves the
    original's identifiers and member accesses: a `<` or `>` that is really
    part of `->`, `<<`, `>>`, `<=` or `>=` must never be flipped.
  - Ordering comparisons with a floating-point literal on either side keep
    `!( )`: `!(a < b)` is not `a >= b` when NaN is possible.
  - A rewrite that would push a line past 160 columns is skipped rather
    than reflowed, since clang-format cannot be run against this tree.

Usage:
    tools/convert_single_return.py [--apply] [--passes gate,cascade]
                                   [--max-returns N] [paths...]
    tools/convert_single_return.py --self-test

Dry-run by default: reports what would change.  Paths default to `src`.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from scan_multi_return import (  # noqa: E402
    IDENTIFIER_CHARACTERS,
    blank_comments_and_literals,
    collect_paths,
    scan_text,
)

INDENT = "    "
COLUMN_LIMIT = 160
# Functions this large are deferred to hand conversion; a mechanical partial
# rewrite of a giant only makes the eventual manual pass harder to review.
DEFAULT_MAX_RETURNS = 50
# Words that may precede the return type in a definition without being part
# of it.  `const`/`unsigned`/... stay: they are the type.
STORAGE_WORDS = {"static", "inline", "extern", "BUSTER_GLOBAL_LOCAL", "BUSTER_C_INTERNAL"}


# --- small text utilities ---------------------------------------------------


def line_indent(line):
    return len(line) - len(line.lstrip(" "))


def is_blank(line):
    return not line.strip()


def is_pure_close(line):
    return line.strip() == "}"


def collapse_spaces(text):
    return " ".join(text.split())


def identifier_multiset(text):
    """Identifiers plus member-access operators, for the negation check."""
    tokens = []
    index = 0
    while index < len(text):
        character = text[index]
        if character in IDENTIFIER_CHARACTERS:
            start = index
            while index < len(text) and text[index] in IDENTIFIER_CHARACTERS:
                index += 1
            tokens.append(text[start:index])
        elif text.startswith("->", index):
            tokens.append("->")
            index += 2
        elif character == ".":
            tokens.append(".")
            index += 1
        else:
            index += 1
    return sorted(tokens)


# --- condition negation -----------------------------------------------------


def split_top_level(text, operator):
    """Split on a top-level `&&` or `||`; returns None when none present."""
    parts = []
    depth = 0
    start = 0
    index = 0
    while index < len(text):
        character = text[index]
        if character in "([":
            depth += 1
        elif character in ")]":
            depth -= 1
        elif depth == 0 and text.startswith(operator, index):
            before = text[index - 1] if index > 0 else ""
            after = text[index + 2] if index + 2 < len(text) else ""
            # `&&` never borders another `&`; same for `|`.  This rejects
            # bitwise `&`/`|` pairs like `a & &b` only in theory — what it
            # really guards is not matching inside `&&&` token pileups.
            if before != operator[0] and after != operator[0]:
                parts.append(text[start:index])
                start = index + 2
                index += 2
                continue
        index += 1
    if not parts:
        return None
    parts.append(text[start:])
    return [part.strip() for part in parts]


COMPARISON_COMPLEMENT = {
    "==": "!=",
    "!=": "==",
    "<": ">=",
    "<=": ">",
    ">": "<=",
    ">=": "<",
}


def find_sole_comparison(text):
    """Index and spelling of the single top-level comparison, else None.

    A `<` or `>` inside `->`, `<<`, `>>`, `<=`, `>=`, `<<=`, `>>=` is not a
    comparison; a top-level `=` (assignment) or `?` (ternary) disqualifies
    the whole leaf.
    """
    found = None
    depth = 0
    index = 0
    while index < len(text):
        character = text[index]
        if character in "([":
            depth += 1
        elif character in ")]":
            depth -= 1
        elif depth == 0:
            if character == "?":
                return None
            two = text[index : index + 2]
            if two in ("==", "!=", "<=", ">="):
                if found is not None:
                    return None
                found = (index, two)
                index += 2
                continue
            if two in ("->", "<<", ">>"):
                index += 2
                if index < len(text) and text[index] == "=":
                    index += 1
                continue
            if character in "<>":
                if found is not None:
                    return None
                found = (index, character)
            elif character == "=":
                return None
            elif character == ",":
                return None
        index += 1
    return found


def looks_floating(text):
    """A visible floating-point literal: `1.5`, `.5`, `1.f`, `1e-3`, `2.0f`."""
    index = 0
    while index < len(text):
        character = text[index]
        if character == "." and (
            (index > 0 and text[index - 1].isdigit())
            or (index + 1 < len(text) and text[index + 1].isdigit())
        ):
            return True
        if character.isdigit():
            end = index
            while end < len(text) and text[end] in "0123456789abcdefABCDEFxXuUlL.pP+-eE":
                end += 1
            literal = text[index:end]
            if not literal.lower().startswith("0x") and (
                "." in literal or "e" in literal.lower() or literal.lower().rstrip("ul").endswith("f")
            ):
                return True
            index = end
            continue
        index += 1
    return False


def is_primary(text):
    """An identifier/member/call/index chain, or one whole parenthesized
    group — something `!` binds to without needing extra parentheses."""
    text = text.strip()
    if not text:
        return False
    depth = 0
    index = 0
    while index < len(text):
        character = text[index]
        if character in "([":
            depth += 1
        elif character in ")]":
            depth -= 1
            if depth == 0 and index != len(text) - 1:
                next_character = text[index + 1]
                if next_character not in IDENTIFIER_CHARACTERS and next_character not in "([.- ":
                    return False
        elif depth == 0:
            if text.startswith("->", index):
                index += 2
                continue
            if character not in IDENTIFIER_CHARACTERS and character not in ". ":
                return False
        index += 1
    return depth == 0


def negate_structurally(text):
    text = text.strip()
    # Peel one layer of full parentheses: `(a || b)` negates as `a || b` does.
    while text.startswith("(") and text.endswith(")"):
        depth = 0
        closes_at_end = False
        for index, character in enumerate(text):
            if character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
                if depth == 0:
                    closes_at_end = index == len(text) - 1
                    break
        if not closes_at_end:
            break
        text = text[1:-1].strip()

    or_parts = split_top_level(text, "||")
    if or_parts:
        negated = []
        for part in or_parts:
            piece = negate_structurally(part)
            if split_top_level(piece, "||"):
                piece = "(" + piece + ")"
            negated.append(piece)
        return " && ".join(negated)

    and_parts = split_top_level(text, "&&")
    if and_parts:
        negated = []
        for part in and_parts:
            piece = negate_structurally(part)
            # `a && b || c` is correct but trips -Wlogical-op-parentheses;
            # keep every mixed level fully parenthesized.
            if split_top_level(piece, "&&"):
                piece = "(" + piece + ")"
            negated.append(piece)
        return " || ".join(negated)

    if text.startswith("!") and is_primary(text[1:]):
        return text[1:].strip()

    comparison = find_sole_comparison(text)
    if comparison and not looks_floating(text):
        index, spelling = comparison
        left = text[:index].strip()
        right = text[index + len(spelling) :].strip()
        return left + " " + COMPARISON_COMPLEMENT[spelling] + " " + right

    if is_primary(text):
        # `!` binds tighter than any operator a primary can contain.
        return "!" + text
    return "!(" + text + ")"


def negate_condition(condition):
    """Negate a (possibly multi-line) condition into one line, or fall back
    to `!( )` when the structural result does not preserve every identifier
    and member access of the original."""
    flat = collapse_spaces(condition)
    negated = negate_structurally(flat)
    if identifier_multiset(negated) != identifier_multiset(flat):
        negated = "!(" + flat + ")"
    return negated


# --- per-function analysis --------------------------------------------------


class SourceModel:
    """Original and blanked lines of one file, with offsets kept in sync."""

    def __init__(self, text):
        self.text = text
        self.blanked = blank_comments_and_literals(text)
        self.lines = text.split("\n")
        self.blanked_lines = self.blanked.split("\n")
        self.offsets = []
        offset = 0
        for line in self.lines:
            self.offsets.append(offset)
            offset += len(line) + 1


def brace_events(model, start_line, end_line):
    """Match braces inside [start_line, end_line]; both endpoints inclusive,
    zero-based.  Returns (open_offset -> close_offset, ordered opens)."""
    matches = {}
    stack = []
    for line_index in range(start_line, min(end_line + 1, len(model.lines))):
        line = model.blanked_lines[line_index]
        base = model.offsets[line_index]
        stripped = line.lstrip()
        if stripped.startswith("#"):
            continue
        for column, character in enumerate(line):
            if character == "{":
                stack.append(base + column)
            elif character == "}":
                if stack:
                    matches[stack.pop()] = base + column
    return matches


def block_owner(model, open_offset):
    """Name the construct that owns the block opening at `open_offset`.

    Scans backwards across the entire header — however many lines it spans —
    so `for (...)` split over three lines is still a loop.  Anything not
    positively recognised is "unknown", and unknown refuses a rewrite: an
    `EACH_SLICE_INT(i, s)` macro header is a loop this scanner cannot see
    into, and a compound literal's brace is not a statement block at all.
    """
    blanked = model.blanked
    index = open_offset - 1
    while index >= 0 and blanked[index].isspace():
        index -= 1
    if index < 0:
        return "bare"
    character = blanked[index]
    if character == ")":
        depth = 0
        while index >= 0:
            if blanked[index] == ")":
                depth += 1
            elif blanked[index] == "(":
                depth -= 1
                if depth == 0:
                    break
            index -= 1
        if index < 0:
            return "unknown"
        index -= 1
        while index >= 0 and blanked[index].isspace():
            index -= 1
        end = index + 1
        while index >= 0 and blanked[index] in IDENTIFIER_CHARACTERS:
            index -= 1
        word = blanked[index + 1 : end]
        if word == "if":
            return "if"
        if word in ("for", "while", "switch"):
            return "loop"
        return "unknown"
    if character in IDENTIFIER_CHARACTERS:
        end = index + 1
        while index >= 0 and blanked[index] in IDENTIFIER_CHARACTERS:
            index -= 1
        word = blanked[index + 1 : end]
        if word == "else":
            return "else"
        if word == "do":
            return "loop"
        return "unknown"
    if character in ";{}":
        return "bare"
    return "unknown"


def preprocessor_profile(model, start_line, end_line):
    """Per-line conditional depth before each line, plus arm-switch lines."""
    depth_before = {}
    arm_switch_depth = {}
    depth = 0
    for line_index in range(start_line, end_line + 1):
        depth_before[line_index] = depth
        stripped = model.blanked_lines[line_index].lstrip()
        if stripped.startswith("#"):
            directive = stripped[1:].lstrip().split(" ", 1)[0].split("\t", 1)[0]
            if directive in ("if", "ifdef", "ifndef"):
                depth += 1
            elif directive == "endif":
                depth -= 1
            elif directive in ("elif", "elifdef", "elifndef", "else"):
                arm_switch_depth[line_index] = depth
    return depth_before, arm_switch_depth


def span_preprocessor_safe(profile, first_line, last_line):
    """True when [first_line, last_line] starts and ends at one conditional
    depth, never dips below it, and no arm switch happens at that depth."""
    depth_before, arm_switch_depth = profile
    base = depth_before[first_line]
    if depth_before[last_line] != base:
        return False
    for line_index in range(first_line, last_line + 1):
        if depth_before[line_index] < base:
            return False
        switch_depth = arm_switch_depth.get(line_index)
        if switch_depth is not None and switch_depth == base:
            return False
    return True


class Guard:
    """One `if (C) ... return X;` guard, in any of the accepted spellings:
    Allman braces, one-line braces, or a braceless same-/next-line return."""

    def __init__(self, if_line, end_line, indent, condition, expression):
        self.if_line = if_line          # zero-based first line of the `if`
        self.end_line = end_line        # zero-based last line of the guard
        self.indent = indent
        self.condition = condition      # raw text, may span lines
        self.expression = expression    # normalized return expression, "" for `return;`


def match_close_paren(model, line_index, column):
    """From `(` at (line, column), find the matching `)`; returns (line,
    column) or None.  Spans lines."""
    depth = 0
    current_line = line_index
    while current_line < len(model.blanked_lines):
        line = model.blanked_lines[current_line]
        start = column if current_line == line_index else 0
        for current_column in range(start, len(line)):
            character = line[current_column]
            if character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
                if depth == 0:
                    return current_line, current_column
        current_line += 1
    return None


def parse_return_statement(model, line_index, column):
    """Parse `return ...;` starting at (line, column) in blanked text.
    Returns (last_line, expression) or None."""
    collected = []
    current_line = line_index
    start_column = column
    while current_line < len(model.blanked_lines):
        blanked_line = model.blanked_lines[current_line]
        semicolon = blanked_line.find(";", start_column)
        if semicolon >= 0:
            collected.append(self_slice(model, current_line, start_column, semicolon))
            text = " ".join(collected)
            if not text.startswith("return"):
                return None
            expression = text[len("return") :].strip()
            return current_line, collapse_spaces(expression), semicolon
        collected.append(self_slice(model, current_line, start_column, len(blanked_line)))
        current_line += 1
        start_column = 0
    return None


def self_slice(model, line_index, start, end):
    return model.lines[line_index][start:end].strip()


def parse_guard(model, line_index):
    """Recognise a guard whose body is exactly one return statement."""
    blanked_line = model.blanked_lines[line_index]
    stripped = blanked_line.lstrip()
    if not stripped.startswith("if"):
        return None
    indent = line_indent(blanked_line)
    after_if = blanked_line[indent + 2 :]
    if not after_if.lstrip().startswith("("):
        return None
    open_column = blanked_line.index("(", indent + 2)
    close = match_close_paren(model, line_index, open_column)
    if close is None:
        return None
    close_line, close_column = close
    condition_parts = []
    for part_line in range(line_index, close_line + 1):
        start = open_column + 1 if part_line == line_index else 0
        end = close_column if part_line == close_line else len(model.lines[part_line])
        condition_parts.append(model.lines[part_line][start:end].strip())
    condition = " ".join(part for part in condition_parts if part)

    rest = model.blanked_lines[close_line][close_column + 1 :].strip()
    rest_original = model.lines[close_line][close_column + 1 :].strip()
    if rest.startswith("return"):
        # `if (C) return X;` — the braceless same-line guard.
        if not rest.endswith(";"):
            return None
        expression = collapse_spaces(rest_original[len("return") : -1].strip())
        return Guard(line_index, close_line, indent, condition, expression)
    if rest.startswith("{"):
        # `if (C) { return X; }` on one line.
        inner = rest[1:].strip()
        inner_original = rest_original[rest_original.index("{") + 1 :].strip()
        if not (inner.startswith("return") and inner.endswith("}")):
            return None
        body = inner_original[:-1].strip()
        if not (body.startswith("return") and body.endswith(";")):
            return None
        expression = collapse_spaces(body[len("return") : -1].strip())
        return Guard(line_index, close_line, indent, condition, expression)
    if rest:
        return None

    next_line = close_line + 1
    while next_line < len(model.lines) and is_blank(model.lines[next_line]):
        next_line += 1
    if next_line >= len(model.lines):
        return None
    next_stripped = model.blanked_lines[next_line].strip()
    if next_stripped.startswith("return"):
        # Braceless with the return on the following line.
        column = model.blanked_lines[next_line].index("return")
        parsed = parse_return_statement(model, next_line, column)
        if parsed is None:
            return None
        last_line, expression, semicolon = parsed
        if model.blanked_lines[last_line][semicolon + 1 :].strip():
            return None
        return Guard(line_index, last_line, indent, condition, expression)
    if next_stripped != "{":
        return None
    body_line = next_line + 1
    while body_line < len(model.lines) and is_blank(model.lines[body_line]):
        body_line += 1
    if body_line >= len(model.lines):
        return None
    body_stripped = model.blanked_lines[body_line].lstrip()
    if not body_stripped.startswith("return"):
        return None
    column = model.blanked_lines[body_line].index("return")
    parsed = parse_return_statement(model, body_line, column)
    if parsed is None:
        return None
    last_line, expression, semicolon = parsed
    if model.blanked_lines[last_line][semicolon + 1 :].strip():
        return None
    close_brace_line = last_line + 1
    while close_brace_line < len(model.lines) and is_blank(model.lines[close_brace_line]):
        close_brace_line += 1
    if close_brace_line >= len(model.lines):
        return None
    if model.blanked_lines[close_brace_line].strip() != "}":
        return None
    return Guard(line_index, close_brace_line, indent, condition, expression)


def guard_has_else(model, guard):
    after = guard.end_line + 1
    while after < len(model.lines) and is_blank(model.lines[after]):
        after += 1
    if after >= len(model.lines):
        return False
    return model.blanked_lines[after].strip().startswith("else")


def guard_is_else_arm(model, guard):
    line = model.blanked_lines[guard.if_line]
    before = line[: line.index("if", guard.indent)].strip()
    if before.endswith("else"):
        return True
    previous = guard.if_line - 1
    while previous >= 0 and is_blank(model.lines[previous]):
        previous -= 1
    if previous < 0:
        return False
    return model.blanked_lines[previous].rstrip().endswith("else")


class FunctionInfo:
    def __init__(self, name, start_line, end_line, returns):
        self.name = name
        self.start_line = start_line - 1  # zero-based line of the opening `{`
        self.end_line = end_line - 1      # zero-based line of the closing `}`
        self.returns = returns


def function_signature_text(model, function):
    """Join the contiguous definition lines above the opening brace."""
    lines = []
    line_index = function.start_line - 1
    while line_index >= 0:
        line = model.lines[line_index]
        stripped = model.blanked_lines[line_index].strip()
        if not stripped or stripped.startswith("#"):
            break
        if stripped.endswith((";", "}", "{")):
            break
        lines.append(line.strip())
        line_index -= 1
    lines.reverse()
    return " ".join(lines)


def function_return_type(model, function):
    signature = function_signature_text(model, function)
    position = signature.rfind(function.name + "(")
    if position < 0:
        position = signature.rfind(function.name + " (")
    if position <= 0:
        return None
    prefix = signature[:position].strip()
    words = prefix.split()
    while words and words[0] in STORAGE_WORDS:
        words = words[1:]
    if not words:
        return None
    return " ".join(words)


def find_tail_return(model, function):
    """The function's final statement, when it is a top-level `return`.

    Returns (first_line, last_line, expression) or None; expression is ""
    for a bare `return;` and None when the function simply ends (a void
    body with no explicit return)."""
    last = function.end_line - 1
    while last > function.start_line and is_blank(model.lines[last]):
        last -= 1
    if last <= function.start_line:
        return None
    # Find the statement's first line: walk back to the line that starts it.
    line = model.blanked_lines[last]
    if not line.rstrip().endswith(";"):
        return function.end_line, function.end_line, None
    first = last
    while first > function.start_line:
        stripped = model.blanked_lines[first].lstrip()
        if stripped.startswith("return"):
            break
        previous = model.blanked_lines[first - 1].rstrip()
        if previous.endswith((";", "{", "}")):
            break
        first -= 1
    stripped = model.blanked_lines[first].lstrip()
    if not stripped.startswith("return") or line_indent(model.blanked_lines[first]) != 4:
        return function.end_line, function.end_line, None
    column = model.blanked_lines[first].index("return")
    parsed = parse_return_statement(model, first, column)
    if parsed is None:
        return None
    parsed_last, expression, semicolon = parsed
    if parsed_last != last or model.blanked_lines[last][semicolon + 1 :].strip():
        return None
    return first, last, expression


def enclosing_chain(model, function, guard, matches):
    """Open offsets of the blocks enclosing the guard, outermost first,
    excluding the function body's own brace."""
    guard_offset = model.offsets[guard.if_line] + guard.indent
    chain = []
    for open_offset, close_offset in matches.items():
        if open_offset < guard_offset < close_offset:
            chain.append(open_offset)
    chain.sort()
    body_offset = model.offsets[function.start_line] + model.blanked_lines[
        function.start_line
    ].index("{")
    return [offset for offset in chain if offset != body_offset]


def offset_line(model, offset):
    low, high = 0, len(model.offsets) - 1
    while low < high:
        middle = (low + high + 1) // 2
        if model.offsets[middle] <= offset:
            low = middle
        else:
            high = middle - 1
    return low


# --- the gate pass ----------------------------------------------------------


def try_gate(model, function, profile, matches):
    """Find the first guard this pass can gate; returns replacement lines
    for a span, as (start_line, end_line_exclusive, new_lines), or None."""
    tail = find_tail_return(model, function)
    if tail is None:
        return None
    tail_first, tail_last, tail_expression = tail

    line_index = function.start_line + 1
    while line_index < tail_first:
        guard = parse_guard(model, line_index)
        if guard is None or guard.end_line >= tail_first:
            line_index += 1
            continue
        rewrite = try_gate_guard(
            model, function, profile, matches, guard, tail_first, tail_expression
        )
        if rewrite is not None:
            return rewrite
        line_index = guard.end_line + 1
    return None


def try_gate_guard(model, function, profile, matches, guard, tail_first, tail_expression):
    if tail_expression is None:
        if guard.expression != "":
            return None
    elif guard.expression != collapse_spaces(tail_expression):
        return None
    if guard.indent < 4 or guard.indent % 4 != 0:
        return None
    if guard_has_else(model, guard) or guard_is_else_arm(model, guard):
        return None

    chain = enclosing_chain(model, function, guard, matches)
    if guard.indent != 4 * (len(chain) + 1):
        # Indentation and nesting disagree — some brace on the way is not
        # laid out in the house style; do not guess.
        return None

    if chain:
        for open_offset in chain:
            if block_owner(model, open_offset) not in ("if", "else", "bare"):
                return None
        # The innermost enclosing block is the one that closes first.
        inner_close_line = offset_line(model, min(matches[o] for o in chain))
        if not is_pure_close(model.lines[inner_close_line]):
            return None
        for between in range(inner_close_line + 1, tail_first):
            line = model.lines[between]
            if not (is_blank(line) or is_pure_close(line)):
                return None
        region_end = inner_close_line  # exclusive
    else:
        region_end = tail_first  # exclusive

    region_start = guard.end_line + 1
    region_lines = list(range(region_start, region_end))
    while region_lines and is_blank(model.lines[region_lines[-1]]):
        region_lines.pop()
    while region_lines and is_blank(model.lines[region_lines[0]]):
        region_lines.pop(0)
    if not region_lines:
        return None

    span_last = tail_first if chain else region_end
    if not span_preprocessor_safe(profile, guard.if_line, span_last):
        return None

    negated = negate_condition(guard.condition)
    prefix = " " * guard.indent
    if_line = prefix + "if (" + negated + ")"
    if len(if_line) > COLUMN_LIMIT:
        return None

    new_lines = [if_line, prefix + "{"]
    for region_line in range(region_lines[0], region_lines[-1] + 1):
        line = model.lines[region_line]
        if is_blank(line):
            new_lines.append("")
        elif model.blanked_lines[region_line].lstrip().startswith("#"):
            new_lines.append(line)
        else:
            shifted = INDENT + line
            if len(shifted) > COLUMN_LIMIT and len(line) <= COLUMN_LIMIT:
                return None
            new_lines.append(shifted)
    new_lines.append(prefix + "}")
    if not chain and tail_expression is not None:
        new_lines.append("")
    return guard.if_line, region_end, new_lines


# --- the cascade pass -------------------------------------------------------


def try_cascade(model, function, profile):
    tail = find_tail_return(model, function)
    if tail is None:
        return None
    tail_first, tail_last, tail_expression = tail
    if not tail_expression:  # bare `return;` and implicit ends have no answer
        return None

    return_type = function_return_type(model, function)
    if return_type is None or return_type == "void":
        return None

    # `result` must be free in the whole function — parameters included, so
    # the signature is part of the scan; the declaration this pass adds would
    # otherwise shadow or collide.
    scan_targets = [function_signature_text(model, function)]
    scan_targets.extend(
        model.blanked_lines[line_index]
        for line_index in range(function.start_line, function.end_line + 1)
    )
    for line in scan_targets:
        position = line.find("result")
        while position >= 0:
            before = line[position - 1] if position > 0 else ""
            after_index = position + len("result")
            after = line[after_index] if after_index < len(line) else ""
            if before not in IDENTIFIER_CHARACTERS and after not in IDENTIFIER_CHARACTERS:
                return None
            position = line.find("result", position + 1)

    guards = []
    cursor = tail_first - 1
    while cursor > function.start_line:
        while cursor > function.start_line and is_blank(model.lines[cursor]):
            cursor -= 1
        found = None
        # The guard ending at `cursor` may start several lines up (multi-line
        # condition or return); a bounded upward scan finds its `if` line, and
        # requiring end_line == cursor rejects any unrelated `if` above.
        lowest = max(function.start_line + 1, cursor - 12)
        for candidate_line in range(cursor, lowest - 1, -1):
            guard = parse_guard(model, candidate_line)
            if guard is not None and guard.end_line == cursor:
                found = guard
                break
        if found is None or found.indent != 4:
            break
        if guard_has_else(model, found) or guard_is_else_arm(model, found):
            break
        guards.append(found)
        cursor = found.if_line - 1
    if not guards:
        return None
    guards.reverse()

    first_line = guards[0].if_line
    if not span_preprocessor_safe(profile, first_line, tail_last):
        return None

    new_lines = [INDENT + return_type + " result;"]
    for position, guard in enumerate(guards):
        keyword = "if" if position == 0 else "else if"
        header = INDENT + keyword + " (" + collapse_spaces(guard.condition) + ")"
        if len(header) > COLUMN_LIMIT:
            return None
        assignment = INDENT + INDENT + "result = " + guard.expression + ";"
        if len(assignment) > COLUMN_LIMIT:
            return None
        new_lines.extend([header, INDENT + "{", assignment, INDENT + "}"])
    final = INDENT + INDENT + "result = " + tail_expression + ";"
    if len(final) > COLUMN_LIMIT:
        return None
    new_lines.extend([INDENT + "else", INDENT + "{", final, INDENT + "}"])
    new_lines.extend(["", INDENT + "return result;"])
    return first_line, tail_last + 1, new_lines


# --- driving ----------------------------------------------------------------


def convert_text(text, passes, max_returns):
    """Apply passes until none fires; returns (new_text, counts)."""
    counts = {"gate": 0, "cascade": 0}
    while True:
        model = SourceModel(text)
        functions = [
            FunctionInfo(*entry)
            for entry in scan_text(text)
            if 2 <= entry[3] <= max_returns
        ]
        edits = []
        for function in functions:
            # A backslash continuation inside a body (a #define spanning
            # lines) would desynchronise the physical-line brace scan.
            if any(
                model.lines[line_index].endswith("\\")
                for line_index in range(function.start_line, function.end_line + 1)
            ):
                continue
            profile = preprocessor_profile(model, function.start_line, function.end_line)
            matches = brace_events(model, function.start_line, function.end_line)
            edit = None
            if "gate" in passes:
                edit = try_gate(model, function, profile, matches)
                if edit is not None:
                    edits.append((edit, "gate"))
                    continue
            if "cascade" in passes:
                edit = try_cascade(model, function, profile)
                if edit is not None:
                    edits.append((edit, "cascade"))
        if not edits:
            return text, counts
        lines = text.split("\n")
        for (start, end, replacement), pass_name in sorted(
            edits, key=lambda item: -item[0][0]
        ):
            lines[start:end] = replacement
            counts[pass_name] += 1
        text = "\n".join(lines)


def convert_file(path, passes, max_returns, apply_changes):
    with open(path, "r", encoding="utf-8") as handle:
        original = handle.read()
    converted, counts = convert_text(original, passes, max_returns)
    if converted != original and apply_changes:
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(converted)
    return counts


# --- self-test --------------------------------------------------------------

SELF_TEST_SOURCE = r"""
static bool multiline_loop_header_refused(Table* table, u32 count)
{
    bool result = probe(table);
    for (u32 index = 0;
         index < count;
         index += 1)
    {
        if (table->rows[index].dead)
        {
            return result;
        }
        consume(table, index);
    }
    return result;
}

static bool single_line_loop_refused(Table* table, u32 count)
{
    bool result = probe(table);
    for (u32 index = 0; index < count; index += 1)
    {
        if (table->rows[index].dead)
        {
            return result;
        }
        consume(table, index);
    }
    return result;
}

static bool macro_loop_refused(Slice values)
{
    bool result = false;
    EACH_SLICE_INT(index, values)
    {
        if (values.pointer[index])
        {
            return result;
        }
        result = accumulate(result, index);
    }
    return result;
}

static u32 do_while_refused(u32 value)
{
    u32 result = seed(value);
    do
    {
        if (value == result)
        {
            return result;
        }
        result = fold(result);
    } while (result < value);
    return result;
}

static u32 switch_refused(u32 value, u32 kind)
{
    u32 result = seed(value);
    switch (kind)
    {
        case 0:
        {
            if (value)
            {
                return result;
            }
            result += 1;
        } break;
        default: break;
    }
    return result;
}

static u32 nested_if_gated(Record* record, u32 count)
{
    u32 result = 0;
    if (record->kind == RECORD_LEAF)
    {
        if (!record->declaration || count >= record->limit)
        {
            return result;
        }
        result = record->payload + count;
    }
    return result;
}

static u32 top_level_gated(Buffer* buffer, u32 size)
{
    u32 result = 0;
    if (size > BUFFER_LIMIT || (!buffer && size))
    {
        return result;
    }
    prepare(buffer);
    result = consume_all(buffer, size);
    return result;
}

static u32 else_attached_refused(u32 value)
{
    u32 result = 0;
    if (value > 4)
    {
        return result;
    }
    else
    {
        result += 1;
    }
    result += fold(value);
    return result;
}

static u32 preprocessor_split_refused(u32 value)
{
    u32 result = 0;
#if defined(FEATURE_A)
    if (value > 4)
    {
        return result;
    }
#endif
    result = fold(value);
    return result;
}

static u32 preprocessor_balanced_gated(u32 value)
{
    u32 result = 0;
    if (value > 4)
    {
        return result;
    }
#if defined(FEATURE_A)
    result += 1;
#endif
    result += fold(value);
    return result;
}

static u8 cascade_base64(char8 value)
{
    if (value >= 'A' && value <= 'Z') return (u8)(value - 'A');
    if (value >= 'a' && value <= 'z') return (u8)(value - 'a' + 26);
    if (value == '+') return 62;
    return 0;
}

static u32 cascade_collision_refused(u32 value, u32* result)
{
    if (value > 4) return 1;
    return *result;
}

static f32 float_guard(f32 value)
{
    f32 result = 0.0f;
    if (value < 1.5f)
    {
        return result;
    }
    result = value;
    return result;
}

static void void_gate(Sink* sink, u32 value)
{
    if (!sink->ready)
    {
        return;
    }
    sink->total += value;
    if (sink->total > SINK_LIMIT)
    {
        return;
    }
    flush(sink);
}
"""


def run_self_test():
    converted, counts = convert_text(SELF_TEST_SOURCE, ("gate", "cascade"), DEFAULT_MAX_RETURNS)
    failures = []

    def check(condition, message):
        if not condition:
            failures.append(message)

    # The four loop shapes and the switch keep their guards.
    for name in (
        "multiline_loop_header_refused",
        "single_line_loop_refused",
        "macro_loop_refused",
        "do_while_refused",
        "switch_refused",
    ):
        body = converted.split(name)[1].split("\nstatic ")[0]
        check("return result;\n        }" in body or "            return result;" in body,
              name + ": guard was rewritten but must be refused")

    check("if (record->declaration && count < record->limit)" in converted,
          "nested_if_gated: expected the negated gate")
    check("if (size <= BUFFER_LIMIT && (buffer || !size))" in converted,
          "top_level_gated: expected the negated gate")
    check("consume_all" in converted.split("top_level_gated")[1].split("\nstatic ")[0],
          "top_level_gated: body lost")
    check("else_attached_refused" in converted and
          "return result;\n    }\n    else" in converted.split("else_attached_refused")[1],
          "else_attached_refused: guard with else arm must be refused")
    check("#if defined(FEATURE_A)\n    if (value > 4)" in converted,
          "preprocessor_split_refused: rewrite crossed an #if boundary")
    check("if (value <= 4)\n    {\n#if defined(FEATURE_A)" in converted,
          "preprocessor_balanced_gated: balanced group should gate")
    cascade = converted.split("cascade_base64")[1].split("\nstatic ")[0]
    check("u8 result;" in cascade and "else if (value >= 'a' && value <= 'z')" in cascade
          and "else\n    {\n        result = 0;\n    }" in cascade
          and "\n    return result;" in cascade,
          "cascade_base64: expected the else-if chain over u8 result")
    check("result = (u8)(value - 'A');" in cascade, "cascade_base64: arm assignment missing")
    check("if (value > 4) return 1;" in converted,
          "cascade_collision_refused: `result` parameter name must refuse")
    check("if (!(value < 1.5f))" in converted,
          "float_guard: ordering comparison with a float literal must not flip")
    check("if (sink->ready)\n    {\n        sink->total += value;" in converted,
          "void_gate: bare-return guard should gate in a void function")

    # Negation unit cases, including the `->` trap that motivated the check.
    negations = [
        ("!record->declaration", "record->declaration"),
        ("a->x > 0 && q->y", "a->x <= 0 || !q->y"),
        ("a - b > c", "a - b <= c"),
        ("count >= limit || !table", "count < limit && table"),
        ("(a || b) && c", "(!a && !b) || !c"),
        ("bits << 2 > mask", "bits << 2 <= mask"),
        ("x = probe()", "!(x = probe())"),
        ("value < 1.5f", "!(value < 1.5f)"),
    ]
    for source, expected in negations:
        actual = negate_condition(source)
        check(actual == expected, f"negate({source!r}) = {actual!r}, expected {expected!r}")

    for failure in failures:
        print("FAIL " + failure)
    if failures:
        return 1
    print("ok: self-test passed, gates=%d cascades=%d" % (counts["gate"], counts["cascade"]))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", default=["src"])
    parser.add_argument("--apply", action="store_true", help="write changes (default: dry run)")
    parser.add_argument("--passes", default="gate,cascade")
    parser.add_argument("--max-returns", type=int, default=DEFAULT_MAX_RETURNS)
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    if arguments.self_test:
        return run_self_test()
    passes = tuple(arguments.passes.split(","))
    total = {"gate": 0, "cascade": 0}
    for path in collect_paths(arguments.paths or ["src"]):
        counts = convert_file(path, passes, arguments.max_returns, arguments.apply)
        if counts["gate"] or counts["cascade"]:
            print(f"{path}: {counts['gate']} gates, {counts['cascade']} cascades")
            total["gate"] += counts["gate"]
            total["cascade"] += counts["cascade"]
    mode = "applied" if arguments.apply else "would apply"
    print(f"total: {mode} {total['gate']} gates, {total['cascade']} cascades")
    return 0


if __name__ == "__main__":
    sys.exit(main())
