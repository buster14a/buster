#!/usr/bin/env python3
"""Audit bounded compatibility entry points for staged internal API migrations."""

import argparse
import json
from pathlib import Path, PurePosixPath
import re
import sys

SCHEMA = "BUSTER_API_MIGRATIONS_V1"
DEFAULT_MANIFEST = ".github/api-migrations.json"
ENTRY_KEYS = {
    "id",
    "compatibility_symbol",
    "owner_issue",
    "removal_issue",
    "removal_condition",
    "scan_globs",
    "compatibility_owners",
    "allowed_callers",
}
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")


class DuplicateKeyError(ValueError):
    pass


def object_without_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise DuplicateKeyError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def relative_path_valid(value, allow_glob=False):
    result = isinstance(value, str) and bool(value) and "\\" not in value
    if result:
        path = PurePosixPath(value)
        result = not path.is_absolute() and all(part not in ("", ".", "..") for part in path.parts)
    if result and not allow_glob:
        result = not any(character in value for character in "*?[")
    return result


def positive_issue(value):
    return isinstance(value, int) and not isinstance(value, bool) and value > 0


def exact_count_map(value, field, migration_id, errors):
    result = {}
    if not isinstance(value, dict):
        errors.append(f"{migration_id}: {field} must be an object mapping paths to positive token counts")
    else:
        for path, count in value.items():
            if not relative_path_valid(path):
                errors.append(f"{migration_id}: {field} contains unsafe path {path!r}")
            elif not isinstance(count, int) or isinstance(count, bool) or count <= 0:
                errors.append(f"{migration_id}: {field}[{path!r}] must be a positive integer")
            else:
                result[path] = count
    return result


def load_manifest(path, errors):
    result = None
    try:
        text = path.read_text(encoding="utf-8")
        result = json.loads(text, object_pairs_hook=object_without_duplicate_keys)
    except (OSError, UnicodeError, json.JSONDecodeError, DuplicateKeyError) as error:
        errors.append(f"{path}: {error}")
    return result


def paths_for_globs(root, patterns, migration_id, errors):
    result = set()
    for pattern in patterns:
        if not relative_path_valid(pattern, allow_glob=True):
            errors.append(f"{migration_id}: scan_globs contains unsafe pattern {pattern!r}")
            continue
        matched = False
        for path in root.glob(pattern):
            if not path.is_file():
                continue
            matched = True
            try:
                relative = path.relative_to(root).as_posix()
                resolved = path.resolve(strict=True)
                resolved.relative_to(root.resolve(strict=True))
            except (OSError, ValueError) as error:
                errors.append(f"{migration_id}: unsafe scanned path {path}: {error}")
                continue
            if path.is_symlink():
                errors.append(f"{migration_id}: scan_globs may not traverse symbolic link {relative}")
                continue
            result.add(relative)
        if not matched:
            errors.append(f"{migration_id}: scan_glob {pattern!r} matched no regular files")
    return result


def token_count(path, pattern, errors):
    result = 0
    try:
        result = len(pattern.findall(path.read_text(encoding="utf-8")))
    except (OSError, UnicodeError) as error:
        errors.append(f"{path}: {error}")
    return result


def audit_repository(root, manifest_relative=DEFAULT_MANIFEST):
    root = Path(root)
    errors = []
    manifest_path = root / manifest_relative
    manifest = load_manifest(manifest_path, errors)
    if manifest is None:
        return errors
    if not isinstance(manifest, dict):
        return [f"{manifest_path}: root must be an object"]
    expected_root_keys = {"schema", "migrations"}
    if set(manifest) != expected_root_keys:
        errors.append(f"{manifest_path}: root keys must be exactly {sorted(expected_root_keys)}")
    if manifest.get("schema") != SCHEMA:
        errors.append(f"{manifest_path}: schema must be {SCHEMA!r}")
    migrations = manifest.get("migrations")
    if not isinstance(migrations, list):
        errors.append(f"{manifest_path}: migrations must be an array")
        return errors

    identifiers = set()
    symbols = set()
    for index, migration in enumerate(migrations):
        fallback_id = f"migration[{index}]"
        if not isinstance(migration, dict):
            errors.append(f"{fallback_id}: entry must be an object")
            continue
        migration_id = migration.get("id", fallback_id)
        if set(migration) != ENTRY_KEYS:
            errors.append(f"{migration_id}: keys must be exactly {sorted(ENTRY_KEYS)}")
        if not isinstance(migration_id, str) or not migration_id or not re.fullmatch(r"[a-z0-9][a-z0-9-]*", migration_id):
            errors.append(f"{fallback_id}: id must be a lowercase dash-separated identifier")
        elif migration_id in identifiers:
            errors.append(f"{migration_id}: duplicate migration id")
        else:
            identifiers.add(migration_id)

        symbol = migration.get("compatibility_symbol")
        if not isinstance(symbol, str) or not IDENTIFIER.fullmatch(symbol):
            errors.append(f"{migration_id}: compatibility_symbol must be one C-style identifier")
            symbol = None
        elif symbol in symbols:
            errors.append(f"{migration_id}: compatibility_symbol {symbol!r} is already registered")
        else:
            symbols.add(symbol)

        owner_issue = migration.get("owner_issue")
        removal_issue = migration.get("removal_issue")
        if not positive_issue(owner_issue):
            errors.append(f"{migration_id}: owner_issue must be a positive GitHub issue number")
        if not positive_issue(removal_issue):
            errors.append(f"{migration_id}: removal_issue must be a positive GitHub issue number")
        if positive_issue(owner_issue) and owner_issue == removal_issue:
            errors.append(f"{migration_id}: removal_issue must be distinct from owner_issue")
        removal_condition = migration.get("removal_condition")
        if not isinstance(removal_condition, str) or not removal_condition.strip():
            errors.append(f"{migration_id}: removal_condition must be nonempty")

        globs = migration.get("scan_globs")
        valid_globs = []
        if not isinstance(globs, list) or not globs:
            errors.append(f"{migration_id}: scan_globs must be a nonempty array")
        else:
            for pattern_value in globs:
                if not isinstance(pattern_value, str):
                    errors.append(f"{migration_id}: scan_globs entries must be strings")
                else:
                    valid_globs.append(pattern_value)

        owners = exact_count_map(migration.get("compatibility_owners"), "compatibility_owners", migration_id, errors)
        callers = exact_count_map(migration.get("allowed_callers"), "allowed_callers", migration_id, errors)
        if not owners:
            errors.append(f"{migration_id}: compatibility_owners must name the narrow compatibility implementation")
        if not callers:
            errors.append(
                f"{migration_id}: no old callers remain; remove the compatibility symbol and registry entry in the final migration PR"
            )
        overlap = sorted(set(owners) & set(callers))
        if overlap:
            errors.append(f"{migration_id}: owner and caller paths overlap: {', '.join(overlap)}")

        scanned = paths_for_globs(root, valid_globs, migration_id, errors)
        expected = dict(owners)
        expected.update(callers)
        missing = sorted(set(expected) - scanned)
        for path in missing:
            errors.append(f"{migration_id}: registered path {path!r} is not matched by scan_globs")

        if symbol:
            token = re.compile(rf"(?<![A-Za-z0-9_]){re.escape(symbol)}(?![A-Za-z0-9_])")
            actual = {}
            for relative in sorted(scanned):
                count = token_count(root / relative, token, errors)
                if count:
                    actual[relative] = count
            for path, count in sorted(actual.items()):
                if path not in expected:
                    errors.append(
                        f"{migration_id}: unregistered use of {symbol} in {path} ({count} token occurrence{'s' if count != 1 else ''})"
                    )
            for path, expected_count in sorted(expected.items()):
                actual_count = actual.get(path, 0)
                if actual_count != expected_count:
                    errors.append(
                        f"{migration_id}: {path} has {actual_count} {symbol} token occurrences; registry requires {expected_count}"
                    )
    return errors


def main(arguments=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    options = parser.parse_args(arguments)
    errors = audit_repository(options.root, options.manifest)
    for error in errors:
        print(error, file=sys.stderr)
    if not errors:
        manifest = load_manifest(options.root / options.manifest, [])
        count = len(manifest["migrations"]) if isinstance(manifest, dict) and isinstance(manifest.get("migrations"), list) else 0
        print(f"API migration compatibility registry valid: {count} active migration{'s' if count != 1 else ''}")
    return int(bool(errors))


if __name__ == "__main__":
    sys.exit(main())
