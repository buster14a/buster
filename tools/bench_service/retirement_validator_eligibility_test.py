#!/usr/bin/env python3
"""Reproduce schema-2 validator eligibility and exercise the C projection.

This intentionally uses the repository's small ContractTests fixture. It proves
that the validator's row-bound applicability and skip evidence reaches the C
service projection, and that the C per-row #1020 configuration_sha256 equals
this module's row_configuration_digest reference for every row; it does not
stand in for an authenticated full census. The reference lives here, outside
the trusted native_retirement_contract module, and only reads that module's
canonical_digest and ROW_IDENTITY_FIELDS.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys


REPOSITORY = Path(__file__).resolve().parents[2]
TOOLS = REPOSITORY / "tools"
sys.path.insert(0, str(TOOLS))

import native_retirement_contract_test as contract_test
import native_retirement_performance_binding as binding


EXPECTED_PROBE_OUTPUT = "VALIDATOR_ELIGIBILITY rows=192 eligible=160 skipped=32 supplement=0"
# Option 3 (#36): the supplement fixture's two failed direct references
# (census rows 0 and 4, native-host allocator-none rows) are compiler-ineligible.
EXPECTED_SUPPLEMENT_PROBE_OUTPUT = "VALIDATOR_ELIGIBILITY rows=192 eligible=158 skipped=32 supplement=2"
SUPPLEMENT_RESOLVED_ROWS = [0, 4]
# (A1) Only compiler-eligible rows on the pinned native-host target
# (binding.NATIVE_TIMED_TARGET, x86_64-unknown-linux-gnu) are timed or carry
# generated runtime, so the unavailable ledger record names another target
# (census rows 16..31) and leaves the native-host rows 0..15 admitted.
INAPPLICABLE_RECORD = (
    "tests/unit.c", "x86_64-apple-ios", "platform-inapplicable",
    "source-registration-test",
)
UNAVAILABLE_RECORD = (
    "tests/unit.c", "aarch64-unknown-linux-gnu", "unavailable", "missing-native-oracle",
)


# Shared with retirement_prepare_tests.c: the C service derives the same
# digest from the equivalent rows.tsv line.
GOLDEN_IDENTITY = (
    "0", "tests/a.c", "x86_64-unknown-linux-gnu", "systemv-x86_64", "baseline",
    "fixture-features", "none", "local-backed-canonical", "0", "compiler-default",
    "supported-object-zero-fallback", "semantic-gate-509", "semantic-gate-509", "none",
    "groups/0/none.argv",
)
GOLDEN_CONFIGURATION_SHA256 = "b8f9e4810f3286d61bc6dcee5fa0c72fa9d873d8e32f5b84f739e16bd426a585"
ESCAPED_CONFIGURATION_SHA256 = "ad3528db6f10ea70b67c3b86ef3b8dcb41654c2416ab476c4126385ca3246521"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def row_configuration_digest(identity):
    """Return one row's #508 ``configuration_sha256`` (#1020).

    ``identity`` is ``rows_identity[row]`` from native_retirement_contract's
    per-shard ``validate``: the ``ROW_IDENTITY_FIELDS`` values in order,
    without the ``row`` key (so it includes ``group`` and ``argv_evidence``
    and excludes ``selected``). The merged schema-2 report binds that map
    through ``rows_identity_fields`` and ``rows_identity_sha256``. The digest
    is the contract's ``canonical_digest`` of the tuple as a JSON list, with
    no extra fields; the C service projection derives the same bytes from
    pinned rows.tsv.
    """
    contract = contract_test.contract
    values = list(identity)
    require(len(values) == len(contract.ROW_IDENTITY_FIELDS) - 1,
            "row configuration identity has the wrong arity")
    require(all(isinstance(value, str) for value in values),
            "row configuration identity values must be strings")
    return contract.canonical_digest(values)


def check_row_configuration_reference():
    """Golden and serialization checks for row_configuration_digest."""
    contract = contract_test.contract
    require(tuple(field for field in contract.ROW_IDENTITY_FIELDS if field != "row") == (
        "group", "fixture", "target", "target_abi", "cpu", "cpu_features", "allocator",
        "frontend_lowering", "PIC", "fixture_recipe", "compile_obligation", "link_obligation",
        "execution_obligation", "diagnostic_obligation", "argv_evidence"),
            "ROW_IDENTITY_FIELDS changed; the #1020 configuration definition must be re-reviewed")
    require(row_configuration_digest(GOLDEN_IDENTITY) == GOLDEN_CONFIGURATION_SHA256,
            "row_configuration_digest golden changed")
    require(row_configuration_digest(list(GOLDEN_IDENTITY)) == GOLDEN_CONFIGURATION_SHA256,
            "row_configuration_digest depends on the identity container type")
    encoded = ("[" + ",".join('"' + value + '"' for value in GOLDEN_IDENTITY) + "]").encode("ascii")
    require(sha256(encoded) == GOLDEN_CONFIGURATION_SHA256,
            "row_configuration_digest is not the compact JSON list digest")
    escaped = ("0", 'tests/q"b\\s/x.c') + GOLDEN_IDENTITY[2:]
    encoded = ('["0","tests/q\\"b\\\\s/x.c",' +
               ",".join('"' + value + '"' for value in GOLDEN_IDENTITY[2:]) + "]").encode("ascii")
    require(sha256(encoded) == ESCAPED_CONFIGURATION_SHA256 and
            row_configuration_digest(escaped) == ESCAPED_CONFIGURATION_SHA256,
            "row_configuration_digest does not escape quote and backslash canonically")
    for malformed in (GOLDEN_IDENTITY[:-1], ("0",) + GOLDEN_IDENTITY, GOLDEN_IDENTITY[:-1] + (1,)):
        try:
            row_configuration_digest(malformed)
        except RuntimeError:
            continue
        raise RuntimeError(f"row_configuration_digest accepted malformed identity {malformed!r}")


def run_validator(shards, output, supplements=False):
    command = [
        sys.executable,
        str(TOOLS / "native_retirement_contract.py"),
        "validate-shards",
        *(str(shard) for shard in shards),
        "--out", str(output),
        "--require-clean-candidate",
    ]
    if supplements:
        command.append("--reference-supplements")
    result = subprocess.run(command, cwd=REPOSITORY, check=False,
                            capture_output=True, text=True)
    require(result.returncode == 0,
            "schema-2 validate-shards failed:\n" + result.stdout + result.stderr)
    require(output.is_file(), "schema-2 validate-shards did not write its report")
    try:
        report = json.loads(output.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RuntimeError(f"schema-2 validator report is unreadable: {error}") from error
    require(report.get("schema") == 2, "validator did not emit schema 2")
    require(report.get("require_clean_candidate") is True,
            "validator report does not retain the clean-candidate requirement")
    require(report.get("clean_candidate") is True,
            "fixture candidate rows are not clean")
    require(report.get("rows_validated") == 192,
            "fixture does not cover its complete 192-row matrix")
    require(report.get("applicability_skip_rows") ==
            list(range(16, 32)) + list(range(128, 144)),
            "validator did not derive the source-ledger unavailable and platform skip sets")
    require(report.get("acceptance_failure_rows") == list(range(16, 32)),
            "validator acceptance failures do not match the unavailable ledger rows")
    require(report.get("clean_acceptance") is False,
            "validator did not retain the unresolved unavailable acceptance state")
    return report


def independently_replay_python(root, report, shard):
    _row_fields, census_rows = contract_test.read_table(shard / "rows.tsv")
    by_row = {}
    for classification in report["applicability_classes"]:
        for row in report["applicability_rows_by_class"][classification]:
            require(row not in by_row, "validator applicability classes overlap")
            by_row[row] = classification
    require(set(by_row) == set(range(len(census_rows))),
            "validator report is not a complete applicability partition")
    skip_rows = set(report["applicability_skip_rows"])

    projection_evidence = binding._check_validator_projection_evidence(
        root, report, census_rows, by_row, skip_rows)
    binding._replay_validator_report(root, report, projection_evidence)
    return projection_evidence


def write_profile(path, artifacts):
    keys = (
        ("support-declaration-sha256", "support"),
        ("census-inputs-sha256", "inputs"),
        ("census-rows-sha256", "rows"),
        ("census-manifest-sha256", "manifest"),
        ("validator-source-applicability-sha256", "source_applicability"),
        ("validator-report-sha256", "report"),
        ("validator-applicability-sha256", "applicability"),
        ("validator-skips-sha256", "skips"),
        ("performance-rows-sha256", "performance_rows"),
    )
    data = "".join(f"{key}={sha256(artifacts[name].read_bytes())}\n"
                   for key, name in keys if name in artifacts)
    # Option 3: a self-test census resolves rows only under this explicit
    # test-only pin of its approved set (full-census uses the compiled pin).
    if "supplement_pin" in artifacts:
        data += f"validator-supplement-resolved-sha256={artifacts['supplement_pin']}\n"
    data = data.encode("ascii")
    if path.exists():
        path.chmod(stat.S_IMODE(path.stat().st_mode) | stat.S_IWUSR)
    path.write_bytes(data)
    path.chmod(0o444)


def make_read_only(*paths):
    for path in paths:
        info = path.lstat()
        require(stat.S_ISREG(info.st_mode) and not stat.S_ISLNK(info.st_mode),
                f"probe evidence is not a regular file: {path.name}")
        require(info.st_nlink == 1, f"probe evidence has unexpected hard links: {path.name}")
        path.chmod(0o444)


def probe_result(probe, artifacts):
    command = [
        str(probe),
        str(artifacts["profile"]),
        str(artifacts["support"]),
        str(artifacts["source_applicability"]),
        str(artifacts["inputs"]),
        str(artifacts["rows"]),
        str(artifacts["manifest"]),
        str(artifacts["report"]),
        str(artifacts["applicability"]),
        str(artifacts["skips"]),
    ]
    if "performance_rows" in artifacts:
        command += [str(artifacts["performance_rows"]), str(POPULATION_NATIVE_TARGET_ID)]
    return subprocess.run(command, cwd=REPOSITORY, check=False,
                          capture_output=True, text=True)


def expected_configurations(report, shard):
    """Python reference #1020 configuration_sha256 values, in row order."""
    _row_fields, census_rows = contract_test.read_table(shard / "rows.tsv")
    identities = contract_test.contract.field_map(
        census_rows, contract_test.contract.ROW_IDENTITY_FIELDS, "row")
    require(report["rows_identity_fields"] == list(contract_test.contract.ROW_IDENTITY_FIELDS),
            "validator report does not bind the expected rows_identity fields")
    require(contract_test.contract.canonical_map_digest(identities) == report["rows_identity_sha256"],
            "rows.tsv identity tuples do not match the report's rows_identity_sha256")
    require(list(identities) == [str(row) for row in range(len(census_rows))],
            "rows.tsv identity map is not in row order")
    validated = contract_test.contract.validate(shard)["rows_identity"]
    require({key: list(value) for key, value in validated.items()} ==
            {key: list(value) for key, value in identities.items()},
            "validate() rows_identity differs from the rows.tsv identity tuples")
    for row in census_rows:
        raw = [row[field] for field in contract_test.contract.ROW_IDENTITY_FIELDS if field != "row"]
        require(sha256(json.dumps(raw, separators=(",", ":")).encode("ascii")) ==
                row_configuration_digest(identities[row["row"]]),
                f"row {row['row']} configuration digest is not its compact rows.tsv tuple")
    return [row_configuration_digest(identities[str(row)])
            for row in range(len(census_rows))]


def census_rows_of(shard):
    _row_fields, census_rows = contract_test.read_table(shard / "rows.tsv")
    return census_rows


def fixture_supplement_pin(shard):
    """The supplement fixture's explicit test-only pin: rows 0 and 4."""
    return (len(SUPPLEMENT_RESOLVED_ROWS),
            binding._supplement_identity_digest(census_rows_of(shard), SUPPLEMENT_RESOLVED_ROWS))


def supplement_disposition(report, shard):
    """The binding's option-3 supplement-resolved set for this report.

    The supplement fixture is judged against its test-only pin; the plain
    fixture has no pin, so it may resolve no row.
    """
    approved = fixture_supplement_pin(shard) if report["reference_supplement_sha256"] else None
    return binding._supplement_resolved_rows(report, census_rows_of(shard),
                                             set(report["applicability_skip_rows"]), approved)


def expected_ineligible(report, artifacts, shard):
    """Python reference for the C probe's VALIDATOR_INELIGIBLE lines.

    A skipped row's proof hashes its applicability-skips.tsv line; a
    supplement-resolved row's proof hashes the report's supplement digest for
    its shard (group % shards) and its applicability.tsv line. Each is under
    its own domain and the pinned report digest, and the reason strings are
    the binding's. (The binding's own per-row proof is the supplement manifest
    and object digests; the rule, not the proof bytes, is shared.)
    """
    report_sha = sha256(artifacts["report"].read_bytes()).encode("ascii")
    supplements = report["reference_supplement_sha256"]

    def lines_by_row(path):
        lines = path.read_bytes().split(b"\n")
        require(lines[-1] == b"", f"{path.name} does not end with a newline")
        return {int(line.split(b"\t", 1)[0]): line for line in lines[1:-1]}

    def proof(domain, line, supplement=None):
        prefix = b"" if supplement is None else supplement.encode("ascii") + b"\0"
        return sha256(domain + b"\0" + report_sha + b"\0" + prefix + line)

    skips = lines_by_row(artifacts["skips"])
    applicability = lines_by_row(artifacts["applicability"])
    require(sorted(skips) == report["applicability_skip_rows"],
            "applicability-skips.tsv rows differ from the report skip set")
    resolved = supplement_disposition(report, shard)
    lines = []
    for row in sorted(set(skips) | set(resolved)):
        if row in skips:
            reason = binding.NONEXECUTED_INELIGIBLE_REASON
            digest = proof(b"bq-retirement-validator-skip-v1", skips[row])
        else:
            reason = binding.SUPPLEMENT_INELIGIBLE_REASON
            digest = proof(b"bq-retirement-validator-supplement-v1", applicability[row],
                           supplements[(row // 4) % report["shards"]])
        lines.append(f"VALIDATOR_INELIGIBLE row={row} reason={reason} proof={digest}")
    return lines


def expect_probe_success(probe, artifacts, configurations, report, shard,
                         expected_summary=EXPECTED_PROBE_OUTPUT):
    result = probe_result(probe, artifacts)
    require(result.returncode == 0,
            "compiled C eligibility probe rejected the genuine fixture:\n" +
            result.stdout + result.stderr)
    lines = result.stdout.splitlines()
    require(lines[:1] == [expected_summary],
            "compiled C eligibility probe returned an unexpected projection: " +
            result.stdout.strip())
    expected = [f"VALIDATOR_CONFIGURATION row={row} sha256={digest}"
                for row, digest in enumerate(configurations)]
    require([line for line in lines if line.startswith("VALIDATOR_CONFIGURATION ")] == expected,
            "C configuration_sha256 projection differs from row_configuration_digest")
    require([line for line in lines if line.startswith("VALIDATOR_INELIGIBLE ")] ==
            expected_ineligible(report, artifacts, shard),
            "C compiler-ineligible rows, reasons or proofs differ from the binding's disposition")


def expect_tamper_rejected(probe, artifacts, path, replacement, label):
    original = path.read_bytes()
    changed = replacement(original)
    require(changed != original, f"{label} tamper did not change the evidence")
    original_mode = stat.S_IMODE(path.stat().st_mode)
    profile = artifacts["profile"]
    original_profile = profile.read_bytes()
    original_profile_mode = stat.S_IMODE(profile.stat().st_mode)
    try:
        path.chmod(original_mode | stat.S_IWUSR)
        path.write_bytes(changed)
        path.chmod(original_mode)
        # Re-pin the changed bytes so the probe must reject their contents or
        # row relationship instead of merely noticing a stale digest.
        write_profile(profile, artifacts)
        result = probe_result(probe, artifacts)
        require(result.returncode != 0,
                f"compiled C eligibility probe accepted tampered {label} evidence")
    finally:
        path.chmod(original_mode | stat.S_IWUSR)
        path.write_bytes(original)
        path.chmod(original_mode)
        profile.chmod(original_profile_mode | stat.S_IWUSR)
        profile.write_bytes(original_profile)
        profile.chmod(original_profile_mode)


def replace_once(data, old, new, label):
    require(old in data, f"expected a {label} marker in fixture evidence")
    return data.replace(old, new, 1)


def mutate_report(data, mutation):
    try:
        report = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot mutate validator report JSON: {error}") from error
    mutation(report)
    return (json.dumps(report, ensure_ascii=True, indent=2, sort_keys=True) + "\n").encode("utf-8")


def mutate_applicability_cell(data, row_number, updates):
    lines = data.decode("ascii").splitlines()
    columns = lines[0].split("\t")
    positions = {name: index for index, name in enumerate(columns)}
    found = False
    for index in range(1, len(lines)):
        fields = lines[index].split("\t")
        if fields[positions["row"]] == str(row_number):
            for name, value in updates.items():
                fields[positions[name]] = value
            lines[index] = "\t".join(fields)
            found = True
            break
    require(found, f"applicability fixture has no row {row_number}")
    return ("\n".join(lines) + "\n").encode("ascii")


def rebind_applicability_digest(report_data, applicability_data):
    return mutate_report(report_data, lambda report:
        report.__setitem__("applicability_sha256", sha256(applicability_data)))


def mutate_manifest_gap_summary(data, row_numbers):
    properties = dict(line.split("=", 1) for line in data.decode("ascii").splitlines())
    properties["supported_gap_count"] = str(len(row_numbers))
    properties["supported_gap_sha256"] = contract_test.contract.canonical_rows_digest(row_numbers)
    return "".join(f"{key}={value}\n" for key, value in properties.items()).encode("ascii")


def expect_tamper_rejected_many(probe, artifacts, replacements, label):
    originals = {name: artifacts[name].read_bytes() for name in replacements}
    modes = {name: stat.S_IMODE(artifacts[name].stat().st_mode) for name in replacements}
    profile = artifacts["profile"]
    original_profile = profile.read_bytes()
    original_profile_mode = stat.S_IMODE(profile.stat().st_mode)
    try:
        for name, changed in replacements.items():
            path = artifacts[name]
            path.chmod(modes[name] | stat.S_IWUSR)
            path.write_bytes(changed)
            path.chmod(modes[name])
        write_profile(profile, artifacts)
        result = probe_result(probe, artifacts)
        require(result.returncode != 0,
                f"compiled C eligibility probe accepted tampered {label} evidence")
    finally:
        for name, original in originals.items():
            path = artifacts[name]
            path.chmod(modes[name] | stat.S_IWUSR)
            path.write_bytes(original)
            path.chmod(modes[name])
        profile.chmod(original_profile_mode | stat.S_IWUSR)
        profile.write_bytes(original_profile)
        profile.chmod(original_profile_mode)


def expect_missing_profile_pin_rejected(probe, artifacts, key):
    profile = artifacts["profile"]
    original = profile.read_bytes()
    mode = stat.S_IMODE(profile.stat().st_mode)
    prefix = (key + "=").encode("ascii")
    require(original.count(prefix) == 1, f"expected one {key} profile pin")
    changed = b"".join(line for line in original.splitlines(keepends=True)
                        if not line.startswith(prefix))
    require(changed != original, f"removing {key} did not change the profile")
    try:
        profile.chmod(mode | stat.S_IWUSR)
        profile.write_bytes(changed)
        profile.chmod(mode)
        result = probe_result(probe, artifacts)
        require(result.returncode != 0,
                f"compiled C eligibility probe accepted missing {key} profile pin")
    finally:
        profile.chmod(mode | stat.S_IWUSR)
        profile.write_bytes(original)
        profile.chmod(mode)


# The #508 performance-row population (binding.ROW_SCHEMA), built the way
# native_retirement_performance_binding_test.py builds its fixture: every
# census object row in census order, then declared link and
# self-host-stage1 rows, each carrying one census row's identity. The C
# service imports this artifact instead of inventing stage rows. Declared
# here: native link and self-host rows on two eligible native-host
# (x86_64-linux) rows, a foreign-target link row and an untimed (source-ledger
# skipped) self-host row. (A1) The native target is the pinned timed target:
# the binding rejects generated runtime on any other target.
POPULATION_NATIVE_TARGET = binding.NATIVE_TIMED_TARGET
POPULATION_NATIVE_TARGET_ID = 11
POPULATION_STAGE_ROWS = (("link", 0), ("self-host-stage1", 1), ("link", 64),
                         ("self-host-stage1", 16))
POPULATION_STAGE_IDS = {"object": 1, "link": 2, "self-host-stage1": 3}


def population_record(report, report_path, shard, stage_rows=POPULATION_STAGE_ROWS):
    """The unserialized performance-row record for the fixture census."""
    require(binding.TARGETS.index(POPULATION_NATIVE_TARGET) + 1 == POPULATION_NATIVE_TARGET_ID,
            "population native target id is not its performance TARGETS ordinal")
    _fields, census_rows = contract_test.read_table(shard / "rows.tsv")
    # Authenticated skips and (option 3) supplement-resolved rows are untimed.
    skips = set(report["applicability_skip_rows"]) | set(supplement_disposition(report, shard))

    def declared(index, census_index, stage):
        census = census_rows[census_index]
        identity = {field: census[field] for field in binding.ROW_IDENTITY_FIELDS
                    if field != "artifact_stage"}
        identity["artifact_stage"] = stage
        record = {"row": index, "identity": identity}
        compile_eligible = census_index not in skips
        runtime = compile_eligible and binding._native_runtime_required(
            record, POPULATION_NATIVE_TARGET)
        record["eligibility"] = {
            "compiler_wall_time": compile_eligible, "compiler_peak_rss": compile_eligible,
            "generated_code_bytes": compile_eligible, "generated_runtime": runtime,
            "runtime_oracle": ("independent-native-executable-oracle" if runtime
                               else "not-applicable"),
            "code_section": ("deterministic-code-section" if compile_eligible
                             else "not-applicable"),
        }
        return record

    rows = [declared(index, index, "object") for index in range(len(census_rows))]
    for stage, census_index in stage_rows:
        rows.append(declared(len(rows), census_index, stage))
    sources = {
        "support_declaration": sha256((shard / "support-contract.tsv").read_bytes()),
        "manifest": sha256((shard / "manifest.txt").read_bytes()),
        "inputs": sha256((shard / "inputs.tsv").read_bytes()),
        "rows": sha256((shard / "rows.tsv").read_bytes()),
        "dependencies": sha256((shard / "dependencies.tsv").read_bytes()),
        "environment": sha256((shard / "environment.tsv").read_bytes()),
        "validator_report": sha256(report_path.read_bytes()),
    }
    return {"schema": binding.ROW_SCHEMA, "version": binding.ROW_VERSION,
            "row_identity_fields": list(binding.ROW_IDENTITY_FIELDS),
            "sources": sources, "rows": rows}


def population_bytes(record):
    return (json.dumps(record, sort_keys=True, separators=(",", ":"),
                       ensure_ascii=False) + "\n").encode("utf-8")


def expected_population(report, shard, data):
    """Python reference for the rows the C service derives from ``data``.

    The binding's own parser accepts the artifact; each row's census row is
    the unique census identity it carries (object rows in census order), its
    configuration digest is that census row's row_configuration_digest, and
    native-runtime applicability is compiler eligibility and
    binding._native_runtime_required, exactly.
    """
    parsed, axes, _family, _sources = binding._performance_rows_with_sources(data)
    require(axes["stages"] == binding.STAGES, "population lacks a declared stage")
    _fields, census_rows = contract_test.read_table(shard / "rows.tsv")
    by_identity = {}
    for index, census in enumerate(census_rows):
        key = tuple(census[field] for field in binding.ROW_IDENTITY_FIELDS
                    if field != "artifact_stage")
        require(key not in by_identity, "census identities are not unique")
        by_identity[key] = index
    configurations = expected_configurations(report, shard)
    skips = set(report["applicability_skip_rows"])
    resolved = set(supplement_disposition(report, shard))
    lines = []
    for row in parsed:
        key = tuple(row["identity"][field] for field in binding.ROW_IDENTITY_FIELDS
                    if field != "artifact_stage")
        census_index = by_identity[key]
        stage = row["identity"]["artifact_stage"]
        if row["row"] < len(census_rows):
            require(stage == "object" and census_index == row["row"],
                    "population object rows are not the census rows in order")
        require(census_index not in resolved or stage == "object",
                "a stage row carries a supplement-resolved identity")
        compile_eligible = census_index not in skips and census_index not in resolved
        require(row["metrics"]["compiler_wall_time"] is compile_eligible,
                "population compiler eligibility differs from the skip and supplement sets")
        runtime = compile_eligible and binding._native_runtime_required(
            row, POPULATION_NATIVE_TARGET)
        require(row["metrics"]["generated_runtime"] is runtime,
                "population runtime eligibility differs from native applicability")
        lines.append(f"VALIDATOR_POPULATION row={row['row']} census={census_index} "
                     f"stage={POPULATION_STAGE_IDS[stage]} runtime={int(runtime)} "
                     f"configuration={configurations[census_index]}")
    return lines


def expect_both_reject_population(probe, population, path, report, shard, record, mutation, label):
    """A population tamper that the Python reference and the C import both reject."""
    changed = json.loads(json.dumps(record))
    mutation(changed)
    try:
        expected_population(report, shard, population_bytes(changed))
    except (RuntimeError, ValueError):
        pass
    else:
        raise RuntimeError(f"Python population reference accepted {label}")
    expect_tamper_rejected(probe, population, path, lambda _original: population_bytes(changed), label)


def check_population(probe, artifacts, report, shard, stage_rows=POPULATION_STAGE_ROWS):
    """C/Python cross-check of the imported performance population."""
    record = population_record(report, artifacts["report"], shard, stage_rows)
    path = artifacts["report"].parent / "performance-rows.json"
    path.write_bytes(population_bytes(record))
    path.chmod(0o444)
    population = dict(artifacts, performance_rows=path)
    write_profile(population["profile"], population)
    expected = expected_population(report, shard, path.read_bytes())
    result = probe_result(probe, population)
    require(result.returncode == 0,
            "compiled C eligibility probe rejected the genuine performance population:\n" +
            result.stdout + result.stderr)
    derived = [line for line in result.stdout.splitlines()
               if line.startswith("VALIDATOR_POPULATION ")]
    require(derived == expected,
            "C performance population differs from the Python reference")

    def mutated(mutation):
        def replacement(_original):
            changed = json.loads(json.dumps(record))
            mutation(changed)
            return population_bytes(changed)
        return replacement

    def swap_objects(changed):
        rows = changed["rows"]
        rows[0]["identity"], rows[1]["identity"] = rows[1]["identity"], rows[0]["identity"]

    def foreign_identity(changed):
        changed["rows"][-1]["identity"]["argv_evidence"] = "groups/0/other.argv"

    def foreign_runtime(changed):
        eligibility = changed["rows"][-2]["eligibility"]
        eligibility["generated_runtime"] = True
        eligibility["runtime_oracle"] = "independent-native-executable-oracle"

    def untimed_compiler(changed):
        eligibility = changed["rows"][-1]["eligibility"]
        eligibility["compiler_wall_time"] = eligibility["compiler_peak_rss"] = True

    def no_link(changed):
        changed["rows"] = [row for row in changed["rows"]
                           if row["identity"]["artifact_stage"] != "link"]
        for index, row in enumerate(changed["rows"]):
            row["row"] = index

    def extra_object(changed):
        changed["rows"][-1]["identity"]["artifact_stage"] = "object"

    def stale_source(changed):
        changed["sources"]["rows"] = "0" * 64

    def duplicate_stage(changed):
        repeated = json.loads(json.dumps(changed["rows"][-4]))
        repeated["row"] = len(changed["rows"])
        changed["rows"].append(repeated)

    for mutation, label in ((swap_objects, "reordered object rows"),
                            (foreign_identity, "stage identity outside the census"),
                            (foreign_runtime, "foreign-target runtime eligibility"),
                            (untimed_compiler, "skipped-row compiler eligibility"),
                            (no_link, "missing link stage"),
                            (extra_object, "object row after the census rows"),
                            (stale_source, "stale rows.tsv source digest"),
                            (duplicate_stage, "duplicated stage row identity")):
        expect_tamper_rejected(probe, population, path, mutated(mutation), label)
    for resolved in supplement_disposition(report, shard):
        def keep_timed(changed, resolved=resolved):
            eligibility = changed["rows"][resolved]["eligibility"]
            eligibility["compiler_wall_time"] = eligibility["compiler_peak_rss"] = True
            eligibility["generated_code_bytes"] = True
            eligibility["code_section"] = "deterministic-code-section"

        def stage_identity(changed, resolved=resolved):
            # Untimed markers, so only the object-rows-only bound rejects it.
            identity = dict(changed["rows"][resolved]["identity"], artifact_stage="link")
            changed["rows"][-3]["identity"] = identity
            changed["rows"][-3]["eligibility"] = dict(changed["rows"][resolved]["eligibility"])

        expect_both_reject_population(probe, population, path, report, shard, record, keep_timed,
                                      f"supplement-resolved row {resolved} kept compiler-eligible")
        expect_both_reject_population(probe, population, path, report, shard, record, stage_identity,
                                      f"stage row carrying supplement-resolved row {resolved}")
    expect_tamper_rejected(probe, population, path,
                           lambda data: data.replace(b'"row":0}', b'"row": 0}', 1),
                           "non-canonical serialization")
    expect_missing_profile_pin_rejected(probe, population, "performance-rows-sha256")
    result = probe_result(probe, population)
    require(result.returncode == 0 and
            [line for line in result.stdout.splitlines()
             if line.startswith("VALIDATOR_POPULATION ")] == expected,
            "restored performance population no longer matches")
    write_profile(artifacts["profile"], artifacts)
    return len(expected)


# Option 3 stage rows avoid the supplement-resolved census rows 0 and 4, which
# the decision disposes of as object rows only.
SUPPLEMENT_STAGE_ROWS = (("link", 1), ("self-host-stage1", 2), ("link", 64),
                         ("self-host-stage1", 16))


def census_artifacts(fixture, report_path):
    shard = fixture.shards[0]
    return {
        "support": shard / "support-contract.tsv",
        "source_applicability": shard / "applicability-ledger.tsv",
        "inputs": shard / "inputs.tsv",
        "rows": shard / "rows.tsv",
        "manifest": shard / "manifest.txt",
        "report": report_path,
        "applicability": fixture.root / "applicability.tsv",
        "skips": fixture.root / "applicability-skips.tsv",
        "profile": fixture.root / "eligibility.profile",
    }


def expect_both_reject_report(probe, artifacts, report, shard, mutation, label, pinned=True):
    """A report tamper that the binding's rule and the C projection both reject."""
    changed = json.loads(json.dumps(report))
    mutation(changed)
    try:
        binding._supplement_resolved_rows(changed, census_rows_of(shard),
                                          set(changed["applicability_skip_rows"]),
                                          fixture_supplement_pin(shard) if pinned else None)
    except ValueError:
        pass
    else:
        raise RuntimeError(f"binding supplement rule accepted {label}")
    expect_tamper_rejected(probe, artifacts, artifacts["report"],
                           lambda data: mutate_report(data, mutation), label)


def check_approved_supplement_pins():
    """The compiled full-census pin in C equals the binding's approved set."""
    count, digest = binding.APPROVED_SUPPLEMENT_SETS["full-census"]
    source = (REPOSITORY / "tools/bench_service/retirement_correctness_service.c").read_text()
    require(f"#define BQ_RETIREMENT_FULL_SUPPLEMENT_RESOLVED_COUNT {count}u\n" in source and
            f'bq_retirement_full_supplement_resolved_sha256[] =\n    "{digest}";' in source,
            "C and Python approved supplement-resolved pins differ")


def check_supplement_disposition(probe):
    """C/Python agreement on option 3 over genuine supplement evidence.

    The fixture's two direct references fail as the frozen backend's do (a
    nonzero exit and no object) and their Clang controls pass, so the census
    keeps rows 0 and 4 in the telemetry, execution and artifact defect lists.
    Both implementations must derive that set, accept it, make those rows
    compiler-ineligible with the same reason and proof, and reject every
    defect outside it. Returns the performance-population row count.
    """
    fixture = contract_test.ContractTests(methodName="runTest")
    fixture.setUp()
    try:
        fixture.install_applicability({INAPPLICABLE_RECORD, UNAVAILABLE_RECORD})
        require(fixture.make_failed_direct_reference_supplements() == SUPPLEMENT_RESOLVED_ROWS,
                "supplement fixture did not fail the expected direct references")
        report_path = fixture.root / "validator-report.json"
        report = run_validator(fixture.shards, report_path, supplements=True)
        independently_replay_python(fixture.root, report, fixture.shards[0])
        shard = fixture.shards[0]
        census_rows = census_rows_of(shard)
        resolved = supplement_disposition(report, shard)
        require(resolved == SUPPLEMENT_RESOLVED_ROWS,
                "binding did not derive the supplement-resolved set")
        for field in binding.SUPPLEMENT_DEFECT_FIELDS:
            require(report[field] == resolved, f"census {field} is not the supplement set")
        proofs = binding._check_reference_supplements(fixture.root, report, census_rows, resolved)
        require(sorted(proofs) == resolved and
                all(proof["reason"] == binding.SUPPLEMENT_INELIGIBLE_REASON for proof in proofs.values()),
                "binding did not authenticate each resolved row's supplement")

        artifacts = census_artifacts(fixture, report_path)
        make_read_only(*(path for name, path in artifacts.items() if name != "profile"))
        pin_count, pin = fixture_supplement_pin(shard)
        require(pin_count == len(SUPPLEMENT_RESOLVED_ROWS), "fixture pin count changed")
        configurations = expected_configurations(report, shard)

        # Without the explicit test-only pin, or with a pin of another set,
        # both implementations refuse the resolved rows.
        for label, approved, profile_pin in (
                ("no approved set", None, None),
                ("a pin of another set", (1, binding._supplement_identity_digest(census_rows, [0])),
                 binding._supplement_identity_digest(census_rows, [0]))):
            try:
                binding._supplement_resolved_rows(report, census_rows,
                                                  set(report["applicability_skip_rows"]), approved)
            except ValueError:
                pass
            else:
                raise RuntimeError(f"binding supplement rule accepted {label}")
            unpinned = {name: value for name, value in artifacts.items() if name != "supplement_pin"}
            if profile_pin is not None:
                unpinned["supplement_pin"] = profile_pin
            write_profile(unpinned["profile"], unpinned)
            require(probe_result(probe, unpinned).returncode != 0,
                    f"compiled C eligibility probe accepted resolved rows with {label}")

        artifacts["supplement_pin"] = pin
        write_profile(artifacts["profile"], artifacts)
        expect_probe_success(probe, artifacts, configurations, report, shard,
                             EXPECTED_SUPPLEMENT_PROBE_OUTPUT)

        def append(field, row):
            return lambda changed: changed[field].__setitem__(slice(None), sorted(changed[field] + [row]))

        def drop(field, row):
            return lambda changed: changed[field].remove(row)

        cases = (
            (append("telemetry_defect_rows", 8), "defect row outside the supplement set"),
            (drop("artifact_defect_rows", 4), "supplement-set row missing from a defect list"),
            (append("execution_defect_rows", 1), "defect on a MIR row"),
            (append("fallback_defect_rows", 0), "fallback defect on a resolved row"),
            (append("candidate_failure_rows", 1), "candidate failure"),
            (lambda changed: changed["reference_failure_rows"].extend([4, 5, 6, 7]),
             "right set with a failed supplement"),
            (append("unexpected_failure_rows", 1), "unexpected failure"),
            (lambda changed: changed["reference_supplement_sha256"].pop(),
             "supplement digests that do not cover every shard"),
            (lambda changed: changed["reference_supplement_sha256"].clear(),
             "direct-reference defects without a supplement"),
            (drop("direct_reference_failure_rows", 5),
             "direct-reference set that does not cover its group"),
            (append("direct_reference_failure_rows", 9),
             "MIR direct-reference row without its failed base"),
        )
        for mutation, label in cases:
            expect_both_reject_report(probe, artifacts, report, shard, mutation, label)
        # A mismatched supplement digest keeps the report's shape, so the C
        # projection (which holds only the pinned report bytes) cannot see it;
        # the binding authenticates the supplement bytes themselves.
        mismatched = json.loads(json.dumps(report))
        mismatched["reference_supplement_sha256"][0] = "0" * 64
        try:
            binding._check_reference_supplements(fixture.root, mismatched, census_rows, resolved)
        except ValueError:
            pass
        else:
            raise RuntimeError("binding accepted a mismatched supplement digest")
        expect_tamper_rejected(probe, artifacts, artifacts["report"],
            lambda data: mutate_report(data, lambda changed:
                changed["reference_supplement_sha256"].__setitem__(0, "z" * 64)),
            "malformed supplement digest")
        expect_probe_success(probe, artifacts, configurations, report, shard,
                             EXPECTED_SUPPLEMENT_PROBE_OUTPUT)
        rows = check_population(probe, artifacts, report, shard, SUPPLEMENT_STAGE_ROWS)
        require(rows == 192 + len(SUPPLEMENT_STAGE_ROWS),
                "supplement population cross-check did not cover every declared row")
        return rows
    finally:
        fixture.tearDown()


def execute(probe):
    require(probe.is_file() and os.access(probe, os.X_OK),
            f"compiled C probe is missing or not executable: {probe}")
    check_row_configuration_reference()
    fixture = contract_test.ContractTests(methodName="runTest")
    fixture.setUp()
    try:
        fixture.install_applicability({INAPPLICABLE_RECORD, UNAVAILABLE_RECORD})
        report_path = fixture.root / "validator-report.json"
        report = run_validator(fixture.shards, report_path)
        projection_evidence = independently_replay_python(
            fixture.root, report, fixture.shards[0])

        expected_skips = set(range(16, 32)) | set(range(128, 144))
        require(projection_evidence["artifacts"][1]["path"] == "applicability-skips.tsv",
                "independent Python replay did not bind the expected skip artifact")
        require(set(report["applicability_skip_rows"]) == expected_skips,
                "expected source-ledger unavailable and platform rows are not all skipped")
        require(report["applicability_counts"]["platform-inapplicable"] == 16,
                "fixture applicability projection does not contain exactly 16 platform rows")
        require(report["applicability_counts"]["unavailable"] == 16,
                "fixture applicability projection does not contain exactly 16 unavailable rows")

        shard = fixture.shards[0]
        artifacts = {
            "support": shard / "support-contract.tsv",
            "source_applicability": shard / "applicability-ledger.tsv",
            "inputs": shard / "inputs.tsv",
            "rows": shard / "rows.tsv",
            "manifest": shard / "manifest.txt",
            "report": report_path,
            "applicability": fixture.root / "applicability.tsv",
            "skips": fixture.root / "applicability-skips.tsv",
            "profile": fixture.root / "eligibility.profile",
        }
        write_profile(artifacts["profile"], artifacts)
        make_read_only(*(path for name, path in artifacts.items() if name != "profile"))

        configurations = expected_configurations(report, shard)
        require(len(configurations) == 192 and len(set(configurations)) == 192,
                "fixture configuration digests are not distinct per row")
        expect_probe_success(probe, artifacts, configurations, report, shard)
        expect_tamper_rejected(
            probe, artifacts, artifacts["report"],
            lambda data: replace_once(data, b'"clean_candidate": true',
                                       b'"clean_candidate": false', "report"),
            "validator report")
        expect_tamper_rejected(
            probe, artifacts, artifacts["applicability"],
            lambda data: replace_once(data, b"platform-inapplicable",
                                       b"admitted-supported", "applicability"),
            "applicability")
        expect_tamper_rejected(
            probe, artifacts, artifacts["skips"],
            lambda data: replace_once(data, b"source-registration-test",
                                       b"source-registration-tesU", "skip"),
            "skip")
        expect_tamper_rejected(
            probe, artifacts, artifacts["report"],
            lambda data: mutate_report(data, lambda report:
                report["applicability_rows_by_class"]["platform-inapplicable"].__setitem__(0, 127)),
            "validator report class membership")
        expect_tamper_rejected(
            probe, artifacts, artifacts["report"],
            lambda data: mutate_report(data, lambda report:
                report["applicability_skip_rows"].pop(0)),
            "validator report skip set")
        for field in ("candidate_failure_rows", "direct_reference_failure_rows",
                      "reference_failure_rows", "fallback_defect_rows",
                      "telemetry_defect_rows", "execution_defect_rows",
                      "artifact_defect_rows", "unexpected_failure_rows",
                      "reference_supplement_sha256"):
            expect_tamper_rejected(
                probe, artifacts, artifacts["report"],
                lambda data, field=field: mutate_report(data, lambda report:
                    report[field].append(1)),
                f"nonempty {field}")
        # Option 3's zero-resolved rule, identical in both: with no resolved
        # row, no supplement digest may be retained.
        expect_both_reject_report(
            probe, artifacts, report, shard,
            lambda changed: changed["reference_supplement_sha256"].extend(["a" * 64, "b" * 64]),
            "supplement digests without a resolved row", pinned=False)
        expect_tamper_rejected(
            probe, artifacts, artifacts["report"],
            lambda data: mutate_report(data, lambda report:
                report.__setitem__("clean_acceptance", True)),
            "clean-acceptance summary")

        wrong_acceptance = mutate_applicability_cell(
            artifacts["applicability"].read_bytes(), 1, {"acceptance_failure": "1"})
        wrong_acceptance_report = mutate_report(
            artifacts["report"].read_bytes(), lambda report:
                report["acceptance_failure_rows"].append(1))
        wrong_acceptance_report = rebind_applicability_digest(wrong_acceptance_report,
                                                              wrong_acceptance)
        expect_tamper_rejected_many(probe, artifacts,
            {"applicability": wrong_acceptance, "report": wrong_acceptance_report},
            "acceptance failure outside the unavailable class")

        missing_unavailable_failure = mutate_applicability_cell(
            artifacts["applicability"].read_bytes(), 16, {"acceptance_failure": "0"})
        missing_unavailable_report = mutate_report(
            artifacts["report"].read_bytes(), lambda report:
                report["acceptance_failure_rows"].remove(16))
        missing_unavailable_report = rebind_applicability_digest(
            missing_unavailable_report, missing_unavailable_failure)
        expect_tamper_rejected_many(probe, artifacts,
            {"applicability": missing_unavailable_failure, "report": missing_unavailable_report},
            "missing acceptance failure for an unavailable row")

        absent_ledger_classification = mutate_applicability_cell(
            artifacts["applicability"].read_bytes(), 1,
            {"applicability": "unavailable", "admission": "unavailable",
             "reason": "compile-obligation-not-admitted", "ownership": "admission",
             "acceptance_failure": "1"})
        def classify_unledgered_row_unavailable(report):
            for key in ("applicability_rows_by_class", "admission_rows_by_class"):
                report[key]["admitted-supported"].remove(1)
                report[key]["unavailable"].append(1)
                report[key]["unavailable"].sort()
            for key in ("applicability_counts", "admission_counts"):
                report[key]["admitted-supported"] -= 1
                report[key]["unavailable"] += 1
            report["acceptance_failure_rows"].append(1)
            report["acceptance_failure_rows"].sort()
        absent_ledger_report = mutate_report(artifacts["report"].read_bytes(),
                                            classify_unledgered_row_unavailable)
        absent_ledger_report = rebind_applicability_digest(absent_ledger_report,
                                                           absent_ledger_classification)
        expect_tamper_rejected_many(probe, artifacts,
            {"applicability": absent_ledger_classification, "report": absent_ledger_report},
            "unledgered row reclassification")

        changed_manifest = mutate_manifest_gap_summary(artifacts["manifest"].read_bytes(), [1])
        changed_report = mutate_report(artifacts["report"].read_bytes(), lambda report: (
            report.__setitem__("supported_gap_rows", [1]),
            report.__setitem__("supported_gap_count", 1),
            report.__setitem__("supported_gap_sha256",
                               contract_test.contract.canonical_rows_digest([1])),
            report.__setitem__("manifest_identity_sha256",
                contract_test.contract.manifest_identity_digest(
                    dict(line.split("=", 1) for line in changed_manifest.decode("ascii").splitlines())))))
        expect_tamper_rejected_many(probe, artifacts,
            {"manifest": changed_manifest, "report": changed_report}, "supported-gap summary")
        expect_tamper_rejected(
            probe, artifacts, artifacts["report"],
            lambda data: mutate_report(data, lambda report: (
                report.__setitem__("residual_rows", 1),
                report.__setitem__("residual_truncated", True))),
            "nonempty residual summary")
        expect_tamper_rejected(
            probe, artifacts, artifacts["report"],
            lambda data: mutate_report(data, lambda report:
                report.__setitem__("residual_sha256", "0" * 64)),
            "residual digest")
        expect_missing_profile_pin_rejected(
            probe, artifacts, "validator-source-applicability-sha256")
        expect_probe_success(probe, artifacts, configurations, report, shard)
        population_rows = check_population(probe, artifacts, report, shard)
        require(population_rows == 192 + len(POPULATION_STAGE_ROWS),
                "population cross-check did not cover every declared row")
    finally:
        fixture.tearDown()
    check_approved_supplement_pins()
    check_supplement_disposition(probe)


# Installed names of the unit's census directory (retirement_unit.h
# BQ_RETIREMENT_UNIT_CENSUS_FILES), mapped to this fixture's artifact keys.
EMITTED_CENSUS_FILES = (
    ("support.tsv", "support"),
    ("source-applicability.tsv", "source_applicability"),
    ("inputs.tsv", "inputs"),
    ("rows.tsv", "rows"),
    ("manifest.txt", "manifest"),
    ("validator-report.json", "report"),
    ("applicability.tsv", "applicability"),
    ("applicability-skips.tsv", "skips"),
    ("performance-rows.json", "performance_rows"),
)


def replace_compilers(shard, compilers):
    """Give one fixture shard other compiler bytes: the two executables and
    their manifest sizes and digests, before the validator reads the shard."""
    manifest = shard / "manifest.txt"
    text = manifest.read_text(encoding="utf-8")
    for (name, prefix), data in zip((("baseline-ide.exe", "baseline"), ("candidate-ide.exe", "compiler")),
                                    compilers):
        (shard / name).write_bytes(data)
        for key, value in ((f"{prefix}_bytes", str(len(data))),
                           (f"{prefix}_sha256", hashlib.sha256(data).hexdigest())):
            lines = [line for line in text.splitlines() if line.startswith(f"{key}=")]
            require(len(lines) == 1, f"fixture manifest lacks exactly one {key}")
            text = text.replace(lines[0] + "\n", f"{key}={value}\n")
    manifest.write_text(text, encoding="utf-8")


def emit(directory, compilers=None):
    """Write the genuine fixture's eight census files for the C unit fixture.

    retirement_prepare_tests.c installs them as the worker unit's pinned
    census and compares the rows its projection derives with
    ``configurations.txt`` (this module's row_configuration_digest for every
    census row, in row order) and ``population.txt`` (expected_population for
    the emitted ``performance-rows.json``). compilers, when given, is the
    (baseline, candidate) byte pair the census names instead of the contract
    fixture's literals: the C fixture's matched builds freeze its stand-in
    compilers (retirement_stand_in_compiler.h). The directory must not exist
    yet.
    """
    check_row_configuration_reference()
    directory.mkdir(mode=0o700)
    fixture = contract_test.ContractTests(methodName="runTest")
    fixture.setUp()
    try:
        for shard in fixture.shards if compilers else ():
            replace_compilers(shard, compilers)
        fixture.install_applicability({INAPPLICABLE_RECORD, UNAVAILABLE_RECORD})
        report_path = fixture.root / "validator-report.json"
        report = run_validator(fixture.shards, report_path)
        independently_replay_python(fixture.root, report, fixture.shards[0])
        shard = fixture.shards[0]
        artifacts = {
            "support": shard / "support-contract.tsv",
            "source_applicability": shard / "applicability-ledger.tsv",
            "inputs": shard / "inputs.tsv",
            "rows": shard / "rows.tsv",
            "manifest": shard / "manifest.txt",
            "report": report_path,
            "applicability": fixture.root / "applicability.tsv",
            "skips": fixture.root / "applicability-skips.tsv",
        }
        configurations = expected_configurations(report, shard)
        artifacts["performance_rows"] = fixture.root / "performance-rows.json"
        artifacts["performance_rows"].write_bytes(
            population_bytes(population_record(report, report_path, shard)))
        population = expected_population(report, shard, artifacts["performance_rows"].read_bytes())
        for name, key in EMITTED_CENSUS_FILES:
            target = directory / name
            target.write_bytes(artifacts[key].read_bytes())
            target.chmod(0o444)
        target = directory / "configurations.txt"
        target.write_text("".join(f"{digest}\n" for digest in configurations), encoding="ascii")
        target.chmod(0o444)
        target = directory / "population.txt"
        target.write_text("".join(f"{line}\n" for line in population), encoding="ascii")
        target.chmod(0o444)
    finally:
        fixture.tearDown()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", type=Path, nargs="?",
                        help="compiled retirement validator eligibility probe")
    parser.add_argument("--emit", type=Path,
                        help="write the fixture's census files into this new directory instead")
    parser.add_argument("--compilers", type=Path, nargs=2, metavar=("BASELINE", "CANDIDATE"),
                        help="with --emit: the compiler files the census names")
    arguments = parser.parse_args()
    if (arguments.probe is None) == (arguments.emit is None):
        parser.error("give exactly one of the probe or --emit")
    if arguments.compilers is not None and arguments.emit is None:
        parser.error("--compilers needs --emit")
    if arguments.emit is not None:
        emit(arguments.emit, None if arguments.compilers is None else
             tuple(path.read_bytes() for path in arguments.compilers))
        return
    execute(arguments.probe.resolve())
    print("schema-2 validator eligibility fixture passed "
          "(192 rows, 160 eligible, 32 skipped, 192 C/Python configuration digests, "
          f"{192 + len(POPULATION_STAGE_ROWS)} C/Python performance-population rows; "
          "option-3 supplement disposition: 2 resolved rows, 158 eligible, C/Python agreement; "
          "positive and tamper probes)")


if __name__ == "__main__":
    main()
