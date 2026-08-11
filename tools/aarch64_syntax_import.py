#!/usr/bin/env python3
"""Generate the checked-in Apple-M1 A64 syntax metadata.

The Arm canonical JSONL is the only source consumed here.  This importer is
deliberately syntax-only: angle-bracket fields are retained as source atoms,
and no encoding or operand semantics are inferred from their spelling.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable


SCHEMA_VERSION = 1
# These pins identify the checked-in Arm snapshot.  The importer computes
# every value below from the supplied source and rejects drift before emitting
# any artifact.  Keeping the pins here makes a partial/mutated source fail
# closed instead of silently producing a plausible-looking new snapshot.
CANONICAL_SOURCE_SHA256 = "8485c5c61835d5394d325757ab2964890e8bdfea304c6faa8fd4c23e4c7aabec"
INPUT_DIGEST = "eea16d7f094badc65614aed988621f48aca5495890847294bb29decc4be1c31c"
ID_DIGEST = "f853ee3a6111d5cbe6de323a22336ae4b1b8a30192c89e09ea4fa6a0bc32dbcf"
KIND_DIGEST = "c046c04fa784fe99cc63a169213a0b6d01fc1fe1fbd27e8549adf54654b2ded3"
GENERIC_SHAPE_COUNT = 1165
EXACT_SHAPE_COUNT = 1635
GENERIC_SHAPE_DIGEST = "6fe6e323db0da19e4d59db217d1b701f556a40a3ed4ba4d55deba4e188538c09"
GENERIC_ROW_DIGEST = "5bf1a8b164bab5c61d43bfe74978de3f02652fbc396374eec1cae41e5f2f1963"
EXACT_SHAPE_DIGEST = "0e45e7d61f59e075017cdabe6bd9ce6cbce92defcd4cd9f1b6123696ba88f4d5"
EXACT_ROW_DIGEST = "e37f03c8f0a20897b289ed70b2000e18aa796bdb03eaf31ba99d31e0457a18c9"
RELEASE_PREFIX = "arm-a64@2026-06:"


K_MNEMONIC = 0
K_SEQ = 1
K_LIT = 2
K_ANCHOR = 3
K_OPTIONAL = 4
K_ALT = 5
K_MEM = 6
K_LIST = 7
K_LANE = 8

F_ANCHOR_ALTERNATIVE = 1 << 0
F_ANCHOR_RANGE = 1 << 1
F_LIT_FIXED = 1 << 0
F_LIT_SHIFT = 1 << 1
F_LIT_EXTEND = 1 << 2
F_LIT_DELIMITER = 1 << 3
F_MNEMONIC_CONDITION = 1 << 0
F_MNEMONIC_OPTIONAL_SUFFIX = 1 << 1
F_ALT_IMPLICIT = 1 << 0
F_MEM_WRITEBACK = 1 << 0

NUMERIC_RE = re.compile(r"(?<![A-Za-z])(?:#?\d+(?:\.\d+)?|\.\d+[A-Za-z]+)")


@dataclass
class Node:
    kind: int
    text: str = ""
    flags: int = 0
    children: list["Node"] = field(default_factory=list)


def anchor_bounds(node: Node) -> tuple[int, int]:
    """Return the minimum/maximum callback occurrences for one AST node.

    An ALT accepts one branch in the compact spelling, while its canonical
    spelling contains every branch between the delimiters.  The upper bound
    therefore includes all children (and is also the canonical row count),
    whereas the lower bound selects the least expensive branch.  OPTIONAL
    nodes may omit their child entirely.
    """
    if node.kind == K_ANCHOR:
        return 1, 1
    if node.kind in (K_LIT, K_MNEMONIC):
        return 0, 0
    if node.kind == K_OPTIONAL:
        child_min = sum(anchor_bounds(child)[0] for child in node.children)
        child_max = sum(anchor_bounds(child)[1] for child in node.children)
        return 0, child_max
    if node.kind == K_ALT:
        branches: list[list[Node]] = [[]]
        for child in node.children:
            if child.kind == K_LIT and child.text == "|":
                branches.append([])
            else:
                branches[-1].append(child)
        branch_bounds = [
            (sum(anchor_bounds(child)[0] for child in branch),
             sum(anchor_bounds(child)[1] for child in branch))
            for branch in branches
        ]
        minimum = min((bounds[0] for bounds in branch_bounds), default=0)
        canonical_max = sum(anchor_bounds(child)[1] for child in node.children)
        branch_max = max((bounds[1] for bounds in branch_bounds), default=0)
        return minimum, max(canonical_max, branch_max)
    child_min = sum(anchor_bounds(child)[0] for child in node.children)
    child_max = sum(anchor_bounds(child)[1] for child in node.children)
    return child_min, child_max


class SyntaxParser:
    """Parse the deliberately small Arm display-template grammar."""

    def __init__(self, text: str):
        self.text = text

    def close(self, start: int, opening: str, closing: str) -> int:
        depth = 0
        for index in range(start, len(self.text)):
            character = self.text[index]
            if character == opening:
                depth += 1
            elif character == closing:
                depth -= 1
                if depth == 0:
                    return index
        raise ValueError(f"unclosed {opening!r} in {self.text!r}")

    @staticmethod
    def normalize_whitespace(value: str) -> str:
        return " ".join(value.split())

    @staticmethod
    def is_list(inner: str) -> bool:
        # Arm uses spaces immediately inside braces for register lists.  A
        # compact brace group is an optional operand (including {#<imm>}).
        return inner.startswith(" ") and inner.endswith(" ") and "<" in inner

    def sequence(self, start: int = 0, end: int | None = None) -> tuple[list[Node], int]:
        result: list[Node] = []
        literal: list[str] = []

        def flush() -> None:
            if literal:
                result.append(Node(K_LIT, "".join(literal)))
                literal.clear()

        index = start
        while index < len(self.text) and (end is None or self.text[index] != end):
            character = self.text[index]
            if character == "<":
                flush()
                close = self.text.find(">", index + 1)
                if close < 0 or (end is not None and close >= end):
                    raise ValueError(f"unclosed anchor in {self.text!r}")
                spelling = self.text[index + 1 : close]
                flags = 0
                if "|" in spelling:
                    flags |= F_ANCHOR_ALTERNATIVE
                if "+" in spelling:
                    flags |= F_ANCHOR_RANGE
                result.append(Node(K_ANCHOR, spelling, flags))
                index = close + 1
                continue

            if character == "|":
                # A pipe is a structural ALT separator.  Angle-bracket
                # alternatives were consumed atomically above, so this never
                # splits the `|` inside `<Xn|SP>`.
                flush()
                result.append(Node(K_LIT, "|"))
                index += 1
                continue

            if character in "{([":
                flush()
                closing = {"{": "}", "(": ")", "[": "]"}[character]
                close = self.close(index, character, closing)
                inner = self.text[index + 1 : close]
                children = SyntaxParser(inner).parse()
                if character == "[" and len(children) == 1 and children[0].kind == K_ANCHOR and children[0].text.startswith("index"):
                    kind = K_LANE
                    flags = 0
                elif character == "[":
                    kind = K_MEM
                    flags = F_MEM_WRITEBACK if close + 1 < len(self.text) and self.text[close + 1] == "!" else 0
                elif character == "(":
                    kind = K_ALT
                    flags = 0
                elif self.is_list(inner):
                    kind = K_LIST
                    flags = 0
                else:
                    kind = K_OPTIONAL
                    flags = 0
                    # ISB's optional braces contain an alternation without
                    # parentheses.  Keep the exceptional ALT node explicit.
                    if "|" in inner:
                        children = [Node(K_ALT, "", F_ALT_IMPLICIT, children)]

                # Braces/brackets are regular literal nodes so the matcher
                # and printer can consume/emit the complete source spelling.
                # ALT retains its delimiters on the ALT node itself.
                if kind != K_ALT:
                    children = [Node(K_LIT, character, F_LIT_DELIMITER), *children,
                                Node(K_LIT, closing + ("!" if flags & F_MEM_WRITEBACK else ""), F_LIT_DELIMITER)]
                result.append(Node(kind, "", flags, children))
                index = close + 1 + (1 if flags & F_MEM_WRITEBACK else 0)
                continue

            literal.append(character)
            index += 1

        flush()
        return result, index

    def parse(self) -> list[Node]:
        nodes, position = self.sequence()
        if position != len(self.text):
            raise ValueError(f"trailing syntax at {position} in {self.text!r}")
        return nodes


def mnemonic_node(assembly: str, nodes: list[Node]) -> list[Node]:
    """Split the first display token into the dedicated MNEMONIC node."""
    if not nodes or nodes[0].kind != K_LIT:
        raise ValueError(f"missing mnemonic in {assembly!r}")
    literal = nodes[0].text
    source_token = assembly.split(None, 1)[0]
    token = base_mnemonic(source_token)
    flags = 0
    if "{" in source_token and source_token.endswith("}"):
        flags |= F_MNEMONIC_OPTIONAL_SUFFIX
    if source_token == "B.<cond>":
        flags |= F_MNEMONIC_CONDITION
    # Keep the complete literal prefix (including a separator or the `.` in
    # `B.<cond>`) on the MNEMONIC node.  The row's base-mnemonic field remains
    # independently available for deterministic lookup.
    node = Node(K_MNEMONIC, literal, flags)
    return [node, *nodes[1:]]


def walk(node: Node) -> Iterable[Node]:
    yield node
    for child in node.children:
        yield from walk(child)


def row_nodes(assembly: str) -> Node:
    normalized = SyntaxParser.normalize_whitespace(assembly)
    parsed = SyntaxParser(normalized).parse()
    return Node(K_SEQ, "", 0, mnemonic_node(normalized, parsed))


def base_mnemonic(token: str) -> str:
    token = token.rstrip()
    suffix = token.find("{")
    if suffix >= 0:
        token = token[:suffix]
    if token.startswith("B."):
        return "B"
    return token


def literal_flags(text: str) -> int:
    flags = 0
    if NUMERIC_RE.search(text):
        flags |= F_LIT_FIXED
    # These are source display literals, not semantic operand transforms.
    if re.search(r"\b(?:LSL|LSR|ASR|ROR)\b", text):
        flags |= F_LIT_SHIFT
    if re.search(r"\b(?:UXTB|UXTH|UXTW|UXTX|SXTB|SXTH|SXTW|SXTX)\b", text):
        flags |= F_LIT_EXTEND
    return flags


def prepare(node: Node) -> None:
    if node.kind == K_LIT:
        node.flags |= literal_flags(node.text)
    for child in node.children:
        prepare(child)


def flatten(root: Node) -> list[Node]:
    output: list[Node] = []

    def visit(node: Node) -> None:
        output.append(node)
        for child in node.children:
            visit(child)

    visit(root)
    return output


def string_hash(value: str) -> int:
    """Return the FNV-1a hash used for stable row source identity."""
    result = 0xCBF29CE484222325
    for byte in value.encode("utf-8"):
        result ^= byte
        result = (result * 0x100000001B3) & ((1 << 64) - 1)
    return result


def source_hash(row: dict) -> int:
    """Hash all source identity fields, not merely the row ID."""
    fields = (row["id"], row["kind"], row["encoding_name"], row["assembly"])
    return string_hash("\0".join(fields))


def c_escape(value: str) -> str:
    data = value.encode("utf-8")
    result = []
    for byte in data:
        if byte == 34:
            result.append('\\"')
        elif byte == 92:
            result.append('\\\\')
        elif byte == 10:
            result.append('\\n')
        elif 32 <= byte <= 126:
            result.append(chr(byte))
        else:
            result.append(f"\\x{byte:02x}")
    return "".join(result)


def c_byte_string(value: bytes, chunk_size: int = 72) -> list[str]:
    """Emit printable C string chunks without expanding one token per byte."""
    chunks = []
    for offset in range(0, len(value), chunk_size):
        chunks.append('"' + "".join(f"\\x{byte:02x}" for byte in value[offset : offset + chunk_size]) + '"')
    return chunks or ['""']


def c_base64_blob(value: bytes, chunk_size: int = 112) -> list[str]:
    """Emit a compact, pointer-free base64 blob for generated integer tables."""
    encoded = base64.b64encode(value).decode("ascii")
    return ['"' + encoded[offset : offset + chunk_size] + '"' for offset in range(0, len(encoded), chunk_size)] or ['""']


def canonical_json(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def digest_lines(lines: Iterable[str]) -> str:
    # The terminal LF is part of the canonical digest representation.
    return hashlib.sha256(("\n".join(lines) + "\n").encode("utf-8")).hexdigest()


def shape_signature(node: Node, generic: bool) -> list[object]:
    # Generic shapes preserve literals and AST flags but erase the spelling of
    # anchors, which distinguishes syntax topology from operand naming.
    text = "<anchor>" if generic and node.kind == K_ANCHOR else node.text
    return [node.kind, node.flags, text, [shape_signature(child, generic) for child in node.children]]


def normalize_source_bytes(source: Path) -> bytes:
    data = source.read_bytes()
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def write_lf(path: Path, text: str) -> None:
    path.write_bytes(text.replace("\r\n", "\n").replace("\r", "\n").encode("utf-8"))


def load_rows(source: Path) -> tuple[list[dict], str]:
    source_bytes = normalize_source_bytes(source)
    source_digest = hashlib.sha256(source_bytes).hexdigest()
    if source_digest != CANONICAL_SOURCE_SHA256:
        raise SystemExit(f"canonical source SHA-256 mismatch: expected {CANONICAL_SOURCE_SHA256}, got {source_digest}")
    try:
        text = source_bytes.decode("utf-8")
        rows_all = [json.loads(line) for line in text.split("\n") if line.strip()]
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise SystemExit(f"invalid canonical JSONL source: {error}") from error
    rows = [row for row in rows_all if row.get("apple_m1")]
    rows.sort(key=lambda row: row["id"])
    if len(rows) != 1695:
        raise SystemExit(f"expected 1695 Apple-M1 rows, got {len(rows)}")
    ids = [row.get("id") for row in rows]
    if any(not isinstance(row.get("id"), str) or not row["id"].startswith(RELEASE_PREFIX) for row in rows):
        raise SystemExit(f"all Apple-M1 rows must use release prefix {RELEASE_PREFIX!r}")
    if len(set(ids)) != len(ids):
        raise SystemExit("duplicate Apple-M1 row ID")
    kinds = {row.get("kind") for row in rows}
    kind_counts = {kind: sum(row.get("kind") == kind for row in rows) for kind in kinds}
    if kind_counts != {"canonical": 1523, "alias": 172}:
        raise SystemExit(f"unexpected Apple-M1 row kinds: {kind_counts}")
    encoding_names = [row.get("encoding_name") for row in rows]
    if any(not isinstance(name, str) or not name for name in encoding_names) or len(set(encoding_names)) != len(encoding_names):
        raise SystemExit("Apple-M1 encoding names must be unique and non-empty")
    canonical_encodings = {row["encoding_name"] for row in rows if row["kind"] == "canonical"}
    alias_encodings = {row["encoding_name"] for row in rows if row["kind"] == "alias"}
    if canonical_encodings & alias_encodings:
        raise SystemExit("canonical and alias encoding names overlap")
    for row in rows:
        if row["kind"] == "alias":
            alias_to = row.get("alias_to") or {}
            if alias_to.get("encoding_id") not in canonical_encodings:
                raise SystemExit(f"alias {row['id']} does not name a canonical encoding")
    input_digest = digest_lines(canonical_json(row) for row in rows)
    id_digest = digest_lines(row["id"] for row in rows)
    kind_digest = digest_lines(row["id"] + "\0" + row["kind"] for row in rows)
    if input_digest != INPUT_DIGEST:
        raise SystemExit(f"canonical input digest mismatch: expected {INPUT_DIGEST}, got {input_digest}")
    if id_digest != ID_DIGEST:
        raise SystemExit(f"canonical ID digest mismatch: expected {ID_DIGEST}, got {id_digest}")
    if kind_digest != KIND_DIGEST:
        raise SystemExit(f"canonical kind digest mismatch: expected {KIND_DIGEST}, got {kind_digest}")
    return rows, source_digest


def generate(source: Path, header: Path, jsonl: Path, manifest: Path) -> None:
    rows, source_digest = load_rows(source)
    roots: list[Node] = []
    for row in rows:
        root = row_nodes(row["assembly"])
        prepare(root)
        roots.append(root)
    input_digest = digest_lines(canonical_json(row) for row in rows)
    id_digest = digest_lines(row["id"] for row in rows)
    kind_digest = digest_lines(row["id"] + "\0" + row["kind"] for row in rows)

    # The pool is first-use ordered, so regeneration is stable while preserving
    # source occurrence order in each AST row.
    strings: dict[str, int] = {"": 0}
    pool = bytearray(b"\0")

    def intern(value: str) -> tuple[int, int]:
        if value in strings:
            return strings[value], len(value.encode())
        offset = len(pool)
        encoded = value.encode()
        pool.extend(encoded)
        pool.append(0)
        strings[value] = offset
        return offset, len(encoded)

    generated_nodes: list[dict] = []
    generated_child_indices: list[int] = []
    generated_rows: list[dict] = []
    mnemonic_ranges: dict[str, list[int]] = {}
    for row, root in zip(rows, roots):
        node_first = len(generated_nodes)
        flat = flatten(root)
        local_indices: dict[int, int] = {}

        def emit(node: Node) -> int:
            index = len(generated_nodes)
            local_indices[id(node)] = index
            text_offset, text_length = intern(node.text)
            generated_nodes.append({"kind": node.kind, "flags": node.flags, "text_offset": text_offset, "text_length": text_length,
                                    "child_first": 0, "child_count": 0,
                                    "numeric_count": len(NUMERIC_RE.findall(node.text)) if node.kind == K_LIT else 0})
            child_first = len(generated_child_indices)
            generated_child_indices.extend([0] * len(node.children))
            for child_offset, child in enumerate(node.children):
                child_index = len(generated_nodes)
                emit(child)
                generated_child_indices[child_first + child_offset] = child_index
            generated_nodes[index]["child_first"] = child_first
            generated_nodes[index]["child_count"] = len(node.children)
            return index

        emit(root)
        node_count = len(generated_nodes) - node_first
        anchors = sum(node.kind == K_ANCHOR for node in flat)
        anchor_min, anchor_max = anchor_bounds(root)
        source_token = SyntaxParser.normalize_whitespace(row["assembly"]).split(None, 1)[0]
        mnemonic = base_mnemonic(source_token)
        generated_rows.append({
            "source_index": len(generated_rows),
            "id": row["id"],
            "encoding_name": row.get("encoding_name"),
            "kind": row.get("kind"),
            "row_kind": 0 if row.get("kind") == "canonical" else 1,
            "assembly": SyntaxParser.normalize_whitespace(row["assembly"]),
            "mnemonic": mnemonic,
            "node_first": node_first,
            "node_count": node_count,
            "anchor_count": anchors,
            "anchor_min": anchor_min,
            "anchor_max": anchor_max,
            "source_hash": source_hash(row),
        })
        mnemonic_ranges.setdefault(mnemonic.upper(), []).append(len(generated_rows) - 1)

    # Row-level strings are interned before the pool is emitted below.  This
    # keeps every checked-in offset inside the declared pool bounds.
    for row in generated_rows:
        intern(row["id"])
        intern(row["assembly"])
        intern(row["mnemonic"])
        intern(row["encoding_name"])

    # Update child ranges are already global because emit is preorder.
    range_records: list[dict] = []
    candidates: list[int] = []
    for key in sorted(mnemonic_ranges):
        candidate_first = len(candidates)
        candidates.extend(mnemonic_ranges[key])
        offset, length = intern(key)
        range_records.append({"key_offset": offset, "key_length": length, "candidate_first": candidate_first,
                              "candidate_count": len(mnemonic_ranges[key])})

    # Keep the compiled projection compact enough for the self-hosting C
    # front-end.  The audit JSONL remains fully expanded; only integer tables
    # are packed here, with fixed-width little-endian records decoded by the
    # runtime's bounded accessors.
    import struct
    node_blob = bytearray()
    for node in generated_nodes:
        node_blob.extend(struct.pack("<IIIIHH", node["child_first"], node["child_count"], node["text_offset"],
                                     node["text_length"], node["kind"] | (node["flags"] << 8), node["numeric_count"]))
    child_blob = bytearray()
    for child in generated_child_indices:
        child_blob.extend(struct.pack("<I", child))
    row_blob = bytearray()
    for row in generated_rows:
        row_blob.extend(struct.pack("<IIIIIIIIIIIIIIQ", row["node_first"], row["node_count"],
                                    intern(row["id"])[0], intern(row["id"])[1],
                                    intern(row["assembly"])[0], intern(row["assembly"])[1],
                                    intern(row["mnemonic"])[0], intern(row["mnemonic"])[1],
                                    intern(row["encoding_name"])[0], intern(row["encoding_name"])[1],
                                    row["anchor_count"], row["anchor_min"], row["anchor_max"], row["row_kind"], row["source_hash"]))
    range_blob = bytearray()
    for record in range_records:
        range_blob.extend(struct.pack("<IIII", record["key_offset"], record["key_length"], record["candidate_first"], record["candidate_count"]))
    candidate_blob = bytearray()
    for candidate in candidates:
        candidate_blob.extend(struct.pack("<I", candidate))

    # Recompute the mechanical census and all integrity digests from the source
    # model.  These values are checked against the snapshot pins below before
    # any artifact is written, so claims cannot drift independently of input.
    all_nodes = [node for root in roots for node in walk(root)]
    exact_shapes = [canonical_json(shape_signature(root, generic=False)) for root in roots]
    generic_shapes = [canonical_json(shape_signature(root, generic=True)) for root in roots]
    exact_row_lines = [canonical_json({"id": row["id"], "kind": row["kind"],
                                      "encoding_name": row["encoding_name"], "assembly": row["assembly"],
                                      "shape": shape_signature(root, generic=False)})
                       for row, root in zip(rows, roots)]
    generic_row_lines = [canonical_json({"id": row["id"], "kind": row["kind"],
                                        "encoding_name": row["encoding_name"], "assembly": row["assembly"],
                                        "shape": shape_signature(root, generic=True)})
                         for row, root in zip(rows, roots)]

    def max_optional_depth(node: Node, depth: int = 0) -> int:
        current = depth + (node.kind == K_OPTIONAL)
        return max([current] + [max_optional_depth(child, current) for child in node.children])

    def max_delimiter_depth(node: Node, depth: int = 0) -> int:
        current = depth + (node.kind in (K_MEM, K_LIST, K_LANE, K_OPTIONAL, K_ALT))
        return max([current] + [max_delimiter_depth(child, current) for child in node.children])

    def max_top_level_comma_groups(root: Node) -> int:
        return 1 + sum(node.kind == K_LIT and "," in node.text for node in root.children)

    counts = {
        "rows": len(rows),
        "canonical_rows": sum(row.get("kind") == "canonical" for row in rows),
        "alias_rows": sum(row.get("kind") == "alias" for row in rows),
        "node_count": len(all_nodes),
        "optional_nodes": sum(node.kind == K_OPTIONAL for node in all_nodes),
        "alt_nodes": sum(node.kind == K_ALT for node in all_nodes),
        "mem_nodes": sum(node.kind == K_MEM for node in all_nodes),
        "mem_writeback_nodes": sum(bool(node.kind == K_MEM and node.flags & F_MEM_WRITEBACK) for node in all_nodes),
        "lane_nodes": sum(node.kind == K_LANE for node in all_nodes),
        "list_nodes": sum(node.kind == K_LIST for node in all_nodes),
        "anchor_occurrences": sum(node.kind == K_ANCHOR for node in all_nodes),
        "anchor_alternative_nodes": sum(bool(node.kind == K_ANCHOR and node.flags & F_ANCHOR_ALTERNATIVE) for node in all_nodes),
        "range_anchor_nodes": sum(bool(node.kind == K_ANCHOR and node.flags & F_ANCHOR_RANGE) for node in all_nodes),
        "mnemonic_optional_suffix_rows": sum(bool(node.kind == K_MNEMONIC and node.flags & F_MNEMONIC_OPTIONAL_SUFFIX) for node in all_nodes),
        "mnemonic_condition_rows": sum(bool(node.kind == K_MNEMONIC and node.flags & F_MNEMONIC_CONDITION) for node in all_nodes),
        "fixed_numeric_literal_occurrences": sum(len(NUMERIC_RE.findall(node.text)) for node in all_nodes if node.kind == K_LIT),
        "fixed_numeric_literal_spellings": len({node.text for node in all_nodes if node.kind == K_LIT and NUMERIC_RE.search(node.text)}),
        "fixed_numeric_literal_rows": sum(any(node.kind == K_LIT and NUMERIC_RE.search(node.text) for node in walk(root)) for root in roots),
        "max_total_ast_nodes": max(len(flatten(root)) for root in roots),
        "max_non_lit_non_seq_nodes": max(sum(node.kind not in (K_LIT, K_SEQ) for node in flatten(root)) for root in roots),
        "max_optional_depth": max(max_optional_depth(root) for root in roots),
        "max_delimiter_nesting": max(max_delimiter_depth(root) for root in roots),
        "max_top_level_comma_groups": max(max_top_level_comma_groups(root) for root in roots),
        "max_anchor_operands": max(row["anchor_count"] for row in generated_rows),
        "input_digest": input_digest,
        "id_digest": id_digest,
        "kind_digest": kind_digest,
        "source_sha256": source_digest,
        "generic_shape_count": len(set(generic_shapes)),
        "exact_shape_count": len(set(exact_shapes)),
        "generic_shape_digest": digest_lines(sorted(set(generic_shapes))),
        "generic_row_digest": digest_lines(generic_row_lines),
        "exact_shape_digest": digest_lines(sorted(set(exact_shapes))),
        "exact_row_digest": digest_lines(exact_row_lines),
    }
    expected_counts = {"rows": 1695, "canonical_rows": 1523, "alias_rows": 172,
                       "optional_nodes": 366, "alt_nodes": 33, "mem_nodes": 625,
                       "mem_writeback_nodes": 36, "list_nodes": 158, "lane_nodes": 157,
                       "anchor_occurrences": 6213, "anchor_alternative_nodes": 680,
                       "range_anchor_nodes": 28, "mnemonic_optional_suffix_rows": 55,
                       "mnemonic_condition_rows": 1, "fixed_numeric_literal_occurrences": 273,
                       "fixed_numeric_literal_spellings": 25, "fixed_numeric_literal_rows": 237,
                       "max_total_ast_nodes": 29, "max_non_lit_non_seq_nodes": 13,
                       "max_optional_depth": 2, "max_delimiter_nesting": 3,
                       "max_top_level_comma_groups": 5, "max_anchor_operands": 10,
                       "generic_shape_count": GENERIC_SHAPE_COUNT, "exact_shape_count": EXACT_SHAPE_COUNT,
                       "generic_shape_digest": GENERIC_SHAPE_DIGEST, "generic_row_digest": GENERIC_ROW_DIGEST,
                       "exact_shape_digest": EXACT_SHAPE_DIGEST, "exact_row_digest": EXACT_ROW_DIGEST}
    expected_counts.update({"input_digest": INPUT_DIGEST, "id_digest": ID_DIGEST, "kind_digest": KIND_DIGEST,
                            "source_sha256": CANONICAL_SOURCE_SHA256})
    for key, expected in expected_counts.items():
        if counts[key] != expected:
            raise SystemExit(f"canonical census mismatch for {key}: expected {expected!r}, got {counts[key]!r}")

    # C header.  All tables are flat integer arrays and therefore pointer-free.
    lines = [
        "/* Generated by aarch64_syntax_import.py; do not edit. */",
        "#ifndef BUSTER_AARCH64_SYNTAX_GENERATED_H",
        "#define BUSTER_AARCH64_SYNTAX_GENERATED_H",
        "#include <buster/lib/compiler/assembly/aarch64_syntax.h>",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_SCHEMA_VERSION {SCHEMA_VERSION}",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_ROW_COUNT {len(generated_rows)}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_NODE_COUNT {len(generated_nodes)}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_CHILD_INDEX_COUNT {len(generated_child_indices)}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_STRING_POOL_SIZE {len(pool)}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_RANGE_COUNT {len(range_records)}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CANDIDATE_COUNT {len(candidates)}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_CANONICAL_ROW_COUNT {counts['canonical_rows']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_ALIAS_ROW_COUNT {counts['alias_rows']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_OPTIONAL_NODE_COUNT {counts['optional_nodes']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_ALT_NODE_COUNT {counts['alt_nodes']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_MEM_NODE_COUNT {counts['mem_nodes']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_MEM_WRITEBACK_COUNT {counts['mem_writeback_nodes']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_LIST_NODE_COUNT {counts['list_nodes']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_LANE_NODE_COUNT {counts['lane_nodes']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_ANCHOR_OCCURRENCE_COUNT {counts['anchor_occurrences']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_ANCHOR_ALTERNATIVE_COUNT {counts['anchor_alternative_nodes']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_RANGE_ANCHOR_COUNT {counts['range_anchor_nodes']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_OPTIONAL_SUFFIX_COUNT {counts['mnemonic_optional_suffix_rows']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CONDITION_COUNT {counts['mnemonic_condition_rows']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_FIXED_NUMERIC_LITERAL_COUNT {counts['fixed_numeric_literal_occurrences']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_GENERIC_SHAPE_COUNT {counts['generic_shape_count']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_EXACT_SHAPE_COUNT {counts['exact_shape_count']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_MAX_TOTAL_AST_NODES {counts['max_total_ast_nodes']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_MAX_NON_LIT_NON_SEQ_NODES {counts['max_non_lit_non_seq_nodes']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_MAX_OPTIONAL_DEPTH {counts['max_optional_depth']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_MAX_DELIMITER_NESTING {counts['max_delimiter_nesting']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_MAX_TOP_LEVEL_COMMA_GROUPS {counts['max_top_level_comma_groups']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_MAX_ANCHOR_OPERANDS {counts['max_anchor_operands']}u",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_SOURCE_SHA256 \"{source_digest}\"",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_INPUT_DIGEST \"{input_digest}\"",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_GENERIC_SHAPE_DIGEST \"{counts['generic_shape_digest']}\"",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_GENERIC_ROW_DIGEST \"{counts['generic_row_digest']}\"",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_EXACT_SHAPE_DIGEST \"{counts['exact_shape_digest']}\"",
        f"#define BUSTER_AARCH64_SYNTAX_GENERATED_EXACT_ROW_DIGEST \"{counts['exact_row_digest']}\"",
        "",
        "static const char8 buster_aarch64_syntax_generated_string_pool[] =",
    ]
    lines.extend(["    " + chunk for chunk in c_byte_string(bytes(pool))])
    lines.extend([";", "",
                  f"#define BUSTER_AARCH64_SYNTAX_GENERATED_NODE_RECORD_BYTES 20u",
                  f"#define BUSTER_AARCH64_SYNTAX_GENERATED_ROW_RECORD_BYTES 64u",
                  f"#define BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_RANGE_RECORD_BYTES 16u",
                  f"#define BUSTER_AARCH64_SYNTAX_GENERATED_CHILD_INDEX_RECORD_BYTES 4u",
                  f"#define BUSTER_AARCH64_SYNTAX_GENERATED_MNEMONIC_CANDIDATE_RECORD_BYTES 4u",
                  "static const char8 buster_aarch64_syntax_generated_nodes_blob[] ="])
    lines.extend(["    " + chunk for chunk in c_base64_blob(bytes(node_blob))])
    lines.extend([";", "", "static const char8 buster_aarch64_syntax_generated_child_indices_blob[] ="])
    lines.extend(["    " + chunk for chunk in c_base64_blob(bytes(child_blob))])
    lines.extend([";", "", "static const char8 buster_aarch64_syntax_generated_rows_blob[] ="])
    lines.extend(["    " + chunk for chunk in c_base64_blob(bytes(row_blob))])
    lines.extend([";", "", "static const char8 buster_aarch64_syntax_generated_mnemonic_ranges_blob[] ="])
    lines.extend(["    " + chunk for chunk in c_base64_blob(bytes(range_blob))])
    lines.extend([";", "", "static const char8 buster_aarch64_syntax_generated_mnemonic_candidates_blob[] ="])
    lines.extend(["    " + chunk for chunk in c_base64_blob(bytes(candidate_blob))])
    lines.extend([";", "", "#endif /* BUSTER_AARCH64_SYNTAX_GENERATED_H */", ""])
    write_lf(header, "\n".join(lines))

    write_lf(jsonl, "\n".join(canonical_json(row) for row in generated_rows) + "\n")

    manifest_text = json.dumps({"schema_version": SCHEMA_VERSION,
                                    "source": {"file": source.name, "source_sha256": source_digest,
                                                "input_digest": input_digest, "id_digest": id_digest,
                                                "kind_digest": kind_digest},
                                    "counts": counts,
                                    "mnemonic_range_count": len(range_records),
                                    "mnemonic_candidate_count": len(candidates),
                                    "string_pool_bytes": len(pool),
                                    "artifacts": {"header": header.name, "jsonl": jsonl.name, "manifest": manifest.name}}, indent=2, sort_keys=True) + "\n"
    write_lf(manifest, manifest_text)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output_directory", type=Path)
    parser.add_argument("--check", action="store_true",
                        help="regenerate in a temporary directory and fail if checked-in artifacts drift")
    options = parser.parse_args()
    artifact_names = ("aarch64-syntax.generated.h", "aarch64-syntax.generated.jsonl", "aarch64-syntax-manifest.json")
    if options.check:
        with tempfile.TemporaryDirectory(prefix="aarch64-syntax-") as temporary:
            temporary_directory = Path(temporary)
            generate(options.source, temporary_directory / artifact_names[0],
                     temporary_directory / artifact_names[1], temporary_directory / artifact_names[2])
            drift = [name for name in artifact_names
                     if not (options.output_directory / name).exists() or
                     (options.output_directory / name).read_bytes() != (temporary_directory / name).read_bytes()]
        if drift:
            raise SystemExit("generated artifacts differ: " + ", ".join(drift))
        print("generated artifacts are up to date")
        return
    options.output_directory.mkdir(parents=True, exist_ok=True)
    generate(options.source, options.output_directory / artifact_names[0],
             options.output_directory / artifact_names[1], options.output_directory / artifact_names[2])


if __name__ == "__main__":
    main()
