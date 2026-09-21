#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def read(path):
    return (ROOT / path).read_text(encoding="utf-8")

def write(path, text):
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")

def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)

def replace_between(text, start, end, replacement, label):
    begin = text.find(start)
    if begin < 0:
        raise RuntimeError(f"{label}: start marker not found")
    finish = text.find(end, begin)
    if finish < 0:
        raise RuntimeError(f"{label}: end marker not found")
    return text[:begin] + replacement + text[finish:]

write("tools/native_retirement_performance_schema.py", '"""Shared schema for the production native-retirement census report.\n\nThe producer and every trusted consumer import this exact field set. Unknown\nfields still fail closed; adding a producer field therefore requires a reviewed\nschema update instead of silently weakening a consumer.\n"""\n\nAPPLICABILITY_CLASSES = (\n    "admitted-supported",\n    "retained-control",\n    "retained-reference",\n    "platform-inapplicable",\n    "unavailable",\n)\n\nAPPLICABILITY_FIELDS = (\n    "row", "group", "fixture", "target", "cpu", "frontend", "allocator", "PIC",\n    "applicability", "admission", "disposition", "reason", "ownership",\n    "candidate_failure", "reference_failure", "acceptance_failure",\n)\n\nAPPLICABILITY_SKIP_FIELDS = (\n    "row", "group", "fixture", "target", "allocator", "applicability", "reason",\n)\n\nVALIDATOR_REPORT_FIELDS = (\n    "schema",\n    "directories",\n    "shards",\n    "profile",\n    "rows_validated",\n    "groups",\n    "compiler_revision_claim",\n    "baseline_revision_claim",\n    "compiler_sha256",\n    "baseline_sha256",\n    "support_contract_sha256",\n    "supported_gap_ledger_sha256",\n    "applicability_ledger_sha256",\n    "applicability_ledger_entries",\n    "resource_include_sha256",\n    "manifest_identity_sha256",\n    "rows_identity_fields",\n    "rows_identity_sha256",\n    "input_ledger_fields",\n    "input_ledger_sha256",\n    "baseline_dispositions",\n    "reference_dispositions",\n    "setup_dispositions",\n    "candidate_dispositions",\n    "applicability_classes",\n    "admission_classes",\n    "applicability_counts",\n    "admission_counts",\n    "applicability_rows_by_class",\n    "admission_rows_by_class",\n    "supported_gap_rows",\n    "supported_gap_count",\n    "supported_gap_sha256",\n    "applicability_rows",\n    "applicability_evidence",\n    "applicability_tsv",\n    "applicability_sha256",\n    "applicability_skip_rows",\n    "applicability_skip_evidence",\n    "residual_evidence",\n    "residual_tsv",\n    "residual_sha256",\n    "residual_rows",\n    "residual_limit",\n    "residual_truncated",\n    "candidate_failure_rows",\n    "direct_reference_failure_rows",\n    "reference_supplement_sha256",\n    "reference_failure_rows",\n    "acceptance_failure_rows",\n    "inapplicable_rows",\n    "fallback_defect_rows",\n    "telemetry_defect_rows",\n    "execution_defect_rows",\n    "artifact_defect_rows",\n    "unexpected_failure_rows",\n    "require_clean_candidate",\n    "require_clean_acceptance",\n    "clean_candidate",\n    "clean_acceptance",\n    "complete_row_partition",\n    "global_identity_unique",\n)\n\nPATH_FIELDS = (\n    "directories",\n    "applicability_evidence",\n    "applicability_tsv",\n    "applicability_skip_evidence",\n    "residual_evidence",\n    "residual_tsv",\n)\n')

# Keep the producer and consumer on one exact schema.
path = "tools/native_retirement_contract.py"
text = read(path)
text = replace_once(
    text,
    "from pathlib import Path\n",
    "import importlib.util\nfrom pathlib import Path\n\ntry:\n    import native_retirement_performance_schema as PERFORMANCE_SCHEMA\nexcept ImportError:\n    _performance_schema_spec = importlib.util.spec_from_file_location(\n        \"native_retirement_performance_schema\",\n        Path(__file__).resolve().with_name(\"native_retirement_performance_schema.py\"))\n    if _performance_schema_spec is None or _performance_schema_spec.loader is None:\n        raise RuntimeError(\"native retirement performance schema is unavailable\")\n    PERFORMANCE_SCHEMA = importlib.util.module_from_spec(_performance_schema_spec)\n    _performance_schema_spec.loader.exec_module(PERFORMANCE_SCHEMA)\n",
    "contract schema import",
)
text = replace_once(
    text,
    '    output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\\n", encoding="utf-8")\n',
    '    assert tuple(result) == PERFORMANCE_SCHEMA.VALIDATOR_REPORT_FIELDS, \\\n'    '        "validate_shards report schema differs from the reviewed performance consumer"\n'    '    output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\\n", encoding="utf-8")\n',
    "contract schema assertion",
)
write(path, text)

path = "tools/native_retirement_performance_binding.py"
text = read(path)
text = replace_once(
    text,
    "from contextlib import closing\n",
    "from contextlib import closing\n\ntry:\n    import native_retirement_performance_schema as RETIREMENT_SCHEMA\nexcept ImportError:\n    _performance_schema_spec = importlib.util.spec_from_file_location(\n        \"native_retirement_performance_schema\",\n        Path(__file__).resolve().with_name(\"native_retirement_performance_schema.py\"))\n    if _performance_schema_spec is None or _performance_schema_spec.loader is None:\n        raise RuntimeError(\"native retirement performance schema is unavailable\")\n    RETIREMENT_SCHEMA = importlib.util.module_from_spec(_performance_schema_spec)\n    _performance_schema_spec.loader.exec_module(RETIREMENT_SCHEMA)\n",
    "binding schema import",
)
text = replace_once(
    text,
    'ROW_SCHEMA = "buster-native-retirement-performance-rows-v1"\nROW_VERSION = 1',
    'ROW_SCHEMA = "buster-native-retirement-performance-rows-v2"\nROW_VERSION = 2',
    "row schema v2",
)
text = replace_once(
    text,
    'PERFORMANCE_DECLARATION_SCHEMA = "buster-native-retirement-performance-population-v1"\n'    'PERFORMANCE_DECLARATION_VERSION = 1',
    'PERFORMANCE_DECLARATION_SCHEMA = "buster-native-retirement-performance-population-v2"\n'    'PERFORMANCE_DECLARATION_VERSION = 2',
    "performance declaration v2",
)
text = replace_once(
    text,
    'RESULT_INPUT_PLAN_SCHEMA = "buster-native-retirement-result-input-plan-v1"',
    'RESULT_INPUT_PLAN_SCHEMA = "buster-native-retirement-result-input-plan-v2"',
    "result plan v2",
)
text = replace_once(
    text,
    'EXECUTION_PLAN_SCHEMA = "buster-native-retirement-execution-plan-v1"',
    'EXECUTION_PLAN_SCHEMA = "buster-native-retirement-execution-plan-v2"',
    "execution plan v2",
)
text = replace_once(
    text,
    'EXECUTION_SCHEDULE = "tp-retirement-block-schedule-v1"',
    'EXECUTION_SCHEDULE = "tp-retirement-block-schedule-v2"',
    "execution schedule v2",
)

old_metrics = '''        if not metrics["compiler_wall_time"] or not metrics["compiler_peak_rss"]:
            _fail(f"{name}.rows[{index}] must measure compiler wall time and peak RSS")
        if metrics["generated_code_bytes"]:
            if eligibility["code_section"] != "deterministic-code-section":
                _fail(f"{name}.rows[{index}] has an invalid code-section obligation")
        elif eligibility["code_section"] != "not-applicable":
            _fail(f"{name}.rows[{index}] has an inapplicable code-section marker")
        if metrics["generated_runtime"]:
            if eligibility["runtime_oracle"] != "independent-native-executable-oracle":
                _fail(f"{name}.rows[{index}] has an invalid runtime oracle")
        elif eligibility["runtime_oracle"] != "not-applicable":
            _fail(f"{name}.rows[{index}] has an inapplicable runtime oracle")
'''
new_metrics = '''        compile_eligible = metrics["compiler_wall_time"]
        if metrics["compiler_peak_rss"] is not compile_eligible:
            _fail(f"{name}.rows[{index}] must keep wall-time and peak-RSS eligibility paired")
        if not compile_eligible:
            if metrics["generated_code_bytes"] or metrics["generated_runtime"] \\
                    or eligibility["code_section"] != "not-applicable" \\
                    or eligibility["runtime_oracle"] != "not-applicable":
                _fail(f"{name}.rows[{index}] gives metrics to an authenticated untimed row")
        elif metrics["generated_code_bytes"]:
            if eligibility["code_section"] != "deterministic-code-section":
                _fail(f"{name}.rows[{index}] has an invalid code-section obligation")
        elif eligibility["code_section"] not in {
                "deterministic-zero-baseline-code-section", "not-applicable"}:
            _fail(f"{name}.rows[{index}] has an invalid zero/inapplicable code marker")
        if metrics["generated_runtime"]:
            if not compile_eligible \\
                    or eligibility["runtime_oracle"] != "independent-native-executable-oracle":
                _fail(f"{name}.rows[{index}] has an invalid runtime oracle")
        elif eligibility["runtime_oracle"] != "not-applicable":
            _fail(f"{name}.rows[{index}] has an inapplicable runtime oracle")
'''
text = replace_once(text, old_metrics, new_metrics, "row eligibility parser")

helpers = r'''
def _bounded_code_bytes(value, name, *, positive=False):
    if type(value) is not int or value < (1 if positive else 0) or value > (1 << 63) - 1:
        qualifier = "positive " if positive else "nonnegative "
        _fail(f"{name} must be a {qualifier}bounded integer")
    return value


def _row_ids(value, name, row_count):
    rows = _list(value, name)
    for index, row in enumerate(rows):
        if type(row) is not int or not 0 <= row < row_count:
            _fail(f"{name}[{index}] is outside the complete census population")
    if rows != sorted(set(rows)):
        _fail(f"{name} must be sorted and unique")
    return rows


def _tsv_rows(data, fields, name, *, allow_empty=False):
    """Parse a strict TSV, optionally permitting a header-only table."""
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        _fail(f"{name} is not valid UTF-8 TSV: {error}")
    expected = list(fields)
    reader = csv.DictReader(io.StringIO(text), delimiter="\t", lineterminator="\n")
    if reader.fieldnames != expected:
        _fail(f"{name} header does not match the reviewed schema")
    rows = []
    for index, row in enumerate(reader):
        if None in row or any(value is None for value in row.values()):
            _fail(f"{name}[{index}] is malformed")
        if any("\r" in value or "\n" in value for value in row.values()):
            _fail(f"{name}[{index}] contains a multi-line field")
        rows.append(row)
    if not rows and not allow_empty:
        _fail(f"{name} is empty")
    return rows


def _validator_projection(report, row_count):
    # Validate the producer's complete applicability/admission partition.
    if report["profile"] != "full-census":
        _fail("#508 validator report is not the production full-census profile")
    _exact_list(report["applicability_classes"], list(RETIREMENT_SCHEMA.APPLICABILITY_CLASSES),
                "validator_report.applicability_classes")
    _exact_list(report["admission_classes"], list(RETIREMENT_SCHEMA.APPLICABILITY_CLASSES),
                "validator_report.admission_classes")
    counts = _keys(report["applicability_counts"], RETIREMENT_SCHEMA.APPLICABILITY_CLASSES,
                   "validator_report.applicability_counts")
    admission_counts = _keys(report["admission_counts"], RETIREMENT_SCHEMA.APPLICABILITY_CLASSES,
                             "validator_report.admission_counts")
    rows_by_class = _keys(report["applicability_rows_by_class"],
                          RETIREMENT_SCHEMA.APPLICABILITY_CLASSES,
                          "validator_report.applicability_rows_by_class")
    admission_rows = _keys(report["admission_rows_by_class"],
                           RETIREMENT_SCHEMA.APPLICABILITY_CLASSES,
                           "validator_report.admission_rows_by_class")
    by_row = {}
    for classification in RETIREMENT_SCHEMA.APPLICABILITY_CLASSES:
        rows = _row_ids(rows_by_class[classification],
                        f"validator_report.applicability_rows_by_class.{classification}",
                        row_count)
        _nonnegative_int(counts[classification],
                         f"validator_report.applicability_counts.{classification}")
        _nonnegative_int(admission_counts[classification],
                         f"validator_report.admission_counts.{classification}")
        if counts[classification] != len(rows):
            _fail("validator report applicability count differs from its authenticated row set")
        if admission_counts[classification] != counts[classification] \
                or admission_rows[classification] != rows:
            _fail("validator report admission projection differs from applicability")
        for row in rows:
            if row in by_row:
                _fail("validator report applicability classes overlap")
            by_row[row] = classification
    if set(by_row) != set(range(row_count)) or report["applicability_rows"] != row_count:
        _fail("validator report applicability projection is not a complete row partition")
    skip_rows = _row_ids(report["applicability_skip_rows"],
                         "validator_report.applicability_skip_rows", row_count)
    expected_skips = {
        row for row, classification in by_row.items()
        if classification in {"retained-control", "platform-inapplicable", "unavailable"}
    }
    if set(skip_rows) != expected_skips:
        _fail("validator report skip set differs from authenticated non-execution classes")
    if report["global_identity_unique"] is not True:
        _fail("validator report does not prove global row identity uniqueness")
    return by_row, set(skip_rows)


def _report_evidence_path(root, value, name):
    _string(value, name)
    root = Path(root).resolve()
    candidate = Path(value)
    if candidate.is_absolute():
        candidate = candidate.resolve()
    else:
        relative = _relative_path(value, name)
        candidate = root.joinpath(*PurePosixPath(relative).parts).resolve()
    try:
        candidate.relative_to(root)
    except ValueError:
        _fail(f"{name} escapes the evidence root")
    cursor = root
    for part in candidate.relative_to(root).parts:
        cursor /= part
        if cursor.is_symlink():
            _fail(f"{name} contains a symbolic link")
    if not candidate.is_file() or candidate.is_symlink():
        _fail(f"{name} is missing or is a symbolic link")
    return candidate


def _check_validator_projection_evidence(root, report, census_rows, by_row, skip_rows):
    if report["applicability_evidence"] != report["applicability_tsv"]:
        _fail("validator report applicability aliases identify different files")
    if report["residual_evidence"] != report["residual_tsv"]:
        _fail("validator report residual aliases identify different files")
    applicability_path = _report_evidence_path(
        root, report["applicability_evidence"], "validator_report.applicability_evidence")
    skip_path = _report_evidence_path(
        root, report["applicability_skip_evidence"],
        "validator_report.applicability_skip_evidence")
    residual_path = _report_evidence_path(
        root, report["residual_evidence"], "validator_report.residual_evidence")
    if hashlib.sha256(applicability_path.read_bytes()).hexdigest() != report["applicability_sha256"]:
        _fail("validator report applicability digest differs from its evidence")
    if hashlib.sha256(residual_path.read_bytes()).hexdigest() != report["residual_sha256"]:
        _fail("validator report residual digest differs from its evidence")

    applicability = _tsv_rows(
        applicability_path.read_bytes(), RETIREMENT_SCHEMA.APPLICABILITY_FIELDS,
        "applicability.tsv")
    if len(applicability) != len(census_rows):
        _fail("applicability.tsv is not the complete census population")
    reasons = {}
    candidate_failures = []
    reference_failures = []
    acceptance_failures = []
    for index, item in enumerate(applicability):
        if int(item["row"]) != index:
            _fail("applicability.tsv rows are not contiguous")
        source = census_rows[index]
        expected = {
            "group": source["group"], "fixture": source["fixture"], "target": source["target"],
            "cpu": source["cpu"], "frontend": source["frontend_lowering"],
            "allocator": source["allocator"], "PIC": source["PIC"],
        }
        if any(item[key] != value for key, value in expected.items()):
            _fail("applicability.tsv identity differs from rows.tsv")
        if item["applicability"] != by_row[index] or item["admission"] != by_row[index]:
            _fail("applicability.tsv classification differs from the report partition")
        for field, target in (("candidate_failure", candidate_failures),
                              ("reference_failure", reference_failures),
                              ("acceptance_failure", acceptance_failures)):
            if item[field] not in {"0", "1"}:
                _fail(f"applicability.tsv {field} is not boolean")
            if item[field] == "1":
                target.append(index)
        reasons[index] = item["reason"]
    if candidate_failures != report["candidate_failure_rows"] \
            or reference_failures != report["reference_failure_rows"] \
            or acceptance_failures != report["acceptance_failure_rows"]:
        _fail("applicability.tsv failure projection differs from the validator report")

    skips = _tsv_rows(
        skip_path.read_bytes(), RETIREMENT_SCHEMA.APPLICABILITY_SKIP_FIELDS,
        "applicability-skips.tsv", allow_empty=True)
    if [int(item["row"]) for item in skips] != sorted(skip_rows):
        _fail("applicability-skips.tsv differs from the authenticated skip row set")
    for item in skips:
        row = int(item["row"])
        source = census_rows[row]
        expected = {"group": source["group"], "fixture": source["fixture"],
                    "target": source["target"], "allocator": source["allocator"],
                    "applicability": by_row[row], "reason": reasons[row]}
        if any(item[key] != value for key, value in expected.items()):
            _fail("applicability-skips.tsv is not row-bound to trusted source evidence")
    return {
        "applicability_bytes": applicability_path.read_bytes(),
        "skip_bytes": skip_path.read_bytes(),
        "residual_bytes": residual_path.read_bytes(),
        "reasons": reasons,
    }


'''
text = replace_once(text, "TARGET_ABIS = {\n", helpers + "TARGET_ABIS = {\n",
                    "validator projection helpers")

old_report_keys_start = '    validator_report = _keys(validator_report, (\n'
old_report_keys_end = '    if validator_report["schema"] != 2 or validator_report["complete_row_partition"] is not True:\n'
new_report_keys = '''    validator_report = _keys(
        validator_report, RETIREMENT_SCHEMA.VALIDATOR_REPORT_FIELDS,
        "validator_report")
'''
text = replace_between(text, old_report_keys_start, old_report_keys_end,
                       new_report_keys, "validator report exact schema")

old_schema_guard = '''    if validator_report["schema"] != 2 or validator_report["complete_row_partition"] is not True:
        _fail("#508 validator report is not a complete schema-2 partition")
'''
new_schema_guard = '''    if validator_report["schema"] != 2 \\
            or validator_report["complete_row_partition"] is not True:
        _fail("#508 validator report is not a complete schema-2 partition")
    applicability_by_row, applicability_skip_rows = _validator_projection(
        validator_report, declaration_object_rows)
    projection_evidence = _check_validator_projection_evidence(
        root, validator_report, census_rows, applicability_by_row,
        applicability_skip_rows)
'''
text = replace_once(text, old_schema_guard, new_schema_guard, "schema guard projection")

text = replace_once(
    text,
    '    for field in ("require_clean_candidate", "require_clean_acceptance",\n                  "clean_candidate", "clean_acceptance"):\n        if validator_report[field] is not True:\n            _fail(f"schema-2 validator report.{field} is not true")\n    for field in ("candidate_failure_rows", "reference_failure_rows",\n                  "acceptance_failure_rows", "fallback_defect_rows",\n                  "telemetry_defect_rows", "execution_defect_rows"):\n        if validator_report[field]:\n            _fail(f"schema-2 validator report contains {field}")\n',
    '    for field in ("require_clean_candidate", "require_clean_acceptance",\n                  "clean_candidate", "clean_acceptance"):\n        _boolean(validator_report[field], f"schema-2 validator report.{field}")\n    if not validator_report["require_clean_candidate"] \\\n            or not validator_report["clean_candidate"]:\n        _fail("schema-2 validator report does not enforce a clean candidate")\n    for field in ("candidate_failure_rows", "reference_failure_rows",\n                  "fallback_defect_rows", "telemetry_defect_rows",\n                  "execution_defect_rows", "artifact_defect_rows",\n                  "unexpected_failure_rows"):\n        if validator_report[field]:\n            _fail(f"schema-2 validator report contains {field}")\n    unavailable_rows = validator_report["applicability_rows_by_class"]["unavailable"]\n    if validator_report["acceptance_failure_rows"] != unavailable_rows:\n        _fail("schema-2 acceptance failures are not exactly authenticated unavailable rows")\n    if validator_report["clean_acceptance"] != (\n            not validator_report["acceptance_failure_rows"]):\n        _fail("schema-2 clean_acceptance disagrees with retained failure evidence")\n',
    "candidate-clean and authenticated-unavailable gates",
)

old_replay_invocation = '''        command = [sys.executable, str(validator_program), "validate-shards",
                   *(str(path) for path in directory_paths), "--out", str(output),
                   "--require-clean-candidate", "--require-clean-acceptance"]
        replay = subprocess.run(command, check=False, capture_output=True, text=True,
                                cwd=Path(root).resolve())
        if replay.returncode != 0 or not output.is_file():
            _fail("#508 schema-2 validator replay failed")
'''
new_replay_invocation = '''        command = [sys.executable, str(validator_program), "validate-shards",
                   *(str(path) for path in directory_paths), "--out", str(output)]
        if validator_report["require_clean_candidate"]:
            command.append("--require-clean-candidate")
        if validator_report["require_clean_acceptance"]:
            command.append("--require-clean-acceptance")
        replay = subprocess.run(command, check=False, capture_output=True, text=True,
                                cwd=Path(root).resolve())
        expected_success = not (
            validator_report["require_clean_candidate"]
            and validator_report["candidate_failure_rows"]) and not (
            validator_report["require_clean_acceptance"]
            and validator_report["acceptance_failure_rows"])
        if (replay.returncode == 0) is not expected_success or not output.is_file():
            _fail("#508 schema-2 validator replay status differs from retained failures")
'''
text = replace_once(text, old_replay_invocation, new_replay_invocation,
                    "validator replay preserves gate flags")

old_replay_compare = '''        for field, expected in validator_report.items():
            if field == "directories":
                continue
            if recomputed.get(field) != expected:
                _fail(f"#508 schema-2 validator replay differs in {field}")
'''
new_replay_compare = '''        path_fields = set(RETIREMENT_SCHEMA.PATH_FIELDS)
        for field, expected in validator_report.items():
            if field in path_fields:
                continue
            if recomputed.get(field) != expected:
                _fail(f"#508 schema-2 validator replay differs in {field}")
        replay_files = {
            "applicability_evidence": projection_evidence["applicability_bytes"],
            "applicability_skip_evidence": projection_evidence["skip_bytes"],
            "residual_evidence": projection_evidence["residual_bytes"],
        }
        for field, expected_bytes in replay_files.items():
            replay_path = Path(recomputed[field])
            if not replay_path.is_file() or replay_path.read_bytes() != expected_bytes:
                _fail(f"#508 schema-2 validator replay differs in {field}")
'''
text = replace_once(text, old_replay_compare, new_replay_compare, "replay path comparison")

return_marker = '''    # Keep the byte-level identities available to the workflow validator.  A
    # result-input plan must bind the actual schema-2 artifacts, not a digest
    # recomputed from the parsed manifest or a caller-provided row total.
'''
eligibility_join = '''    compiler_eligible_rows = set()
    performance_applicability = {}
    eligible_object_rows = 0
    for row in parsed:
        identity = tuple(row["identity"][field] for field in ROW_IDENTITY_FIELDS
                         if field != "artifact_stage")
        source = expected_object.get(identity)
        if source is None:
            _fail("canonical performance row has no schema-2 census identity")
        census_row = int(source["row"])
        compiler_eligible = census_row not in applicability_skip_rows
        if row["metrics"]["compiler_wall_time"] is not compiler_eligible \\
                or row["metrics"]["compiler_peak_rss"] is not compiler_eligible:
            _fail("canonical compiler eligibility differs from authenticated skip provenance")
        if not compiler_eligible:
            if any(row["metrics"][metric] for metric in METRICS) \\
                    or row["eligibility"]["runtime_oracle"] != "not-applicable" \\
                    or row["eligibility"]["code_section"] != "not-applicable":
                _fail("authenticated non-executed row contains measurement eligibility")
        else:
            compiler_eligible_rows.add(row["row"])
            if row["identity"]["artifact_stage"] == "object":
                eligible_object_rows += 1
        performance_applicability[row["row"]] = {
            "census_row": census_row,
            "classification": applicability_by_row[census_row],
            "reason": projection_evidence["reasons"][census_row],
            "compiler_eligible": compiler_eligible,
        }

'''
text = replace_once(text, return_marker, eligibility_join + return_marker,
                    "trusted eligibility join")
text = replace_once(
    text,
    '''            "object_row_count": declaration_object_rows,
            "group_count": declaration_groups}
''',
    '''            "object_row_count": declaration_object_rows,
            "eligible_object_row_count": eligible_object_rows,
            "compiler_eligible_rows": compiler_eligible_rows,
            "performance_applicability": performance_applicability,
            "applicability_sha256": validator_report["applicability_sha256"],
            "applicability_skip_rows": sorted(applicability_skip_rows),
            "group_count": declaration_groups}
''',
    "support output eligibility facts",
)

text = replace_once(
    text,
    '''        selected = sorted(row["row"] for row in rows
                          if kind == "compiler" or row["metrics"]["generated_runtime"])
''',
    '''        selected = sorted(
            row["row"] for row in rows
            if (kind == "compiler" and row["metrics"]["compiler_wall_time"])
            or (kind == "runtime" and row["metrics"]["generated_runtime"]))
''',
    "eligible execution schedule",
)
text = replace_once(
    text,
    '    campaigns = len(parsed) + sum(row["metrics"]["generated_runtime"] for row in parsed)\n',
    '    campaigns = (sum(row["metrics"]["compiler_wall_time"] for row in parsed)\n'    '                 + sum(row["metrics"]["generated_runtime"] for row in parsed))\n',
    "eligible transcript count",
)
text = replace_once(
    text,
    '''    if value["sample_population"] != "canonical-performance-rows-with-required-metrics":
        _fail("result-input plan must sample every eligible canonical row")
    if value["eligible_population"] != "canonical-performance-rows":
        _fail("result-input plan must bind every eligible performance row")
''',
    '''    if value["sample_population"] != \\
            "trusted-census-eligible-performance-rows-with-required-metrics":
        _fail("result-input plan must sample every authenticated eligible row")
    if value["eligible_population"] != \\
            "authenticated-applicability-minus-nonexecuted-rows":
        _fail("result-input plan must bind the trusted applicability projection")
''',
    "result plan population names",
)

text = replace_once(
    text,
    '''            if metric == "generated_code_bytes":
                if type(sample) is not int or sample <= 0 or sample > (1 << 63) - 1:
                    _fail("generated code-byte observations must be bounded integers")
                continue
''',
    '''            if metric == "generated_code_bytes":
                minimum = 1 if side == "baseline" else 0
                if type(sample) is not int or sample < minimum or sample > (1 << 63) - 1:
                    _fail("generated code-byte observations require a positive baseline and "
                          "a nonnegative candidate")
                continue
''',
    "zero code result observations",
)

text = replace_once(
    text,
    '''        code_eligible = oracle_item["code_section_status"] == "parsed-deterministic"
        if code_eligible:
            _positive_int(oracle_item["code_section_bytes"], "execution oracle code-section size")
            _sha(oracle_item["code_section_sha256"], "execution oracle code-section digest")
''',
    '''        code_observed = oracle_item["code_section_status"] == "parsed-deterministic"
        if code_observed:
            _bounded_code_bytes(oracle_item["code_section_bytes"],
                                "execution oracle code-section size")
            _sha(oracle_item["code_section_sha256"], "execution oracle code-section digest")
            if oracle_item["code_section_bytes"] == 0 \\
                    and oracle_item["code_section_sha256"] != hashlib.sha256(b"").hexdigest():
                _fail("zero-byte execution oracle does not bind the empty payload")
        elif oracle_item["code_section_status"] != "not-applicable" \\
                or oracle_item["code_section_bytes"] is not None \\
                or oracle_item["code_section_sha256"] is not None:
            _fail("inapplicable execution oracle must use explicit null code observations")
        code_eligible = code_observed and oracle_item["code_section_bytes"] > 0
''',
    "zero code oracle",
)
text = replace_once(
    text,
    '''            for key in ("compiler_command_sha256", "artifact_sha256"):
                _sha(side[key], f"execution_plan.row.{variant}.{key}")
            if row["metrics"]["generated_code_bytes"]:
                _sha(side["code_section_sha256"], "execution plan code-section identity")
                _positive_int(side["code_section_bytes"], "execution plan code-section size")
                if side["code_section_bytes"] > (1 << 63) - 1:
                    _fail("execution plan code-section size exceeds the result domain")
            elif side["code_section_sha256"] is not None or side["code_section_bytes"] is not None:
                _fail("ineligible code-section evidence must be explicitly absent")
''',
    '''            compile_eligible = row["metrics"]["compiler_wall_time"]
            for key in ("compiler_command_sha256", "artifact_sha256"):
                if compile_eligible:
                    _sha(side[key], f"execution_plan.row.{variant}.{key}")
                elif side[key] is not None:
                    _fail("untimed row compiler evidence must be explicitly null")
            if code_observed:
                _sha(side["code_section_sha256"], "execution plan code-section identity")
                _bounded_code_bytes(
                    side["code_section_bytes"], "execution plan code-section size",
                    positive=(variant == "baseline" and code_eligible))
                if side["code_section_bytes"] == 0 \\
                        and side["code_section_sha256"] != hashlib.sha256(b"").hexdigest():
                    _fail("zero-byte execution plan payload does not bind empty bytes")
                if variant == "baseline" and (
                        side["code_section_bytes"] != oracle_item["code_section_bytes"]
                        or side["code_section_sha256"] != oracle_item["code_section_sha256"]):
                    _fail("execution plan baseline code fact differs from the independent oracle")
            elif side["code_section_sha256"] is not None or side["code_section_bytes"] is not None:
                _fail("ineligible code-section evidence must be explicitly absent")
''',
    "execution plan sparse and zero code",
)

text = replace_once(
    text,
    '        runtime_eligible = runtime_required\n        if row["metrics"]["generated_code_bytes"] is not code_eligible \\\n                or row["metrics"]["generated_runtime"] is not runtime_eligible:\n            _fail("execution eligibility is not derived from the independent oracle")\n',
    '        runtime_eligible = runtime_required\n        expected_code_marker = (\n            "deterministic-code-section" if code_eligible\n            else "deterministic-zero-baseline-code-section" if code_observed\n            else "not-applicable")\n        if row["metrics"]["generated_code_bytes"] is not code_eligible \\\n                or row["metrics"]["generated_runtime"] is not runtime_eligible \\\n                or row["eligibility"]["code_section"] != expected_code_marker:\n            _fail("execution eligibility is not derived from the independent oracle")\n',
    "execution plan zero-baseline eligibility",
)

text = replace_once(
    text,
    '''        if item["code_section_status"] != "parsed-deterministic":
            _fail("oracle record code section was not independently parsed")
        _positive_int(item["code_section_bytes"],
                      f"workflow.records.oracle.records[{index}].code_section_bytes")
        _sha(item["code_section_sha256"],
             f"workflow.records.oracle.records[{index}].code_section_sha256")
''',
    '''        expected_compile = item["row"] in support_output["compiler_eligible_rows"]
        if expected_compile:
            if item["code_section_status"] != "parsed-deterministic":
                _fail("eligible oracle record code section was not independently parsed")
            _bounded_code_bytes(
                item["code_section_bytes"],
                f"workflow.records.oracle.records[{index}].code_section_bytes")
            _sha(item["code_section_sha256"],
                 f"workflow.records.oracle.records[{index}].code_section_sha256")
            if item["code_section_bytes"] == 0 \\
                    and item["code_section_sha256"] != hashlib.sha256(b"").hexdigest():
                _fail("zero-byte oracle record does not bind the empty payload")
        else:
            if item["code_section_status"] != "not-applicable" \\
                    or item["code_section_bytes"] is not None \\
                    or item["code_section_sha256"] is not None:
                _fail("untimed oracle record must retain explicit null code observations")
''',
    "workflow zero/sparse oracle",
)

old_admission_validation = '''        if item["requested_obligation"] != "compiler-wall-time-and-peak-rss":
            _fail("admission records must name the timing/resource obligation")
        if item["status"] != "completed" or item["exit_code"] != 0:
            _fail("admission records must be completed successful compiler executions")
        _boolean(item["timed_out"],
                 f"workflow.records.admission.records[{index}].timed_out")
        _boolean(item["native_compiler"],
                 f"workflow.records.admission.records[{index}].native_compiler")
        if item["timed_out"] or not item["native_compiler"]:
            _fail("admission records must prove native non-timeout compiler execution")
        if item["artifact_kind"] not in {"object", "linked-executable", "self-host-stage1"}:
            _fail("admission record artifact kind is not approved")
        _positive_int(item["artifact_bytes"],
                      f"workflow.records.admission.records[{index}].artifact_bytes")
        _sha(item["artifact_sha256"],
             f"workflow.records.admission.records[{index}].artifact_sha256")
        admission_by_row[item["row"]] = item
        object_admissions += item["artifact_stage"] == "object"
'''
new_admission_validation = '''        expected_compile = item["row"] in support_output["compiler_eligible_rows"]
        _boolean(item["timed_out"],
                 f"workflow.records.admission.records[{index}].timed_out")
        _boolean(item["native_compiler"],
                 f"workflow.records.admission.records[{index}].native_compiler")
        if expected_compile:
            if item["requested_obligation"] != "compiler-wall-time-and-peak-rss" \\
                    or item["status"] != "completed" or item["exit_code"] != 0 \\
                    or item["timed_out"] or not item["native_compiler"]:
                _fail("eligible admission record is not a successful native compiler execution")
            if item["artifact_kind"] not in {
                    "object", "linked-executable", "self-host-stage1"}:
                _fail("eligible admission record artifact kind is not approved")
            _positive_int(item["artifact_bytes"],
                          f"workflow.records.admission.records[{index}].artifact_bytes")
            _sha(item["artifact_sha256"],
                 f"workflow.records.admission.records[{index}].artifact_sha256")
            object_admissions += item["artifact_stage"] == "object"
        else:
            if item["requested_obligation"] is not None \\
                    or item["status"] != "not-applicable" \\
                    or item["exit_code"] is not None \\
                    or item["timed_out"] or item["native_compiler"] \\
                    or item["artifact_kind"] is not None \\
                    or item["artifact_bytes"] is not None \\
                    or item["artifact_sha256"] is not None:
                _fail("authenticated non-executed admission must use explicit null observations")
        admission_by_row[item["row"]] = item
'''
text = replace_once(text, old_admission_validation, new_admission_validation,
                    "sparse admission records")
text = replace_once(
    text,
    '''    if object_admissions != support_output["object_row_count"]:
        _fail("admission records do not cover every schema-2 object row")
''',
    '''    if object_admissions != support_output["eligible_object_row_count"]:
        _fail("admission records do not cover every authenticated eligible object row")
''',
    "eligible object admission count",
)

old_expected = '''        expected = {
            "compiler_wall_time": admission_item["status"] == "completed"
                and admission_item["exit_code"] == 0
                and not admission_item["timed_out"]
                and admission_item["native_compiler"]
                and admission_item["requested_obligation"]
                == "compiler-wall-time-and-peak-rss",
            "compiler_peak_rss": admission_item["status"] == "completed"
                and admission_item["exit_code"] == 0
                and not admission_item["timed_out"]
                and admission_item["native_compiler"]
                and admission_item["requested_obligation"]
                == "compiler-wall-time-and-peak-rss",
            "generated_code_bytes": oracle_item["code_section_status"]
                == "parsed-deterministic" and oracle_item["code_section_bytes"] > 0,
            "generated_runtime": oracle_item["native_runtime"],
            "runtime_oracle": ("independent-native-executable-oracle"
                                if oracle_item["runtime_oracle_status"] == "passed-native"
                                else "not-applicable"),
            "code_section": ("deterministic-code-section"
                             if oracle_item["code_section_status"] == "parsed-deterministic"
                             else "not-applicable"),
        }
'''
new_expected = '''        compile_eligible = row["row"] in support_output["compiler_eligible_rows"]
        code_parsed = oracle_item["code_section_status"] == "parsed-deterministic"
        code_ratio_eligible = (
            code_parsed and compile_eligible and oracle_item["code_section_bytes"] > 0)
        expected = {
            "compiler_wall_time": compile_eligible,
            "compiler_peak_rss": compile_eligible,
            "generated_code_bytes": code_ratio_eligible,
            "generated_runtime": oracle_item["native_runtime"] and compile_eligible,
            "runtime_oracle": ("independent-native-executable-oracle"
                                if oracle_item["runtime_oracle_status"] == "passed-native"
                                else "not-applicable"),
            "code_section": (
                "deterministic-code-section" if code_ratio_eligible
                else "deterministic-zero-baseline-code-section"
                if code_parsed and compile_eligible else "not-applicable"),
        }
'''
text = replace_once(text, old_expected, new_expected, "trusted row eligibility derivation")

# Native runtime cannot resurrect an authenticated non-executed row.
native_start = text.find("def _native_runtime_required(row, native_target):\n")
if native_start < 0:
    raise RuntimeError("native runtime helper not found")
native_end = text.find("\n\ndef ", native_start + 1)
if native_end < 0:
    raise RuntimeError("native runtime helper end not found")
native_block = text[native_start:native_end]
if 'row["metrics"]["compiler_wall_time"]' not in native_block:
    native_block_new = native_block.replace(
        "    return (",
        '    return (row["metrics"]["compiler_wall_time"]\n            and ',
        1)
    text = text[:native_start] + native_block_new + text[native_end:]

write(path, text)

# Documentation: v2 rows, complete audit population, sparse authenticated samples,
# and explicit zero-candidate code policy.
path_doc = "docs/native-retirement-performance-contract.md"
doc = read(path_doc)
doc = replace_once(
    doc,
    "`buster-native-retirement-performance-rows-v1`, integer version `1`",
    "`buster-native-retirement-performance-rows-v2`, integer version `2`",
    "contract row schema",
)
doc = doc.replace(
    "`buster-native-retirement-performance-population-v1`",
    "`buster-native-retirement-performance-population-v2`")
doc = doc.replace(
    "`buster-native-retirement-result-input-plan-v1`",
    "`buster-native-retirement-result-input-plan-v2`")
doc = doc.replace(
    "`buster-native-retirement-execution-plan-v1`",
    "`buster-native-retirement-execution-plan-v2`")
doc = doc.replace(
    "`tp-retirement-block-schedule-v1`",
    "`tp-retirement-block-schedule-v2`")
needle = '''plus explicit boolean eligibility for compiler wall time, peak RSS,
deterministic code-section bytes and generated runtime. Runtime eligibility
must name an independent native executable oracle; code eligibility must name
deterministic code sections.'''
replacement = '''plus explicit boolean eligibility for compiler wall time, peak RSS,
deterministic code-section bytes and generated runtime. The array remains an
exhaustive one-to-one audit join to every census row. Compiler eligibility is
recomputed from the production validator's authenticated
`applicability_skip_rows`; source-proven non-executed rows retain explicit null
admission/oracle observations and never enter the dense timing schedule,
result-input population, or statistical family. Runtime eligibility must name
an independent native executable oracle; code eligibility must name
deterministic code sections.'''
doc = replace_once(doc, needle, replacement, "contract row eligibility prose")
needle = '''Code bytes are deterministic and use the observed exact ratio rather than a
fabricated confidence interval. If a format cannot identify code sections with
an existing validated parser, that cell is unavailable until the parser is
provided; whole-file size cannot be silently substituted.'''
replacement = '''Code bytes are deterministic and use the observed exact ratio rather than a
fabricated confidence interval. A deterministic zero-byte candidate code
payload is retained as the integer observation `0` and is valid against a
positive baseline denominator; no padding or positive-value fabrication is
permitted. A zero baseline code payload has no ratio denominator and is
retained as evidence but excluded from the code-ratio population. If a format
cannot identify code sections with an existing validated parser, that cell is
unavailable until the parser is provided; whole-file size cannot be silently
substituted.'''
doc = replace_once(doc, needle, replacement, "contract zero code prose")
write(path_doc, doc)

workflow = read(".github/workflows/native-retirement-contract.yml")
workflow = replace_once(
    workflow,
    "      - tools/native_retirement_performance_binding.py\n",
    "      - tools/native_retirement_performance_binding.py\n"    "      - tools/native_retirement_performance_schema.py\n"    "      - tools/native_retirement_performance_eligibility_test.py\n",
    "workflow paths",
)
workflow = replace_once(
    workflow,
    "      - name: Validate immutable performance binding schema\n"    "        run: python3 -W error::ResourceWarning tools/native_retirement_performance_binding_test.py -v\n",
    "      - name: Validate immutable performance binding schema\n"    "        run: |\n"    "          python3 -W error::ResourceWarning tools/native_retirement_performance_binding_test.py -v\n"    "          python3 -W error::ResourceWarning tools/native_retirement_performance_eligibility_test.py -v\n",
    "workflow focused tests",
)
write(".github/workflows/native-retirement-contract.yml", workflow)

path_test = "tools/native_retirement_performance_binding_test.py"
tests = read(path_test)
tests = replace_once(
    tests,
    "import native_retirement_performance_binding as binding\n",
    "import native_retirement_performance_binding as binding\n"    "import native_retirement_performance_schema as RETIREMENT_SCHEMA\n",
    "test schema import",
)
old_report_start = '        report_data = {\n            "schema": 2,\n'
old_report_end = '        report_bytes = (json.dumps(report_data, sort_keys=True, separators=(",", ":"))\n'
report_fixture = '''        applicability_rows = list(range(len(census_rows)))
        applicability_counts = {
            name: (len(census_rows) if name == "admitted-supported" else 0)
            for name in RETIREMENT_SCHEMA.APPLICABILITY_CLASSES
        }
        applicability_rows_by_class = {
            name: (applicability_rows if name == "admitted-supported" else [])
            for name in RETIREMENT_SCHEMA.APPLICABILITY_CLASSES
        }
        applicability_records = []
        for row in census_rows:
            applicability_records.append({
                "row": row["row"], "group": row["group"], "fixture": row["fixture"],
                "target": row["target"], "cpu": row["cpu"],
                "frontend": row["frontend_lowering"], "allocator": row["allocator"],
                "PIC": row["PIC"], "applicability": "admitted-supported",
                "admission": "admitted-supported", "disposition": "strict-success",
                "reason": "supported-object-zero-fallback",
                "ownership": "candidate-compiler", "candidate_failure": "0",
                "reference_failure": "0", "acceptance_failure": "0",
            })
        applicability_data = cls._tsv(
            RETIREMENT_SCHEMA.APPLICABILITY_FIELDS, applicability_records)
        applicability_artifact = artifact("census/applicability.tsv", applicability_data)
        skips_data = cls._tsv(RETIREMENT_SCHEMA.APPLICABILITY_SKIP_FIELDS, [])
        skips_artifact = artifact("census/applicability-skips.tsv", skips_data)
        residual_data = (
            b"row\tgroup\tfixture\tfunction\tfunction_id\ttarget\tcpu\tfrontend\t"
            b"allocator\tPIC\tapplicability\tadmission\tdisposition\treason\t"
            b"ownership\tdiagnostic\tstage\topcode_id\tsource_hex\tfunction_hex\t"
            b"line\tcolumn\n")
        residual_artifact = artifact("census/residual.tsv", residual_data)
        report_data = {
            "schema": 2,
            "directories": ["census/shard-0"],
            "shards": 1,
            "profile": "full-census",
            "rows_validated": len(census_rows),
            "groups": len(census_rows) // len(binding.ALLOCATORS),
            "compiler_revision_claim": revision_a,
            "baseline_revision_claim": revision_b,
            "compiler_sha256": candidate_binary["sha256"],
            "baseline_sha256": baseline_binary["sha256"],
            "support_contract_sha256": declaration_artifact["sha256"],
            "supported_gap_ledger_sha256": "d" * 64,
            "applicability_ledger_sha256": "e" * 64,
            "applicability_ledger_entries": 0,
            "resource_include_sha256": "c" * 64,
            "manifest_identity_sha256": manifest_artifact["sha256"],
            "rows_identity_fields": list(binding.ROW_FIELDS),
            "rows_identity_sha256": rows_artifact["sha256"],
            "input_ledger_fields": list(binding.INPUT_FIELDS),
            "input_ledger_sha256": inputs_artifact["sha256"],
            "baseline_dispositions": {},
            "reference_dispositions": {},
            "setup_dispositions": {},
            "candidate_dispositions": {},
            "applicability_classes": list(RETIREMENT_SCHEMA.APPLICABILITY_CLASSES),
            "admission_classes": list(RETIREMENT_SCHEMA.APPLICABILITY_CLASSES),
            "applicability_counts": applicability_counts,
            "admission_counts": dict(applicability_counts),
            "applicability_rows_by_class": applicability_rows_by_class,
            "admission_rows_by_class": dict(applicability_rows_by_class),
            "supported_gap_rows": [],
            "supported_gap_count": 0,
            "supported_gap_sha256": binding._canonical_json_digest([]),
            "applicability_rows": len(census_rows),
            "applicability_evidence": applicability_artifact["path"],
            "applicability_tsv": applicability_artifact["path"],
            "applicability_sha256": applicability_artifact["sha256"],
            "applicability_skip_rows": [],
            "applicability_skip_evidence": skips_artifact["path"],
            "residual_evidence": residual_artifact["path"],
            "residual_tsv": residual_artifact["path"],
            "residual_sha256": residual_artifact["sha256"],
            "residual_rows": 0,
            "residual_limit": 256,
            "residual_truncated": False,
            "candidate_failure_rows": [],
            "direct_reference_failure_rows": [],
            "reference_supplement_sha256": [],
            "reference_failure_rows": [],
            "acceptance_failure_rows": [],
            "inapplicable_rows": [],
            "fallback_defect_rows": [],
            "telemetry_defect_rows": [],
            "execution_defect_rows": [],
            "artifact_defect_rows": [],
            "unexpected_failure_rows": [],
            "require_clean_candidate": True,
            "require_clean_acceptance": True,
            "clean_candidate": True,
            "clean_acceptance": True,
            "complete_row_partition": True,
            "global_identity_unique": True,
        }
'''
tests = replace_between(tests, old_report_start, old_report_end,
                        report_fixture + old_report_end, "test report fixture")
tests = tests.replace(
    '"sample_population": "canonical-performance-rows-with-required-metrics"',
    '"sample_population": "trusted-census-eligible-performance-rows-with-required-metrics"')
tests = tests.replace(
    '"eligible_population": "canonical-performance-rows"',
    '"eligible_population": "authenticated-applicability-minus-nonexecuted-rows"')
write(path_test, tests)

identity = read("tools/native_retirement_performance_identity_test.py")
identity = identity.replace(
    '"sample_population": "canonical-performance-rows-with-required-metrics"',
    '"sample_population": "trusted-census-eligible-performance-rows-with-required-metrics"')
identity = identity.replace(
    '"eligible_population": "canonical-performance-rows"',
    '"eligible_population": "authenticated-applicability-minus-nonexecuted-rows"')
write("tools/native_retirement_performance_identity_test.py", identity)

write("tools/native_retirement_performance_eligibility_test.py", '#!/usr/bin/env python3\n"""Regression tests for #929\'s trusted sparse eligibility projection."""\n\nimport copy\nimport hashlib\nimport json\nimport unittest\n\nimport native_retirement_performance_binding as binding\nimport native_retirement_performance_schema as schema\n\n\nclass RetirementEligibilityTests(unittest.TestCase):\n    def report(self):\n        rows_by_class = {name: [] for name in schema.APPLICABILITY_CLASSES}\n        rows_by_class["admitted-supported"] = [0, 4]\n        rows_by_class["retained-control"] = [1]\n        rows_by_class["platform-inapplicable"] = [2]\n        rows_by_class["unavailable"] = [3]\n        counts = {name: len(rows) for name, rows in rows_by_class.items()}\n        return {\n            "profile": "full-census",\n            "applicability_classes": list(schema.APPLICABILITY_CLASSES),\n            "admission_classes": list(schema.APPLICABILITY_CLASSES),\n            "applicability_counts": counts,\n            "admission_counts": dict(counts),\n            "applicability_rows_by_class": rows_by_class,\n            "admission_rows_by_class": copy.deepcopy(rows_by_class),\n            "applicability_rows": 5,\n            "applicability_skip_rows": [1, 2, 3],\n            "global_identity_unique": True,\n        }\n\n    def test_shared_schema_is_exact_production_62_field_report(self):\n        self.assertEqual(len(schema.VALIDATOR_REPORT_FIELDS), 62)\n        self.assertEqual(len(set(schema.VALIDATOR_REPORT_FIELDS)), 62)\n        self.assertIn("applicability_rows_by_class", schema.VALIDATOR_REPORT_FIELDS)\n        self.assertIn("artifact_defect_rows", schema.VALIDATOR_REPORT_FIELDS)\n        self.assertIn("profile", schema.VALIDATOR_REPORT_FIELDS)\n\n    def test_projection_retains_complete_audit_and_sparse_measurement_set(self):\n        by_row, skipped = binding._validator_projection(self.report(), 5)\n        self.assertEqual(set(by_row), set(range(5)))\n        self.assertEqual(skipped, {1, 2, 3})\n        self.assertEqual({row for row in by_row if row not in skipped}, {0, 4})\n\n    def test_candidate_cannot_supply_missing_duplicate_or_supported_skip(self):\n        cases = []\n        missing = self.report()\n        missing["applicability_rows_by_class"]["admitted-supported"] = [0]\n        missing["applicability_counts"]["admitted-supported"] = 1\n        missing["admission_rows_by_class"]["admitted-supported"] = [0]\n        missing["admission_counts"]["admitted-supported"] = 1\n        cases.append(missing)\n        duplicate = self.report()\n        duplicate["applicability_rows_by_class"]["retained-control"] = [1, 1]\n        duplicate["applicability_counts"]["retained-control"] = 2\n        duplicate["admission_rows_by_class"]["retained-control"] = [1, 1]\n        duplicate["admission_counts"]["retained-control"] = 2\n        cases.append(duplicate)\n        supported_skip = self.report()\n        supported_skip["applicability_skip_rows"] = [0, 1, 2, 3]\n        cases.append(supported_skip)\n        for candidate in cases:\n            with self.subTest(candidate=candidate), self.assertRaises(ValueError):\n                binding._validator_projection(candidate, 5)\n\n    def test_row_parser_accepts_authenticated_untimed_control(self):\n        base_identity = {\n            "fixture": "tests/control.c", "target": "x86_64-unknown-linux-gnu",\n            "target_abi": "systemv-x86_64", "cpu": "baseline",\n            "cpu_features": "baseline", "allocator": "none",\n            "frontend_lowering": "direct-ssa", "PIC": "0",\n            "fixture_recipe": "compiler-default",\n            "compile_obligation": "registered-non-object-control",\n            "link_obligation": "semantic-gate-509",\n            "execution_obligation": "semantic-gate-509",\n            "diagnostic_obligation": "none", "argv_evidence": "groups/0/none.argv",\n            "artifact_stage": "object",\n        }\n        rows = []\n        for row_id, stage in enumerate(("object", "link")):\n            identity = dict(base_identity)\n            identity["fixture"] = f"tests/eligible-{row_id}.c"\n            identity["compile_obligation"] = "supported-object-zero-fallback"\n            identity["artifact_stage"] = stage\n            eligibility = {\n                "compiler_wall_time": True, "compiler_peak_rss": True,\n                "generated_code_bytes": True,\n                "generated_runtime": stage == "link",\n                "runtime_oracle": ("independent-native-executable-oracle"\n                                   if stage == "link" else "not-applicable"),\n                "code_section": "deterministic-code-section",\n            }\n            rows.append({"row": row_id, "identity": identity,\n                         "eligibility": eligibility})\n        rows.append({\n            "row": 2,\n            "identity": base_identity,\n            "eligibility": {\n                "compiler_wall_time": False, "compiler_peak_rss": False,\n                "generated_code_bytes": False, "generated_runtime": False,\n                "runtime_oracle": "not-applicable", "code_section": "not-applicable",\n            },\n        })\n        zero_identity = dict(base_identity)\n        zero_identity["fixture"] = "tests/empty-code.c"\n        zero_identity["compile_obligation"] = "supported-object-zero-fallback"\n        rows.append({\n            "row": 3,\n            "identity": zero_identity,\n            "eligibility": {\n                "compiler_wall_time": True, "compiler_peak_rss": True,\n                "generated_code_bytes": False, "generated_runtime": False,\n                "runtime_oracle": "not-applicable",\n                "code_section": "deterministic-zero-baseline-code-section",\n            },\n        })\n        value = {\n            "schema": binding.ROW_SCHEMA, "version": binding.ROW_VERSION,\n            "row_identity_fields": list(binding.ROW_IDENTITY_FIELDS),\n            "sources": {name: "a" * 64 for name in binding.ROW_SOURCE_ROLES},\n            "rows": rows,\n        }\n        data = (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\\n").encode()\n        parsed, _axes, family, _sources = binding._performance_rows_with_sources(data)\n        self.assertFalse(parsed[2]["metrics"]["compiler_wall_time"])\n        self.assertNotIn("compiler_wall_time/cell/row=2", family["members"])\n        self.assertFalse(parsed[3]["metrics"]["generated_code_bytes"])\n        self.assertEqual(parsed[3]["eligibility"]["code_section"],\n                         "deterministic-zero-baseline-code-section")\n\n    def test_execution_schedule_excludes_authenticated_untimed_rows(self):\n        rows = [\n            {"row": 0, "metrics": {"compiler_wall_time": True,\n                                    "generated_runtime": False}},\n            {"row": 1, "metrics": {"compiler_wall_time": False,\n                                    "generated_runtime": False}},\n            {"row": 2, "metrics": {"compiler_wall_time": True,\n                                    "generated_runtime": True}},\n        ]\n        sampling = {"seed": 7, "warmups_per_variant": 0,\n                    "rounds": 1, "pairs_per_round": 2}\n        schedule = list(binding._execution_schedule(rows, sampling))\n        compiler_rows = {item["row"] for item in schedule\n                         if item["kind"] == "compiler"}\n        runtime_rows = {item["row"] for item in schedule\n                        if item["kind"] == "runtime"}\n        self.assertEqual(compiler_rows, {0, 2})\n        self.assertEqual(runtime_rows, {2})\n        self.assertNotIn(1, {item["row"] for item in schedule})\n\n    def test_zero_candidate_code_is_retained_but_zero_baseline_has_no_ratio(self):\n        row = {"row": 0, "metrics": {\n            "compiler_wall_time": False, "compiler_peak_rss": False,\n            "generated_code_bytes": True, "generated_runtime": False}}\n        value = {\n            "record_id": "row-0/round-0/pair-0", "row": 0,\n            "round": 0, "pair": 0,\n            "measurements": {\n                "generated_code_bytes": {"baseline": 1, "candidate": 0},\n            },\n        }\n        seen = [0]\n        binding._consume_result_record(\n            value, {0: 0}, {0: row}, 1, 1, 0, seen, hashlib.sha256())\n        self.assertEqual(seen, [1])\n        value["measurements"]["generated_code_bytes"]["baseline"] = 0\n        with self.assertRaises(ValueError):\n            binding._consume_result_record(\n                value, {0: 0}, {0: row}, 1, 1, 0, [0], hashlib.sha256())\n\n    def test_zero_code_payload_is_exact_not_padded(self):\n        empty = hashlib.sha256(b"").hexdigest()\n        self.assertEqual(binding._bounded_code_bytes(0, "empty"), 0)\n        self.assertEqual(len(empty), 64)\n        with self.assertRaises(ValueError):\n            binding._bounded_code_bytes(0, "baseline", positive=True)\n\n\nif __name__ == "__main__":\n    unittest.main()\n')

for relative in ("tools/apply_929_patch.py", ".github/workflows/apply-929-patch.yml"):
    target = ROOT / relative
    if target.exists():
        target.unlink()
