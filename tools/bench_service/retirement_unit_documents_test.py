#!/usr/bin/env python3
"""The validator accepts the in-unit campaign's workflow documents (#881-D).

Usage: retirement_unit_documents_test.py DIRECTORY

The prep runner (retirement_prepare_tests.c, bq_prep_campaign_documents) has
the unit driver's document steps (bq_retirement_unit_campaign_documents and
_post_aa_document) write the oracle, execution-plan (v3), result-input-plan
(v3), pre-sample and post-A/A documents into DIRECTORY, beside context.json
naming each document's role, path and descriptor, the installed census the
rows were pinned from and the values the driver derived. This script rederives every source it can from the
census itself and runs the validator's own checks over the documents:
_performance_rows_with_sources and _family_member_counts (the family and its
counts), _check_execution_plan (group, untimed and row contracts joined to
the oracle), _result_input_plan, _workflow_phase and the pre-sample and
post-A/A field joins. The oracle-record and result-population checks are
inline in _check_workflow_evidence_open, so their conditions are repeated
here. Every document must also be the canonical JSON serialization of its
value. A tampered execution plan must be refused.
"""

import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile


REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / "tools"))

import native_retirement_performance_binding as binding


DOCUMENTS = ("oracle", "execution_plan", "result_input_plan", "pre_sample_plan",
             "post_aa_binding")


def fail(message):
    raise ValueError(message)


def file_sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def canonical(root, descriptor, name):
    """Check the descriptor against the file and return its canonical value."""
    binding._check_evidence(root, descriptor, name)
    data = (root / descriptor["path"]).read_bytes()
    value = json.loads(data.decode("utf-8"), object_pairs_hook=binding._json_object)
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":"),
                         ensure_ascii=False).encode("utf-8")
    if encoded != data:
        fail(f"{name} is not the canonical JSON serialization of its value")
    return value


def check_oracle(oracle, parsed, support_output, native_target):
    """_check_workflow_evidence_open's oracle-record conditions."""
    oracle = binding._keys(oracle, ("schema", "version", "source_manifest_sha256",
                                    "source_rows_sha256", "records"), "oracle")
    if oracle["schema"] != binding.ORACLE_SCHEMA or oracle["version"] != 1:
        fail("oracle record schema/version is not approved")
    if oracle["source_manifest_sha256"] != support_output["manifest_sha256"] \
            or oracle["source_rows_sha256"] != support_output["rows_sha256"]:
        fail("oracle records do not bind the actual schema-2 source artifacts")
    seen = set()
    for index, item in enumerate(binding._list(oracle["records"], "oracle.records")):
        item = binding._keys(item, ("row", "code_section_status", "code_section_bytes",
                                    "code_section_sha256", "runtime_oracle_status",
                                    "runtime_exit_code", "native_runtime"),
                             f"oracle.records[{index}]")
        if type(item["row"]) is not int or not 0 <= item["row"] < len(parsed) \
                or item["row"] in seen:
            fail("oracle record row is outside or duplicates the canonical population")
        seen.add(item["row"])
        row = parsed[item["row"]]
        compile_eligible = row["row"] in support_output["compiler_eligible_rows"]
        if compile_eligible:
            if item["code_section_status"] != "parsed-deterministic":
                fail("eligible oracle record code section was not independently parsed")
            binding._bounded_code_bytes(item["code_section_bytes"], "oracle code-section size")
            binding._sha(item["code_section_sha256"], "oracle code-section digest")
            if item["code_section_bytes"] == 0 \
                    and item["code_section_sha256"] != hashlib.sha256(b"").hexdigest():
                fail("zero-byte oracle record does not bind the empty payload")
        elif item["code_section_status"] != "not-applicable" \
                or item["code_section_bytes"] is not None \
                or item["code_section_sha256"] is not None:
            fail("untimed oracle record must retain explicit null code observations")
        if item["runtime_oracle_status"] not in {"passed-native", "not-applicable"}:
            fail("oracle record runtime status is not approved")
        if item["runtime_oracle_status"] == "passed-native" and item["runtime_exit_code"] != 0:
            fail("passing native oracle must have exit code zero")
        if item["runtime_oracle_status"] == "not-applicable" \
                and item["runtime_exit_code"] != (-1 if compile_eligible else None):
            fail("inapplicable runtime oracle has an invalid absent observation")
        binding._boolean(item["native_runtime"], "oracle native_runtime")
        if item["native_runtime"] is not (item["runtime_oracle_status"] == "passed-native"):
            fail("oracle native-runtime flag contradicts its runtime status")
        required = compile_eligible and binding._native_runtime_required(row, native_target)
        if item["native_runtime"] is not required:
            fail("oracle native-runtime applicability differs from the frozen row obligation")
        code_parsed = item["code_section_status"] == "parsed-deterministic"
        code_ratio = code_parsed and compile_eligible and item["code_section_bytes"] > 0
        if row["metrics"]["generated_code_bytes"] is not code_ratio \
                or row["metrics"]["generated_runtime"] is not (item["native_runtime"] and compile_eligible):
            fail("canonical row eligibility is not derived from the oracle records")
    if seen != set(range(len(parsed))):
        fail("oracle records do not cover every canonical performance row")


def check_populations(result_plan, parsed):
    """_check_workflow_evidence_open's result-population joins."""
    sampled = [row["row"] for row in binding._timed_rows(parsed)
               if any(row["metrics"].get(metric, False) for metric in binding.ROW_SAMPLE_METRICS)]
    groups = binding._object_groups(binding._batch_groups(parsed))
    per_unit = result_plan["rounds"] * result_plan["pairs_per_round"]
    rows = result_plan["populations"]["rows"]
    batches = result_plan["populations"]["batches"]
    if rows["sample_count"] != len(sampled) or rows["required_records"] != len(sampled) * per_unit:
        fail("result-input plan does not cover every timed canonical row")
    if batches["sample_count"] != len(groups) \
            or batches["required_records"] != len(groups) * per_unit:
        fail("result-input plan batch population is not every object batch group")


def check_phase(root, descriptor, name, expected_sources, execution_plan):
    fields = ("schema", "version", "status", *expected_sources, "execution_plan")
    post = name == "post_aa_binding"
    if post:
        fields = (*fields, "pre_sample_plan_sha256", "aa_admission_sha256")
    canonical(root, descriptor, f"workflow.{name}")
    phase = binding._workflow_phase(root, descriptor, binding.PHASE_SCHEMA[name], name)
    phase = binding._keys(phase, fields, f"workflow.{name}")
    status = "bound-after-aa-before-samples" if post else "frozen-before-samples"
    if phase["status"] != status:
        fail(f"workflow.{name} has the wrong status")
    for field, expected in expected_sources.items():
        if phase[field] != expected:
            fail(f"workflow.{name}.{field} does not bind the frozen plan")
    binding._artifact(phase["execution_plan"], f"workflow.{name}.execution_plan")
    if phase["execution_plan"] != execution_plan:
        fail(f"workflow.{name} does not bind the execution plan descriptor")
    return phase


def validate(root, context):
    census = Path(context["census"])
    performance_bytes = (census / "performance-rows.json").read_bytes()
    parsed, _axes, family, sources = binding._performance_rows_with_sources(performance_bytes)
    rows_tsv = (census / "rows.tsv").read_bytes()
    support_output = {
        "support_declaration_sha256": file_sha256(census / "support.tsv"),
        "manifest_sha256": file_sha256(census / "manifest.txt"),
        "rows_sha256": hashlib.sha256(rows_tsv).hexdigest(),
        # The declaration's full object row count: every census row.
        "object_row_count": len(rows_tsv.decode("utf-8").splitlines()) - 1,
        "compiler_eligible_rows": {row["row"] for row in parsed
                                   if row["metrics"]["compiler_wall_time"]},
    }
    for role, field in (("support_declaration", "support_declaration_sha256"),
                        ("manifest", "manifest_sha256"), ("rows", "rows_sha256")):
        if sources[role] != support_output[field]:
            fail(f"the pinned performance rows do not name the census {role}")
    counts = binding._family_member_counts(family)
    sampling = {"seed": context["seed"], "rounds": 2, "pairs_per_round": context["pairs_per_round"],
                "warmups_per_variant": 2, "resamples": context["resamples"],
                "bootstrap_members_per_scope": counts["bootstrap_members_per_scope"],
                "cell_members_per_scope": counts["cell_members_per_scope"]}
    # The unit's own family digest and counts are the validator's.
    if context["family_sha256"] != family["sha256"] \
            or context["bootstrap_members_per_scope"] != counts["bootstrap_members_per_scope"] \
            or context["cell_members_per_scope"] != counts["cell_members_per_scope"]:
        fail("the unit's statistical family differs from the validator's derivation")
    if context["performance_rows_sha256"] != hashlib.sha256(performance_bytes).hexdigest() \
            or context["support_declaration_sha256"] != support_output["support_declaration_sha256"] \
            or context["manifest_sha256"] != support_output["manifest_sha256"] \
            or context["rows_sha256"] != support_output["rows_sha256"] \
            or context["object_row_count"] != support_output["object_row_count"]:
        fail("the unit's sources differ from the census")
    documents = context["documents"]
    if set(documents) != set(DOCUMENTS):
        fail("the context does not name the five documents")
    oracle = canonical(root, documents["oracle"], "workflow.records.oracle")
    check_oracle(oracle, parsed, support_output, binding.NATIVE_TIMED_TARGET)
    support_files = [{"path": role, "bytes": 1, "sha256": "0" * 64}
                     for role in binding.SUPPORT_FILE_ROLES]
    support_files[binding.SUPPORT_FILE_ROLES.index("performance_rows")] = {
        "path": "performance-rows.json", "bytes": len(performance_bytes),
        "sha256": hashlib.sha256(performance_bytes).hexdigest()}
    record = {"support": {"files": support_files},
              "workflow": {"records": {"oracle": documents["oracle"]}}}
    execution_plan = documents["execution_plan"]
    canonical(root, execution_plan, "execution_plan")
    binding._check_execution_plan(root, execution_plan, record, parsed, sampling,
                                  context["cpu"], binding.NATIVE_TIMED_TARGET)
    result_descriptor = documents["result_input_plan"]
    canonical(root, result_descriptor, "workflow.result_input_plan")
    result_plan = binding._result_input_plan(root, result_descriptor, support_output, None,
                                             {"sampling": sampling})
    check_populations(result_plan, parsed)
    manifests = [result_plan["populations"][kind]["manifest_count"] for kind in ("rows", "batches")]
    if manifests != context["manifest_counts"]:
        fail("the unit's returned partitions differ from its result-input plan")
    expected_sources = {
        "support_declaration_sha256": support_output["support_declaration_sha256"],
        "manifest_sha256": support_output["manifest_sha256"],
        "rows_sha256": support_output["rows_sha256"],
        "family_sha256": family["sha256"],
        "seed": sampling["seed"],
        "rounds": sampling["rounds"],
        "pairs_per_round": sampling["pairs_per_round"],
        "resamples": sampling["resamples"],
        "bootstrap_members_per_scope": sampling["bootstrap_members_per_scope"],
        "cell_members_per_scope": sampling["cell_members_per_scope"],
        "result_input_plan_sha256": result_descriptor["sha256"],
    }
    pre_descriptor = documents["pre_sample_plan"]
    pre = check_phase(root, pre_descriptor, "pre_sample_plan", expected_sources, execution_plan)
    post = check_phase(root, documents["post_aa_binding"], "post_aa_binding",
                       expected_sources, execution_plan)
    if post["pre_sample_plan_sha256"] != pre_descriptor["sha256"]:
        fail("workflow.post_aa_binding does not bind pre-sample planning")
    if post["aa_admission_sha256"] != context["aa_admission_sha256"]:
        fail("workflow.post_aa_binding does not bind the admitted AA receipt")
    if post["execution_plan"] != pre["execution_plan"]:
        fail("post-AA execution plan differs from its pre-sample plan")
    return parsed, sampling, record


def refuses_tampered_plan(root, context, parsed, sampling, record):
    """The same check refuses an execution plan whose seed changed."""
    scratch = Path(tempfile.mkdtemp(prefix="bq-retirement-unit-documents-tamper-"))
    refused = False
    try:
        for role in DOCUMENTS:
            path = context["documents"][role]["path"]
            shutil.copyfile(root / path, scratch / path)
        plan_name = context["documents"]["execution_plan"]["path"]
        plan_path = scratch / plan_name
        value = json.loads(plan_path.read_text(encoding="utf-8"))
        value["seed"] += 1
        data = json.dumps(value, sort_keys=True, separators=(",", ":"),
                          ensure_ascii=False).encode("utf-8")
        plan_path.write_bytes(data)
        descriptor = {"path": plan_name, "bytes": len(data),
                      "sha256": hashlib.sha256(data).hexdigest()}
        try:
            binding._check_execution_plan(scratch, descriptor, record, parsed, sampling,
                                          context["cpu"], binding.NATIVE_TIMED_TARGET)
        except ValueError:
            refused = True
    finally:
        shutil.rmtree(scratch)
    return refused


def main():
    if len(sys.argv) != 2:
        print("usage: retirement_unit_documents_test.py DIRECTORY", file=sys.stderr)
        return 2
    root = Path(sys.argv[1]).resolve()
    status = 1
    try:
        context = json.loads((root / "context.json").read_text(encoding="utf-8"))
        parsed, sampling, record = validate(root, context)
        if not refuses_tampered_plan(root, context, parsed, sampling, record):
            fail("a tampered execution plan was accepted")
        print(f"RETIREMENT_UNIT_DOCUMENTS accepted rows={len(parsed)} "
              f"family={context['family_sha256'][:12]}")
        status = 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"RETIREMENT_UNIT_DOCUMENTS refused: {error}", file=sys.stderr)
    return status


if __name__ == "__main__":
    sys.exit(main())
