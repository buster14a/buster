#!/usr/bin/env python3
"""Apply one research-only expression-query layout to a disposable checkout.

Usage:
  python3 hotcold.py REPO_ROOT [--poison] [--dry-run]

The checkout must be the exact clean baseline for the two affected files. All
source identities and replacement counts are checked before either file is
written. This script does not build, run, commit, or publish anything.

--poison fills the uninitialized payload with 0xa5 for correctness diagnostics.
It changes construction work and must not be used in a timed candidate build.
"""

import argparse
import hashlib
import json
import pathlib
import subprocess


REVISION = "8f67df736f13d4edc055110a7a6d619a00a22eaf"
SOURCE_IDENTITIES = {
    "src/buster/lib/compiler/frontend/c/c_internal.h":
        "5368d5beb3de00bf310bb389095577479927fedcbee48f088818e6a394c39271",
    "src/buster/lib/compiler/frontend/c/c_parse.c":
        "9f65229d902318ceb3ee5d32900232365285e95154a2e5835c4f5c98ce8161b0",
}


def sha256(source):
    return hashlib.sha256(source).hexdigest()


def replace_exact(source, old, new, label, replacements):
    actual_count = source.count(old)
    if actual_count != 1:
        raise RuntimeError(
            f"{label}: expected exactly one replacement, found {actual_count}"
        )
    replacements.append({"label": label, "count": actual_count})
    return source.replace(old, new, 1)


def patch_header(source, replacements):
    source = replace_exact(source, """#define C_PARSE_EXPRESSION_QUERY_CONSTANT 8u

typedef struct CParseExpressionQuery CParseExpressionQuery;
struct CParseExpressionQuery
{
    u32 end;
    CScopeId scope;
    CTypeId type;
    u32 flags;
};

struct CTypeParseMachine
{
    CParseExpressionQuery* expression_queries;
""", """#define C_PARSE_EXPRESSION_QUERY_CONSTANT 8u

BUSTER_CT_CHECK((C_PARSE_EXPRESSION_QUERY_VALID | C_PARSE_EXPRESSION_QUERY_CHECKED |
                 C_PARSE_EXPRESSION_QUERY_RUNTIME | C_PARSE_EXPRESSION_QUERY_CONSTANT) <= UINT8_MAX);

typedef struct CParseExpressionQuery CParseExpressionQuery;
struct CParseExpressionQuery
{
    u32 end;
    CScopeId scope;
    CTypeId type;
};
BUSTER_CT_CHECK(sizeof(CParseExpressionQuery) == 12);

struct CTypeParseMachine
{
    CParseExpressionQuery* expression_queries;
    u8* expression_query_flags;
""", "header: byte flags sidecar and 12-byte payload", replacements)
    return source


def patch_parser(source, poison, replacements):
    source = replace_exact(source, """            if (query && query->end == task->end && query->scope.value == scope.value &&
                (query->flags & ~C_PARSE_EXPRESSION_QUERY_CHECKED) == (flags & ~C_PARSE_EXPRESSION_QUERY_CHECKED) &&
                (query->flags & flags) == flags && query->type.value < result->type_count)
""", """            u32 query_flags = query ? machine->expression_query_flags[task->start - machine->expression_query_start] : 0u;
            if (query &&
                (query_flags & ~C_PARSE_EXPRESSION_QUERY_CHECKED) == (flags & ~C_PARSE_EXPRESSION_QUERY_CHECKED) &&
                (query_flags & flags) == flags && query->end == task->end && query->scope.value == scope.value &&
                query->type.value < result->type_count)
""", "task reader: check flags before any payload field", replacements)

    source = replace_exact(source, """    if (query && query->end == end && query->scope.value == scope.value &&
        (query->flags & ~C_PARSE_EXPRESSION_QUERY_CHECKED) == (flags & ~C_PARSE_EXPRESSION_QUERY_CHECKED) &&
        (query->flags & flags) == flags && query->type.value < result->type_count)
""", """    u32 query_flags = query ? machine->expression_query_flags[start - machine->expression_query_start] : 0u;
    if (query &&
        (query_flags & ~C_PARSE_EXPRESSION_QUERY_CHECKED) == (flags & ~C_PARSE_EXPRESSION_QUERY_CHECKED) &&
        (query_flags & flags) == flags && query->end == end && query->scope.value == scope.value &&
        query->type.value < result->type_count)
""", "root reader: check flags before any payload field", replacements)

    source = replace_exact(source, """        if (query && valid && !machine->expression_constraint.length && result->diagnostic_count == checkpoint.diagnostic_count)
            *query = (CParseExpressionQuery){.end = end, .scope = scope, .type = *type_out, .flags = flags};
""", """        if (query && valid && !machine->expression_constraint.length && result->diagnostic_count == checkpoint.diagnostic_count)
        {
            *query = (CParseExpressionQuery){.end = end, .scope = scope, .type = *type_out};
            machine->expression_query_flags[start - machine->expression_query_start] = (u8)flags;
        }
""", "writer: assign full payload before publishing flags", replacements)

    initialization = """        machine->expression_queries = arena_allocate(machine->scratch_arena, CParseExpressionQuery, declaration->body_token_count);
        machine->expression_query_flags = arena_allocate(machine->scratch_arena, u8, declaration->body_token_count);
        memset(machine->expression_query_flags, 0, declaration->body_token_count);
"""
    if poison:
        initialization += """        // Research diagnostic only: exercise misses with nonzero unpublished payload.
        memset(machine->expression_queries, 0xa5, sizeof(*machine->expression_queries) * declaration->body_token_count);
"""
    source = replace_exact(source, """        machine->expression_queries = arena_allocate(machine->scratch_arena, CParseExpressionQuery, declaration->body_token_count);
        memset(machine->expression_queries, 0, sizeof(*machine->expression_queries) * declaration->body_token_count);
""", initialization, "construction: allocate 12N payload plus N zeroed flags", replacements)

    source = replace_exact(source, """        machine->expression_queries = 0;
        machine->expression_query_result = 0;
""", """        machine->expression_queries = 0;
        machine->expression_query_flags = 0;
        machine->expression_query_result = 0;
""", "teardown: clear both borrowed cache pointers", replacements)

    if "query->flags" in source or ".flags = flags};" in source:
        raise RuntimeError("a stale inline query flag access remains")
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repo_root", type=pathlib.Path)
    parser.add_argument("--poison", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    root = args.repo_root.resolve(strict=True)
    revision = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "HEAD"],
        check=True, text=True, capture_output=True,
    ).stdout.strip()
    if revision != REVISION:
        raise RuntimeError(f"wrong baseline revision: expected {REVISION}, got {revision}")

    original = {}
    for relative, expected_sha in SOURCE_IDENTITIES.items():
        content = (root / relative).read_bytes()
        actual_sha = sha256(content)
        if actual_sha != expected_sha:
            raise RuntimeError(
                f"{relative}: wrong baseline SHA-256: expected {expected_sha}, got {actual_sha}"
            )
        original[relative] = content

    replacements = []
    header, parser_source = SOURCE_IDENTITIES
    updated = {
        header: patch_header(original[header].decode("utf-8"), replacements).encode("utf-8"),
        parser_source: patch_parser(
            original[parser_source].decode("utf-8"), args.poison, replacements
        ).encode("utf-8"),
    }
    if not args.dry_run:
        for relative, content in updated.items():
            (root / relative).write_bytes(content)

    print(json.dumps({
        "revision": revision,
        "action": "dry-run" if args.dry_run else "applied",
        "poison_payload": args.poison,
        "timing_eligible": not args.poison,
        "layout": {
            "payload_bytes_per_slot": 12,
            "flag_bytes_per_slot": 1,
            "construction_zero_bytes_per_slot": 1,
            "additional_machine_pointer_bytes_on_64_bit": 8,
        },
        "files": [{
            "path": relative,
            "baseline_sha256": sha256(original[relative]),
            "candidate_sha256": sha256(updated[relative]),
        } for relative in SOURCE_IDENTITIES],
        "replacements": replacements,
    }, indent=2))


if __name__ == "__main__":
    main()
