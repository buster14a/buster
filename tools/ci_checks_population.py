#!/usr/bin/env python3
"""Offline prospective standard-hosted comparison for #2610/#2119/#2120.

prepare() freezes source/producer, exact census catalogue and reader bytes.
qualify() authenticates individual samples with the unchanged v2 reader, checks
the published declaration and ordered dispatch prefix, and reports population
medians. validate_census() admits only complete observed per-row profiles from
the source-reviewed catalogue. Timing arithmetic never grants landing approval.

Prepare: python3 -B tools/ci_checks_population.py --prepare DECLARATION.json
Read:    python3 -B tools/ci_checks_population.py CAMPAIGN.json
Campaign has schema=buster-ci-checks-population-v1, declaration=REF,
publication=REF, dispatch_inventory=REF and attempts=[{ordinal,run:REF,sample:REF|null,input_evidence:REF|null,
intake_completed_at:ISO|null}]. Paths are relative to the containing manifest.
Publication is the retained original issue #2610 API comment containing the
returned declaration marker. REF is {path,sha256}; no network or native runs.
Dispatch inventory binds every complete paginated workflow_dispatch API page
since publication, including all three exact refs and failed/retried runs.
Retained digests bind bytes and joins, not fabricated records' external origin.
Exit 2 means pending/incomplete, 1 means complete timing targets rejected.
Overall acceptance always requires the separately recorded population and
resource/deadline/capture/cleanup/reliability review.
"""

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import statistics
import sys

import analyzer_reference
import ci_checks_qualification as qualification
import github_ci_time as github

SCHEMA = "buster-ci-checks-population-v1"
DECLARATION_SCHEMA = "buster-ci-checks-population-declaration-v1"
CATALOGUE_SCHEMA = "buster-ci-checks-native-census-v1"
CATALOGUE_SHA256 = "22073da1881d5798d6c4edba447e13ec2ded8ab30727c401b5d3cddec06e9229"
BLOCKS = ("CAB", "ACB", "CBA", "BAC", "BCA", "ABC", "ABC", "CBA", "ACB", "BCA", "BAC", "CAB")
SEED = "5bb691c893e292c2469686a078cfe9ae50b6afe8ed510da4d99785428dd85050"
VARIANTS = dict(zip("ABC", qualification.VARIANTS))
EXCLUDED_RUNS = [37191738110, 37193669465]
INPUT_PRODUCERS = {"tools/analyzer_reference.py": "9fb89b30c21a16886cd2cdcddbf11acf4f58f227",
                   "tools/ci_configure_evidence.py": "7d052f908a5a42293f2e476728909f1e86ba1aae"}
READER_FILES = ("ci_checks_population.py", "ci_checks_qualification.py", "ci_matrix_phases.py",
                "ci_summary_core.py", "ci_unit_tests_measure.py", "ci_unit_tests_campaign.py", "github_ci_time.py",
                "ci_checks_dispatch_inventory.py", "analyzer_reference.py")
PENDING_REVIEWS = ["native-profile population imbalance and timing uncertainty",
                   "original dispatch requests and evidence collection origin", "CPU time and memory observations",
                   "resource/deadline/capture/cleanup/reliability comparison"]
require = qualification.require


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def reader_bindings():
    return {name: digest(Path(__file__).parent / name) for name in READER_FILES}


def marker(declaration_digest):
    return "CI_CHECKS_POPULATION_DECLARATION_V1 sha256=" + declaration_digest


def read_declaration(root, reference):
    path = qualification.retained(root, reference)
    value = qualification.phases.read(path)
    fields = {"schema", "repository", "experiment", "cohort", "source_tree", "producer_blobs",
              "reader_sha256", "catalogue", "blocks", "seed", "excluded_runs", "dispatch_inputs", "input_producer_blobs"}
    require(set(value) == fields and value["schema"] == DECLARATION_SCHEMA and
            value["repository"] == "buster14a/buster" and value["experiment"] == "issue2610-standard-hosted-v1",
            "unknown population declaration")
    require(value["blocks"] == list(BLOCKS) and value["seed"] == SEED and value["excluded_runs"] == EXCLUDED_RUNS,
            "changed prospective order, budget or archived-run disposition")
    require(json.dumps(value["dispatch_inputs"], sort_keys=True) ==
            json.dumps({"cmake_profile": False, "analyzer_comparison": False}, sort_keys=True),
            "optional diagnostic inputs are not declared false")
    require(value["reader_sha256"] == reader_bindings(), "reader bytes differ from prospective declaration")
    require(value["input_producer_blobs"] == INPUT_PRODUCERS, "input witness producer bindings changed")
    require(value["catalogue"]["sha256"] == CATALOGUE_SHA256, "unreviewed census catalogue")
    catalogue = qualification.record(path.parent, value["catalogue"])
    require(catalogue["schema"] == CATALOGUE_SCHEMA, "unknown census catalogue schema")
    cohort = qualification.cohort(value)
    require(cohort["name"] == qualification.PROSPECTIVE_COHORT and
            all(cohort[k] == catalogue[k] for k in ("head_sha", "workflow_blob_sha")) and
            value["source_tree"] == catalogue["source_tree"] and value["producer_blobs"] == catalogue["producer_blobs"],
            "declaration differs from reviewed source/tree/workflow/producer bindings")
    for name in READER_FILES:
        producer = catalogue["producer_blobs"].get("tools/" + name, INPUT_PRODUCERS.get("tools/" + name))
        if producer is not None:
            data = (Path(__file__).parent / name).read_bytes()
            blob = hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()
            require(blob == producer, "accepted sample-reader dependency changed: " + name)
    return value, catalogue


def publication(root, reference, declaration_reference):
    receipt = json.loads(qualification.retained(root, reference).read_bytes())
    identifier = receipt.get("id")
    require(type(identifier) is int and identifier > 0 and
            receipt.get("issue_url") == "https://api.github.com/repos/buster14a/buster/issues/2610" and
            receipt.get("url") == f"https://api.github.com/repos/buster14a/buster/issues/comments/{identifier}" and
            receipt.get("html_url") == f"https://github.com/buster14a/buster/issues/2610#issuecomment-{identifier}",
            "declaration publication is not the original issue2610 comment")
    body = receipt.get("body")
    require(isinstance(body, str) and body.splitlines().count(marker(declaration_reference["sha256"])) == 1,
            "published declaration digest differs or is not unique")
    created = github.timestamp(receipt.get("created_at"))
    updated = github.timestamp(receipt.get("updated_at"))
    require(created is not None and updated == created, "declaration publication is missing time or was edited")
    return created


def validate_census(observed, catalogue):
    expected = catalogue["platforms"]
    actual = observed["platforms"]
    require(set(actual) == set(expected), "native platform inventory changed")
    for platform, rows in actual.items():
        wanted = expected[platform]
        require(json.dumps(rows["identity"], sort_keys=True) == json.dumps(wanted["identity"], sort_keys=True) and
                rows["selected"] == wanted["selected"],
                "native platform source/policy/toolchain/quota/selected rows changed: " + platform)
        require(set(rows["census"]) == set(wanted["census"]), "runtime row census changed: " + platform)
        for row_id, census in rows["census"].items():
            options = wanted["census"][row_id]
            require(isinstance(options, list) and options, "empty reviewed row census")
            matches = [option for option in options if json.dumps(option["native_host_profile"], sort_keys=True) ==
                       json.dumps(census.get("native_host_profile"), sort_keys=True)]
            require(len(matches) == 1, "unknown or ambiguous complete native profile: " + row_id)
            require(json.dumps(census, sort_keys=True) == json.dumps(matches[0], sort_keys=True),
                    "complete module/assertion census differs for observed profile: " + row_id)


def input_evidence(root, reference, sample_root, item, run, source_tree):
    """Bind actual worker input flags to the already validated artifact siblings."""
    path = qualification.retained(root, reference)
    value = qualification.phases.read(path)
    require(set(value) == {"schema", "configure", "analyzer_selection"} and
            value["schema"] == "buster-ci-checks-population-inputs-v1", "unknown input witness schema")
    witnesses = value["configure"]
    require(isinstance(witnesses, list) and
            Counter(witness["job"] for witness in witnesses) == Counter(desktop["job"] for desktop in item["desktops"]),
            "missing/duplicate desktop input witnesses")
    by_job = {witness["job"]: witness for witness in witnesses}
    conditions = qualification.record(sample_root, item["conditions"])["jobs"]
    for desktop in item["desktops"]:
        witness = by_job[desktop["job"]]
        require(set(witness) == {"job", "manifest"}, "unknown configure witness fields")
        actual = qualification.retained(path.parent, witness["manifest"])
        expected = (sample_root / desktop["phase_directory"]).parent / "configure" / "manifest.json"
        require(actual.resolve() == expected.resolve(), "configure witness is not the exact desktop artifact sibling")
        receipt = qualification.phases.read(actual)
        require(type(receipt.get("schema")) is int and receipt["schema"] == 1 and
                receipt.get("kind") == "cmake-configure-evidence" and receipt.get("role") == "diagnostic-only" and
                receipt.get("profile_requested") is False and type(receipt.get("profiles_captured")) is int and
                receipt["profiles_captured"] == 0 and receipt.get("errors") == [], "configure profiling input is not observed false")
        condition = conditions[desktop["job"]]
        identity = {"GITHUB_REPOSITORY": "buster14a/buster", "GITHUB_SHA": run["head_sha"],
                    "GITHUB_RUN_ID": str(run["id"]), "GITHUB_RUN_ATTEMPT": "1",
                    "RUNNER_OS": {"Linux": "Linux", "Windows": "Windows", "macOS": "macOS"}[desktop["job"].split(" ", 1)[0]],
                    "RUNNER_ARCH": "X64" if "x86-64" in desktop["job"] else "ARM64",
                    "ImageOS": condition["image_os"], "ImageVersion": condition["image_version"]}
        require(receipt.get("identity") == identity, "configure input witness source/run/attempt/runner identity mismatch")
    selection_path = qualification.retained(path.parent, value["analyzer_selection"])
    ninja = qualification.retained(sample_root, conditions["Clang analyzer shards"]["selected_tools"]["ninja"])
    require(selection_path.resolve() == (ninja.parent / "comparison-selection.txt").resolve(),
            "analyzer input witness is not the exact selected-tool artifact sibling")
    try:
        selection = analyzer_reference.load_selection(selection_path)
    except analyzer_reference.ProvenanceError as error:
        raise ValueError(str(error)) from error
    expected = {"event": "workflow_dispatch", "requested": "false", "selection": "skip", "reason": "same-revision",
                "candidate_revision": run["head_sha"], "reference_revision": run["head_sha"],
                "candidate_tree": source_tree, "reference_tree": source_tree,
                "candidate_complete": "true", "reference_complete": "true"}
    require(all(selection[key] == expected_value for key, expected_value in expected.items()),
            "analyzer comparison input/source/tree is not observed false")
    require(all(selection["candidate_" + key] == selection["reference_" + key]
                for key in ("closure_sha256", "manifest_sha256")), "same-revision analyzer provenance differs")
    return {"cmake_profile": False, "analyzer_comparison": False, "evidence": reference}


def dispatch_inventory(root, reference, published, attempts):
    """Reconcile exhaustive original API pages with every supplied ordinal."""
    manifest_path = qualification.retained(root, reference)
    value = qualification.phases.read(manifest_path)
    require(set(value) == {"schema", "endpoint", "event", "created_after", "per_page", "collected_at", "pages"} and
            value["schema"] == "buster-ci-checks-dispatch-inventory-v1" and
            value["endpoint"] == "https://api.github.com/repos/buster14a/buster/actions/workflows/ci.yml/runs" and
            value["event"] == "workflow_dispatch" and value["per_page"] == 100 and
            github.timestamp(value["created_after"]) == published, "unknown dispatch inventory scope")
    collected = github.timestamp(value["collected_at"])
    require(collected is not None and collected >= published, "unknown dispatch inventory capture time")
    pages = value["pages"]
    require(isinstance(pages, list) and pages, "missing paginated dispatch history")
    records = []
    total = None
    for index, page in enumerate(pages, 1):
        require(set(page) == {"page", "response"} and type(page["page"]) is int and page["page"] == index,
                "missing or reordered dispatch API page")
        response = json.loads(qualification.retained(manifest_path.parent, page["response"]).read_bytes())
        if total is None:
            total = response.get("total_count")
        require(type(total) is int and 0 <= total < 1000 and response.get("total_count") == total,
                "dispatch history meets/exceeds API cap or changed during pagination")
        rows = response.get("workflow_runs")
        expected = min(100, max(0, total - (index - 1) * 100))
        require(isinstance(rows, list) and len(rows) == expected, "dispatch API page is incomplete")
        records.extend(rows)
    require(len(pages) == max(1, (total + 99) // 100) and len(records) == total,
            "dispatch history pagination incomplete")
    require(all(type(record.get("id")) is int and record["id"] > 0 for record in records) and
            len({record["id"] for record in records}) == total, "duplicate or invalid dispatch API run")
    branches = set(qualification.COHORT_BRANCHES[qualification.PROSPECTIVE_COHORT].values())
    selected = []
    for record in records:
        created = github.timestamp(record.get("created_at"))
        require(record.get("event") == "workflow_dispatch" and record.get("path") == ".github/workflows/ci.yml" and
                created is not None and published <= created <= collected, "dispatch API record outside inventory scope")
        if record.get("head_branch") in branches:
            selected.append(record)
    selected.sort(key=lambda record: (github.timestamp(record["created_at"]), record["id"]))
    archived = [qualification.record(root, attempt["run"]) for attempt in attempts]
    require([r["id"] for r in selected] == [r["id"] for r in archived],
            "supplied ordinals omit or select from actual dispatch history")
    fields = ("id", "head_sha", "head_branch", "created_at", "run_attempt", "status", "conclusion")
    for original, archive, attempt in zip(selected, archived, attempts):
        require(all(original.get(key) == archive.get(key) for key in fields), "dispatch API record differs from retained attempt")
        intake = github.timestamp(attempt.get("intake_completed_at"))
        require(intake is None or intake <= collected, "dispatch history captured before archive intake")
    return {"collected_at": value["collected_at"], "api_runs": total, "campaign_dispatches": len(selected),
            "complete_pages": len(pages), "run_ids": [record["id"] for record in selected]}


def job_timing(run, variant):
    """Preserve authentic timestamps; metadata is outside required-job end."""
    measured = qualification.timing(run, variant, qualification.PROSPECTIVE_COHORT)
    required = set(qualification.cohort_jobs(variant))
    created = github.timestamp(run["created_at"])
    end = max(github.timestamp(job["completed_at"]) for job in run["jobs"] if job["name"] in required)
    measured["elapsed_seconds"] = (end - created).total_seconds()
    measured["workflow_to_job_creation_seconds"] = {job["name"]: (github.timestamp(job["created_at"]) - created).total_seconds()
                                                     for job in run["jobs"] if not qualification.skipped_reuse(job)}
    executed_end = max(github.timestamp(job["completed_at"]) for job in run["jobs"] if not qualification.skipped_reuse(job))
    return measured, executed_end


def population_summary(observations):
    populations = defaultdict(dict)
    for observation in observations:
        for platform, rows in observation["platforms"].items():
            for row_id, census in rows["census"].items():
                profile = census.get("native_host_profile")
                key = json.dumps(profile, sort_keys=True, separators=(",", ":"))
                records = populations[platform + "/" + row_id]
                if key not in records:
                    records[key] = {"native_host_profile": profile, "variants": Counter(),
                                    "module_assertions": {name: values.get("assertions") for name, values in census.get("modules", {}).items()}}
                records[key]["variants"][observation["variant"]] += 1
    return {row_id: list(records.values()) for row_id, records in sorted(populations.items())}


def timing_verdict(observations):
    groups = defaultdict(list)
    for observation in observations:
        groups[observation["variant"]].append(observation["timing"])
    require(Counter({v: len(group) for v, group in groups.items()}) == Counter({v: 12 for v in qualification.VARIANTS}),
            "need all 36 first attempts, 12 per variant")
    metrics = {v: ("elapsed_seconds", "runner_seconds") +
               (("windows_checks_seconds",) if v != "split-overlap" else ()) for v in qualification.VARIANTS}
    summary = {variant: {metric: {"median": statistics.median(sample[metric] for sample in samples),
                                 "minimum": min(sample[metric] for sample in samples),
                                 "maximum": max(sample[metric] for sample in samples),
                                 "mad": statistics.median(abs(sample[metric] - statistics.median(s[metric] for s in samples)) for sample in samples)}
                        for metric in metrics[variant]} for variant, samples in groups.items()}
    baseline = summary["combined-overlap"]
    issues = {}
    for issue, variant, metric, ratio in (("2119", "combined-all-builds", "windows_checks_seconds", .90),
                                         ("2120", "split-overlap", "elapsed_seconds", .85)):
        require(baseline[metric]["median"] > 0 and baseline["runner_seconds"]["median"] > 0, "non-positive timing denominator")
        time_ratio = summary[variant][metric]["median"] / baseline[metric]["median"]
        runner_ratio = summary[variant]["runner_seconds"]["median"] / baseline["runner_seconds"]["median"]
        issues[issue] = {"variant": variant, "metric": metric, "time_ratio": time_ratio,
                         "maximum_time_ratio": ratio, "runner_seconds_ratio": runner_ratio,
                         "maximum_runner_seconds_ratio": 1.05,
                         "timing_status": "accepted" if time_ratio <= ratio and runner_ratio <= 1.05 else "rejected"}
    return summary, issues


def qualify(path):
    path = Path(path)
    output = {"schema": SCHEMA, "status": "pending", "performance_accepted": False,
              "timing_status": "pending", "timing_contract_met": False, "errors": [],
              "dispatches": [], "samples": [], "native_populations": {}, "pending_reviews": list(PENDING_REVIEWS)}
    observations = []
    try:
        campaign = qualification.phases.read(path)
        require(set(campaign) == {"schema", "declaration", "publication", "dispatch_inventory", "attempts"} and campaign["schema"] == SCHEMA,
                "unknown population campaign schema/fields")
        attempts = campaign["attempts"]
        require(isinstance(attempts, list), "dispatch history is not an ordered list")
        output["dispatches"] = [{"ordinal": item.get("ordinal"), "run": item.get("run"),
                                 "sample": item.get("sample"), "input_evidence": item.get("input_evidence"),
                                 "intake_completed_at": item.get("intake_completed_at")}
                                for item in attempts]
        require(len(attempts) <= 36, "prospective budget exceeded")
        declaration, catalogue = read_declaration(path.parent, campaign["declaration"])
        published = publication(path.parent, campaign["publication"], campaign["declaration"])
        output["dispatch_inventory"] = dispatch_inventory(path.parent, campaign["dispatch_inventory"], published, attempts)
        output["declaration"] = campaign["declaration"]
        output["population_scope"] = "declared sequential standard-hosted campaign with observed native assignment and background demand"
        order = [VARIANTS[letter] for block in declaration["blocks"] for letter in block]
        previous_end = previous_intake = published
        run_ids = set()
        for ordinal, attempt in enumerate(attempts, 1):
            output["stop_ordinal"] = ordinal
            require(set(attempt) == {"ordinal", "run", "sample", "input_evidence", "intake_completed_at"} and
                    type(attempt["ordinal"]) is int and attempt["ordinal"] == ordinal, "missing, repeated or reordered dispatch ordinal")
            run_path = qualification.retained(path.parent, attempt["run"])
            run = qualification.phases.read(run_path)
            dispatch = output["dispatches"][ordinal - 1]
            dispatch.update({key: run.get(key) for key in ("id", "run_attempt", "status", "conclusion", "created_at")})
            require(type(run.get("id")) is int and run["id"] > 0 and run["id"] not in run_ids and
                    run["id"] not in declaration["excluded_runs"], "duplicate run or archived old-epoch sample")
            run_ids.add(run["id"])
            require(all(run.get(key) == declaration["cohort"][key] for key in ("head_sha", "workflow_blob_sha")),
                    "run differs from prospective source/workflow")
            variant = order[ordinal - 1]
            require(run.get("head_branch") == qualification.COHORT_BRANCHES[qualification.PROSPECTIVE_COHORT][variant],
                    "dispatch differs from prospective variant order")
            created = github.timestamp(run.get("created_at"))
            require(created is not None and created > published and created > previous_end and created > previous_intake,
                    "dispatch predates publication, prior completion or archive intake")
            timing, end = job_timing(run, variant)
            intake = github.timestamp(attempt.get("intake_completed_at"))
            require(intake is not None and intake >= end, "missing or premature archive intake completion")
            sample_path = qualification.retained(path.parent, attempt["sample"])
            item = qualification.phases.read(sample_path)
            require(item.get("variant") == variant and item["run"]["sha256"] == attempt["run"]["sha256"] and
                    qualification.retained(sample_path.parent, item["run"]).resolve() == run_path.resolve(),
                    "sample manifest does not join the declared dispatch")
            observed = qualification.sample(sample_path.parent, item, qualification.PROSPECTIVE_COHORT)
            require(observed["run_id"] == run["id"] and
                    all(observed[key] == declaration["cohort"][key] for key in ("head_sha", "workflow_blob_sha")),
                    "sample identity differs from declared dispatch/source/workflow")
            output["samples"].append(observed)
            observed["population_census_validated"] = False
            observed["ordinal"] = ordinal
            observed["dispatch_inputs"] = input_evidence(path.parent, attempt["input_evidence"], sample_path.parent,
                                                        item, run, declaration["source_tree"])
            validate_census(observed, catalogue)
            if observations:
                require(json.dumps(observed["conditions"], sort_keys=True) == json.dumps(observations[0]["conditions"], sort_keys=True),
                        "normalized conditions drift during population epoch")
            if variant != "split-overlap":
                timing["windows_checks_seconds"] = timing["job_seconds"]["Windows x86-64 checks"]
            observed["timing"] = timing
            observed["population_census_validated"] = True
            observations.append(observed)
            previous_end, previous_intake = end, intake
        output.pop("stop_ordinal", None)
        output["native_populations"] = population_summary(output["samples"])
        require(len(observations) == 36, "prospective campaign incomplete; no replacement or successful-subset acceptance")
        summary, issues = timing_verdict(observations)
        met = all(issue["timing_status"] == "accepted" for issue in issues.values())
        output.update(timing_status="accepted" if met else "rejected", timing_contract_met=met,
                      status="pending" if met else "rejected", distributions=summary, issues=issues)
    except (OSError, ValueError, KeyError, TypeError, AttributeError, IndexError, StopIteration) as error:
        output["errors"].append(str(error))
        output["native_populations"] = population_summary(output["samples"])
        if "stop_ordinal" in output:
            output["dispatches_after_stop"] = [d["ordinal"] for d in output["dispatches"][output["stop_ordinal"]:]]
    return output


def prepare(path):
    path = Path(path)
    catalogue_path = Path(__file__).parent.parent / "docs" / "ci-checks-native-census-d69.json"
    require(digest(catalogue_path) == CATALOGUE_SHA256, "unreviewed census catalogue")
    catalogue = qualification.phases.read(catalogue_path)
    require(not path.exists(), "declaration already exists; never rewrite a frozen declaration")
    path.parent.mkdir(parents=True, exist_ok=True)
    retained_catalogue = path.parent / catalogue_path.name
    if retained_catalogue.exists():
        require(not retained_catalogue.is_symlink() and digest(retained_catalogue) == CATALOGUE_SHA256,
                "existing retained catalogue differs")
    else:
        with retained_catalogue.open("xb") as stream:
            stream.write(catalogue_path.read_bytes())
    value = {"schema": DECLARATION_SCHEMA, "repository": "buster14a/buster", "experiment": "issue2610-standard-hosted-v1",
             "cohort": {"name": qualification.PROSPECTIVE_COHORT,
                        **{key: catalogue[key] for key in ("head_sha", "workflow_blob_sha")}},
             "source_tree": catalogue["source_tree"], "producer_blobs": catalogue["producer_blobs"],
             "input_producer_blobs": INPUT_PRODUCERS,
             "reader_sha256": reader_bindings(), "catalogue": {"path": retained_catalogue.name, "sha256": CATALOGUE_SHA256},
             "blocks": list(BLOCKS), "seed": SEED, "excluded_runs": EXCLUDED_RUNS,
             "dispatch_inputs": {"cmake_profile": False, "analyzer_comparison": False}}
    with path.open("x", encoding="utf-8") as stream:
        stream.write(json.dumps(value, indent=2) + "\n")
    reference = {"path": path.name, "sha256": digest(path)}
    return {"declaration": reference, "publication_marker": marker(reference["sha256"]),
            "performance_accepted": False, "sampling_authorized": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("campaign", nargs="?", type=Path)
    parser.add_argument("--prepare", type=Path)
    args = parser.parse_args()
    require((args.prepare is not None) != (args.campaign is not None), "select a campaign or --prepare")
    report = prepare(args.prepare) if args.prepare else qualify(args.campaign)
    print(json.dumps(report, indent=2))
    return 1 if report.get("status") == "rejected" else 2 if args.campaign else 0


if __name__ == "__main__":
    sys.exit(main())
