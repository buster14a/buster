#!/usr/bin/env python3
"""Hosted controls for bounded sampling evidence data and whole-job accounting."""
import io
import stat
import unittest
import zipfile
from unittest import mock
import compiler_publish as publisher


def archive(entries):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_DEFLATED) as zipped:
        for name, data, kind in entries:
            info = zipfile.ZipInfo(name)
            info.external_attr = (kind | 0o600) << 16
            zipped.writestr(info, data)
    return stream.getvalue()


class SamplingPublicationControls(unittest.TestCase):
    def test_regular_data_stays_in_memory(self):
        payload = archive([("identity.tsv", b"schema\tv1\n", stat.S_IFREG)])
        with mock.patch.object(zipfile.ZipFile, "extract", side_effect=AssertionError("extraction")), \
                mock.patch.object(zipfile.ZipFile, "extractall", side_effect=AssertionError("extraction")):
            self.assertEqual(publisher.sampling_archive(payload), {"identity.tsv": b"schema\tv1\n"})

    def test_archive_aliases_and_links_are_rejected(self):
        for entries in [
            [("../identity.tsv", b"x", stat.S_IFREG)],
            [("/identity.tsv", b"x", stat.S_IFREG)],
            [("a\\identity.tsv", b"x", stat.S_IFREG)],
            [("./identity.tsv", b"x", stat.S_IFREG)],
            [("identity.tsv", b"x", stat.S_IFLNK)],
            [("identity.tsv", b"x", stat.S_IFIFO)],
            [("identity.tsv", b"x", stat.S_IFREG), ("identity.tsv", b"y", stat.S_IFREG)],
            [("a/", b"", stat.S_IFDIR), ("a", b"x", stat.S_IFREG)],
        ]:
            with self.subTest(entries=entries), self.assertRaises(ValueError):
                publisher.sampling_archive(archive(entries))

    def test_inflated_member_bound_is_checked_before_read(self):
        payload = archive([("pairs.json", b"123456789", stat.S_IFREG)])
        with mock.patch.object(publisher, "ANALYZER_MEMBER_LIMIT", 8), self.assertRaises(ValueError):
            publisher.sampling_archive(payload)

    def test_trailing_data_and_excessive_directory_rejected(self):
        payload = archive([("identity.tsv", b"x", stat.S_IFREG)])
        with self.assertRaises(ValueError):
            publisher.sampling_archive(payload + b"junk")
        payload = archive([(str(i), b"", stat.S_IFREG) for i in range(2049)])
        with self.assertRaises(ValueError):
            publisher.sampling_archive(payload)

    def test_strict_tsv_duplicate_and_missing_cells(self):
        self.assertEqual(publisher.sampling_tsv(b"a\tone\nb\ttwo\n"), {"a": "one", "b": "two"})
        self.assertEqual(publisher.sampling_tsv(b"a\tb\none\ttwo\n", True), [{"a": "one", "b": "two"}])
        for raw in [b"a\t1\na\t2\n", b"a\t\n", b"a\tb\r\n", b"a\tb", b"a\tb\tc\n", b"a\t\x00\n"]:
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                publisher.sampling_tsv(raw)
        with self.assertRaises(ValueError):
            publisher.sampling_tsv(b"a\ta\n1\t2\n", True)

    def test_cleanup_proof_rejects_adopted_or_uncertain_children(self):
        raw = (b"schema\tbuster-native-qualification-supervisor-v1\ncleanup_proven\ttrue\nwall_us\t100\n"
               b"adoption_waves\t1\nadopted_signalled\t0\nadopted_reaped\t0\n")
        self.assertEqual(publisher.sampling_supervision(raw)["wall_us"], "100")
        for changed in [raw.replace(b"cleanup_proven\ttrue", b"cleanup_proven\tfalse"),
                        raw.replace(b"adopted_reaped\t0", b"adopted_reaped\t1"),
                        raw.replace(b"wall_us\t100", b"wall_us\t0"), raw + b"extra\t1\n"]:
            with self.assertRaises(ValueError):
                publisher.sampling_supervision(changed)

    def test_whole_job_not_only_native_measurement_charges_budget(self):
        job = {"name": publisher.SAMPLING_HOST_JOB, "status": "completed", "conclusion": "success",
               "created_at": "2026-10-09T00:00:00Z", "started_at": "2026-10-09T00:01:00Z",
               "completed_at": "2026-10-09T00:02:00Z"}
        observed = publisher.sampling_job_accounting(job, 40000000, 62)
        self.assertEqual(observed["physical_job_wall_us"], 60000000)
        self.assertEqual(observed["physical_job_wall_upper_us"], 62000000)
        self.assertEqual(observed["queue_delay_seconds"], 60)
        for changed, wall, budget in [
            (job, 40000000, 61), (job, 63000000, 100),
            (dict(job, completed_at=None), 1, 100),
            (dict(job, started_at="2026-10-09T00:03:00Z"), 1, 100),
            (dict(job, conclusion="cancelled"), 1, 100),
            (dict(job, started_at="2026-10-09T00:01:00"), 1, 100),
        ]:
            with self.subTest(job=changed), self.assertRaises(ValueError):
                publisher.sampling_job_accounting(changed, wall, budget)


    def authority(self):
        return {"head": "a" * 40, "request_id": "101", "run_id": "202",
                "admitted": {"sampling_freeze_sha256": "b" * 64, "sampling_phase": "pilot", "sampling_packet": "0"}}

    def fake_api(self, authority, completed=False, lost_response=False):
        class FakeApi:
            def __init__(self):
                self.rows = []
                self.posts = self.patches = 0
            def pages(self, path, field):
                return self.rows
            def request(self, path, fields, method=""):
                if path == "/check-runs":
                    self.posts += 1
                    row = dict(fields, id=303, app={"id": 15368})
                    row["details_url"] = "https://github.com/buster14a/buster/runs/303"
                    self.rows.append(row)
                    if lost_response and self.posts == 1:
                        raise publisher.urllib.error.URLError("response lost")
                    return row
                self.patches += 1
                self.rows[0].update(fields)
                return self.rows[0]
        api = FakeApi()
        if completed:
            api.rows.append({"id": 303, "name": publisher.SAMPLING_CHECK_NAME, "head_sha": authority["head"],
                             "external_id": publisher.sampling_check_marker(authority), "app": {"id": 15368},
                             "status": "completed", "conclusion": "failure"})
        return api

    def test_sampling_check_is_attempt_bound_and_terminal_immutable(self):
        authority = self.authority()
        api = self.fake_api(authority, completed=True)
        row = publisher.sampling_write(api, authority, {"status": "queued"})
        self.assertEqual(row["conclusion"], "failure")
        self.assertEqual((api.posts, api.patches), (0, 0))
        api.rows[0]["external_id"] = api.rows[0]["external_id"].replace(":202:1", ":999:1")
        self.assertEqual(publisher.sampling_checks(api, authority), [])

    def test_lost_check_post_response_is_resolved_without_duplicate(self):
        authority = self.authority()
        api = self.fake_api(authority, lost_response=True)
        row = publisher.sampling_write(api, authority, {"status": "queued"})
        self.assertEqual((api.posts, api.patches, row["id"]), (1, 0, 303))
        self.assertEqual(row["details_url"], "https://github.com/buster14a/buster/runs/303")

    def test_duplicate_owned_checks_are_rejected(self):
        authority = self.authority()
        api = self.fake_api(authority, completed=True)
        api.rows.append(dict(api.rows[0], id=304))
        with self.assertRaises(ValueError):
            publisher.sampling_checks(api, authority)

    def test_unbound_sampling_workflow_is_rejected_before_api_access(self):
        with mock.patch.object(publisher, "Api", side_effect=AssertionError("must not access API")):
            with self.assertRaises(ValueError):
                publisher.sampling_authority({"BQ_REPOSITORY": "buster14a/buster"})


    def test_in_progress_check_never_moves_back_to_queued(self):
        authority = self.authority()
        api = self.fake_api(authority, completed=True)
        api.rows[0].update(status="in_progress")
        row = publisher.sampling_write(api, authority, {"status": "queued"})
        self.assertEqual(row["status"], "in_progress")
        self.assertEqual((api.posts, api.patches), (0, 0))

    def test_ambiguous_lost_post_never_retries_creation(self):
        authority = self.authority()
        api = self.fake_api(authority, lost_response=True)
        api.pages = lambda path, field: []
        with self.assertRaises(ValueError):
            publisher.sampling_write(api, authority, {"status": "queued"})
        self.assertEqual(api.posts, 1)

    def test_successful_post_is_verified_without_visibility_retry(self):
        authority = self.authority()
        api = self.fake_api(authority)
        row = publisher.sampling_write(api, authority, {"status": "queued"})
        self.assertEqual((api.posts, api.patches, row["id"]), (1, 0, 303))


# These are synthetic hosted data controls. They run the real prepared adapter,
# corpus classifier and raw paired statistics; they never launch a benchmark.
import base64
import copy
import hashlib
import json
import os
import sys
from pathlib import Path
from datetime import datetime, timedelta, timezone

import compiler_preparation as preparation
import compiler_preparation_test as preparation_fixture
import compiler_test
import sampling_qualification_receipt as sampling
import sampling_qualification_test as packet_fixture


def json_bytes(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def tsv_bytes(value):
    return "".join(str(key) + "\t" + str(item) + "\n" for key, item in value.items()).encode()


def table_bytes(rows, header=None):
    header = header or list(rows[0])
    return ("\t".join(header) + "\n" + "".join("\t".join(str(row[key]) for key in header) + "\n" for row in rows)).encode()


def complete_supervision():
    return tsv_bytes({"schema": "buster-native-qualification-supervisor-v1", "cleanup_proven": "true",
                      "wall_us": "1000", "adoption_waves": "1", "adopted_signalled": "0", "adopted_reaped": "0"})


def physical_files(authority, context, prepared, packet_wall, prep_us):
    acquired = context["record"]
    owner_wall = packet_wall + 1000000
    files = {"prepared/" + key: raw for key, raw in prepared["files"].items()}
    files["host.json"] = json_bytes({"schema": "buster-main-sampling-host-v1", "state": "complete",
        "cpu_model": "AMD Ryzen 7 9700X 8-Core Processor", "logical_processor_records": 16, "observed_from": "/proc/cpuinfo",
        "request_head": authority["head"], "run_id": authority["run_id"], "run_attempt": "1",
        "request_run_id": authority["request_id"], "measurement_trusted_revision": acquired["trusted_revision"],
        "policy_trusted_revision": authority["executor"]["head_sha"],
        "freeze_sha256": authority["admitted"]["sampling_freeze_sha256"], "acquisition_campaign": context["sha256"],
        "protocol_sha256": acquired["protocol_sha256"]})
    files["owner.tsv"] = tsv_bytes({"schema": "buster-main-sampling-owner-v1", "physical_packet_wall_us": str(owner_wall),
        "process_state": "complete", "timed_out": "0", "cleanup_failed": "0", "within_reservation": "true", "cancelled": "0"})
    files["packet.tsv"] = tsv_bytes({"physical_packet_wall_us": str(packet_wall), "prep_us": str(prep_us),
        "captured_input_files_unchanged": "true", "within_reservation": "true", "process_state": "complete",
        "qualification_state": "unvalidated", "queue_delay": "unavailable"})
    files["owner-supervision.tsv"] = complete_supervision()
    names = ["trusted-harness-pin"]
    if context["phase"] == "acquire":
        names += ["clone-sources", "fetch-pinned-arms", "baseline-tree", "ab1-tree", "ab2-tree",
                  "primary-arm-checkout", "acquire-prepared-closure"]
    else:
        names += ["full-default-corpus"]
    phases = []
    for index, name in enumerate(names, 1):
        phases.append({"stage": str(index), "phase": name, "wall_us": "1000", "exit_status": "0", "timed_out": "0",
                       "cleanup_failed": "0", "cancelled": "0", "state": "complete"})
        stem = f"controller-{index}-{name}"
        files[stem + "-supervision.tsv"] = complete_supervision()
        files[stem + ".stdout.log"] = files[stem + ".stderr.log"] = b""
    files["controller.tsv"] = table_bytes(phases)
    return files


def publication_fixture(phase="pilot", packet=0):
    expected, actual_prepared, producer = preparation_fixture.fixture(False, 3)
    expected = dict(expected)
    lab_bytes, protocol_bytes = Path(sampling._lab.__file__).read_bytes(), b"trusted synthetic frozen protocol\n"
    plan = {"schema": "buster-main-sampling-acquisition-v1", "phase": "acquire", "base": expected["base"],
        "base_tree": expected["base_tree"], "request_head": "8" * 40, "trusted_revision": "9" * 40,
        "baseline_revision": expected["base"], "ab1_revision": expected["head"], "ab2_revision": expected["secondary_head"],
        "protocol_sha256": hashlib.sha256(protocol_bytes).hexdigest(), "source_root": expected["root"],
        "store_root": "/persistent", "closure_policy": "snapshot-v1", "toolchain_policy": "clang-release-tests-off-native-v1",
        "measurement": "false", "physical_budget_seconds": "1800"}
    plan_bytes = tsv_bytes(plan)
    plan_sha = hashlib.sha256(plan_bytes).hexdigest()
    output = plan["store_root"] + "/" + plan_sha + "/prepared"
    expected["output"] = output
    # Rebuild only command data around the deterministic committed store path.
    # Actual source, compiler records and raw inventory come from the producer fixture.
    for ordinal, name in enumerate(preparation_fixture.native_phases("snapshot", False, 3), 1):
        command = preparation.child_commands("snapshot", expected, producer["prepared"], False).get(name)
        if command is not None:
            producer["files"][f"{ordinal}-{name}.argv"] = b"".join(
                str(len(item.encode())).encode() + b":" + item.encode() + b"\n" for item in command)
    producer["files"]["closure.manifest.tsv"] = producer["manifest"]
    prepared_sha = hashlib.sha256(producer["files"]["prepared.json"]).hexdigest()
    freeze = {"schema": "buster-main-sampling-freeze-v1", "phase": phase,
        "campaign_parent": plan_sha if phase != "confirm" else "2" * 64,
        "campaign_parent_revision": "1" * 40 if phase != "confirm" else "2" * 40,
        "base": plan["base"], "base_tree": plan["base_tree"], "request_head": plan["request_head"],
        "trusted_revision": plan["trusted_revision"], "baseline_revision": plan["base"], "aa_candidate_revision": plan["base"],
        "ab1_revision": plan["ab1_revision"], "ab2_revision": plan["ab2_revision"],
        "protocol_sha256": plan["protocol_sha256"], "lab_sha256": hashlib.sha256(lab_bytes).hexdigest(),
        "python_sha256": "4" * 64, "driver_sha256": "5" * 64, "prepared_sha256": prepared_sha,
        "closure_sha256": producer["prepared"]["snapshot_digest"],
        "baseline_sha256": producer["prepared"]["baseline_sha256"], "aa_candidate_sha256": producer["prepared"]["baseline_sha256"],
        "ab1_candidate_sha256": producer["prepared"]["candidate_sha256"], "ab2_candidate_sha256": producer["prepared"]["candidate2_sha256"],
        "baseline_bytes": str(producer["prepared"]["baseline_bytes"]),
        "ab1_candidate_bytes": str(producer["prepared"]["candidate_bytes"]), "ab2_candidate_bytes": str(producer["prepared"]["candidate2_bytes"])}
    family = sampling.schedule(phase, packet)["family"]
    campaign = plan_sha if phase == "acquire" else "e" * 64
    authority = {"repository": "buster14a/buster", "head": "7" * 40, "request_id": "200", "run_id": "201",
        "admitted": {"sampling_phase": phase, "sampling_packet": str(packet), "sampling_family": family,
            "sampling_freeze_sha256": campaign, "sampling_freeze_revision": "1" * 40 if phase == "acquire" else "e" * 40,
            "sampling_trusted_revision": plan["trusted_revision"], "sampling_protocol_sha256": plan["protocol_sha256"],
            "sampling_reservation_seconds": str(sampling.schedule(phase, packet)["reservation_seconds"])},
        "acquisition_plan": plan, "acquisition_plan_bytes": plan_bytes, "freeze": plan if phase == "acquire" else freeze,
        "parent_freeze": {} if phase == "acquire" else plan if phase == "pilot" else
            dict(freeze, phase="pilot", campaign_parent=plan_sha, campaign_parent_revision="1" * 40),
        "facts": {"actor_login": "davidgmbb", "owner_login": "davidgmbb"},
        "history": [], "executor": {"head_sha": "6" * 40}, "request": {}}
    context = publisher.sampling_plan(authority)
    prepared_context = {"record": producer["prepared"], "files": producer["files"], "bytes": producer["files"]["prepared.json"],
        "sha256": prepared_sha, "expected": expected, "bundle": producer}
    old = dict(authority, head="0" * 40, request_id="100", run_id="101", executor={"head_sha": "3" * 40},
        admitted=dict(authority["admitted"], sampling_phase="acquire", sampling_packet="0", sampling_family="acquire",
                      sampling_freeze_sha256=plan_sha, sampling_freeze_revision="1" * 40, sampling_reservation_seconds="1800"))
    old_context = dict(context, phase="acquire", packet=0, schedule=sampling.schedule("acquire", 0))
    acquired_files = physical_files(old, old_context, prepared_context, 10000000, 9000000)
    acquired = {"schema": "buster-main-sampling-acquisition-receipt-v1", "phase": "acquire", "packet": "0",
        "measurement": "false", "campaign": plan_sha, "base": plan["base"], "base_tree": plan["base_tree"],
        "request_head": plan["request_head"], "trusted_revision": plan["trusted_revision"],
        "ab1_revision": plan["ab1_revision"], "ab2_revision": plan["ab2_revision"], "protocol_sha256": plan["protocol_sha256"],
        "lab_sha256": freeze["lab_sha256"], "python_sha256": freeze["python_sha256"], "driver_sha256": freeze["driver_sha256"],
        "python_path": "/usr/bin/python3.12",
        "prepared_sha256": prepared_sha, "reservation_seconds": "1800", "process_state": "complete", "qualification_state": "unvalidated"}
    acquired_files["acquisition.tsv"] = tsv_bytes(acquired)
    old_records = [(old, acquired_files)]
    if phase == "acquire":
        files = physical_files(authority, context, prepared_context, 10000000, 9000000)
        files["acquisition.tsv"] = tsv_bytes(acquired)
    else:
        identity, attempts, unused_terminal, unused_series, unused_trusted = packet_fixture.fixture(phase, packet)
        role = "baseline" if family == "aa" else "candidate" if family == "ab1" else "candidate2"
        candidate_revision = plan["base"] if family == "aa" else plan["ab1_revision"] if family == "ab1" else plan["ab2_revision"]
        identity.update(campaign=campaign, freeze_sha256=campaign, base=plan["base"], base_tree=plan["base_tree"],
            request_head=authority["head"], trusted_revision=plan["trusted_revision"], baseline_revision=plan["base"],
            candidate_revision=candidate_revision, baseline_sha256=producer["prepared"]["baseline_sha256"],
            candidate_sha256=producer["prepared"][role + "_sha256"], lab_sha256=acquired["lab_sha256"],
            protocol_sha256=acquired["protocol_sha256"], python_sha256=acquired["python_sha256"], driver_sha256=acquired["driver_sha256"],
            closure_sha256=producer["prepared"]["snapshot_digest"], prepared_sha256=prepared_sha,
            baseline_bytes=str(producer["prepared"]["baseline_bytes"]), candidate_bytes=str(producer["prepared"][role + "_bytes"]))
        binaries = {name: {"sha256": identity[name + "_sha256"], "revision": identity[name + "_revision"],
                           "size_bytes": int(identity[name + "_bytes"])} for name in ("baseline", "candidate")}
        paths = {"baseline": output + "/bin/ide-base", "candidate": output + "/bin/" +
                 {"baseline": "ide-base", "candidate": "ide-cand", "candidate2": "ide-cand2"}[role]}
        workload = {"command": preparation.WORKLOAD_COMMAND, "repo_root": plan["source_root"],
                    "perf": "perf", "extra": [], "extra_by_variant": {"a": [], "b": []}}
        wall = sum(int(row["wall_us"]) for row in attempts) + 2010000
        files = physical_files(authority, context, prepared_context, wall, 2000000)
        for index, (row, slot) in enumerate(zip(attempts, context["schedule"]["slots"])):
            row.update(cpu_status="1", memory_status="1", user_cpu_us="1000", system_cpu_us="100",
                       peak_rss_bytes="10000", closure_before=identity["closure_sha256"], closure_after=identity["closure_sha256"])
            bundle = packet_fixture.bundle(slot[0], slot[2], binaries, workload, 1.0 if family == "aa" else 1.021)
            bundle["summary"]["host"]["git_revision"] = plan["base"]
            for key, name in (("a", "baseline"), ("b", "candidate")):
                bundle["compare"]["variants"][key]["ide"] = paths[name]
            for name, key in (("compare.json", "compare"), ("pairs.json", "pairs"), ("summary.json", "summary")):
                files[f"trial-{index}/" + name] = json_bytes(bundle[key])
            files[f"trial-{index}-supervision.tsv"] = complete_supervision()
            files[f"trial-{index}.stdout.log"] = files[f"trial-{index}.stderr.log"] = b""
        files["identity.tsv"], files["attempts.tsv"] = tsv_bytes(identity), table_bytes(attempts)
        for index in [99, *range(len(attempts))]:
            for side in ("before", "after"):
                stem = f"closure-{index}-{side}"
                files[stem + ".json"] = json_bytes(producer["closure"]["verify"])
                files[stem + ".json.manifest.tsv"] = producer["manifest"]
                files[stem + "-supervision.tsv"] = complete_supervision()
        corpus = compiler_test.corpus()
        corpus["metadata"].update(flags=[], allocation_compilers=[], scale=1, seed=20260907)
        for provenance, name in zip(corpus["metadata"]["compiler_provenance"], ("baseline", "candidate")):
            provenance.update(sha256=binaries[name]["sha256"], bytes=binaries[name]["size_bytes"],
                              path=paths[name], revision_label=binaries[name]["revision"])
        files["throughput/metadata.json"], files["throughput/summary.json"] = json_bytes(corpus["metadata"]), json_bytes(corpus["summary"])
        preceding = [("acquire", 0, old)]
        for previous_phase, total in (("pilot", packet if phase == "pilot" else 3),
                                      ("confirm", packet if phase == "confirm" else 0)):
            for previous_packet in range(total):
                number = str(110 + len(preceding) * 2)
                prior = dict(authority, request_id=number, run_id=str(int(number) + 1), head=("%040x" % int(number)),
                             executor={"head_sha": "3" * 40})
                preceding.append((previous_phase, previous_packet, prior))
                old_records.append((prior, {}))
        for previous_phase, previous_packet, prior in preceding:
            history_campaign = plan_sha if previous_phase == "acquire" else \
                "2" * 64 if previous_phase == "pilot" and phase == "confirm" else campaign
            history_revision = "1" * 40 if previous_phase == "acquire" else \
                "2" * 40 if previous_phase == "pilot" and phase == "confirm" else "e" * 40
            authority["history"].append({"phase": previous_phase, "packet": str(previous_packet),
                "request_run_id": prior["request_id"], "request_run_attempt": "1",
                "executor_run_id": prior["run_id"], "executor_run_attempt": "1", "state": "complete",
                "physical_wall_us": "122000000", "campaign": history_campaign, "freeze_revision": history_revision,
                "actor_login": "davidgmbb", "actor_id": "39247043", "triggering_login": "davidgmbb",
                "triggering_id": "39247043", "pull_author_login": "davidgmbb", "pull_author_id": "39247043"})
    class FixtureApi:
        token = "synthetic"
        prefix = "https://api.github.com/repos/buster14a/buster"
        def __init__(self):
            self.runs, self.jobs, self.payloads = {}, {}, {}
            self.contents = {"tools/uarch_lab.py": lab_bytes, "docs/compiler-main-sampling-qualification-v1.json": protocol_bytes}
            for item, raw_files in [*old_records, (authority, files)]:
                request_id, run_id = item["request_id"], item["run_id"]
                self.runs[request_id] = {"id": int(request_id), "run_attempt": 1, "head_sha": item["head"]}
                self.runs[run_id] = {"id": int(run_id), "run_attempt": 1, "head_sha": item["executor"]["head_sha"],
                    "path": publisher.BENCH_WORKFLOW, "event": "workflow_run", "head_branch": "main",
                    "repository": {"full_name": "buster14a/buster"}, "status": "completed", "conclusion": "success",
                    "display_title": f"9700X request {request_id}.1 head {item['head']}"}
                duration = 120
                if run_id == authority["run_id"]:
                    owner = publisher.sampling_tsv(raw_files["owner.tsv"])
                    duration = (int(owner["physical_packet_wall_us"]) + 999999) // 1000000 + 10
                stamp = datetime(2026, 10, 9, tzinfo=timezone.utc)
                self.jobs[run_id] = {"id": int(run_id) + 1000, "name": publisher.SAMPLING_HOST_JOB,
                    "run_id": int(run_id), "run_attempt": 1, "status": "completed", "conclusion": "success",
                    "runner_id": 123, "runner_name": "fixture-9700x", "labels": ["self-hosted", "9700x"],
                    "created_at": stamp.isoformat(), "started_at": (stamp + timedelta(seconds=60)).isoformat(),
                    "completed_at": (stamp + timedelta(seconds=60 + duration)).isoformat()}
                if raw_files:
                    self.payloads[run_id] = archive([(name, value, stat.S_IFREG) for name, value in raw_files.items()])
        def request(self, path):
            if path.startswith("/git/commits/"):
                revision = path.rsplit("/", 1)[1]
                tree = {expected["base"]: expected["base_tree"], expected["head"]: expected["head_tree"],
                        expected["secondary_head"]: expected["secondary_tree"]}[revision]
                return {"sha": revision, "tree": {"sha": tree}}
            if path.startswith("/contents/"):
                name = path[len("/contents/"):].split("?", 1)[0]
                raw = self.contents[name]
                return {"type": "file", "encoding": "base64", "size": len(raw), "content": base64.b64encode(raw).decode()}
            if "/artifacts?" in path:
                run_id = path.split("/")[3]
                run = self.runs[run_id]
                request = self.runs[run["display_title"].split()[2].split(".")[0]]
                return {"artifacts": [{"id": int(run_id), "name": publisher.SAMPLING_ARTIFACT_PREFIX + request["head_sha"] + "-1",
                    "expired": False, "size_in_bytes": len(self.payloads[run_id]),
                    "workflow_run": {"id": int(run_id), "head_sha": run["head_sha"]}}]}
            if path.startswith("/actions/runs/"):
                return self.runs[path.rsplit("/", 1)[1]]
            raise AssertionError("unexpected API " + path)
        def pages(self, path, field):
            self_field = path.split("/")[3]
            return [self.jobs[self_field]]
        def download(self, url):
            return self.payloads[url.rsplit("/", 2)[1]]
    return FixtureApi(), authority, files


class SamplingPublicationOutcomes(unittest.TestCase):
    def test_real_preparation_and_pair_replay_all_phase_families(self):
        for phase, packet in (("acquire", 0), ("pilot", 0), ("pilot", 1), ("pilot", 2),
                              ("confirm", 0), ("confirm", 1), ("confirm", 2), ("confirm", 3)):
            with self.subTest(phase=phase, packet=packet):
                api, authority, files = publication_fixture(phase, packet)
                result = publisher.sampling_validate(api, authority, files)
                self.assertEqual(result["packet_state"], "complete-valid-research", result.get("problems"))
                self.assertEqual(result["qualification_state"], "unqualified")
                self.assertIs(result["routine_profile_enabled"], False)
                self.assertGreater(result["accounting"]["physical_job_wall_upper_us"], result["physical_packet_wall_us"])
                self.assertEqual(len(result["acquired_binaries"]), 3)
                self.assertEqual(result["acquired_runtime"]["python_path"], "/usr/bin/python3.12")
                self.assertEqual(result["acquired_runtime"]["python_sha256"], "4" * 64)

    def test_duplicate_json_and_nonfinite_values_are_rejected(self):
        for raw in (b'{"a":1,"a":2}', b'{"a":NaN}', b'{"a":Infinity}', b'{"a":1e999}', b'[]', b''):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                publisher.sampling_json({"host.json": raw}, "host.json")

    def test_producer_and_frozen_artifact_tampering_fail(self):
        for name, replacement in (("prepared/prepared.json", b"{}\n"), ("prepared/prepared.manifest.tsv", b"changed\n"),
                                  ("prepared/candidate2.binary.json", b"{}\n"), ("prepared/7-baseline-generate.argv", b"changed\n")):
            with self.subTest(name=name):
                api, authority, files = publication_fixture()
                files[name] = replacement
                with self.assertRaises(ValueError):
                    publisher.sampling_validate(api, authority, files)
        for key in ("prepared_sha256", "baseline_bytes", "ab1_candidate_bytes", "ab2_candidate_bytes", "driver_sha256"):
            with self.subTest(key=key):
                api, authority, files = publication_fixture()
                authority["freeze"][key] = "0" if key.endswith("bytes") else "0" * 64
                with self.assertRaises(ValueError):
                    publisher.sampling_validate(api, authority, files)

    def test_cpu_owner_supervision_and_phase_controls_fail_closed(self):
        for name in ("host.json", "owner.tsv", "owner-supervision.tsv", "controller.tsv",
                     "closure-99-before.json", "closure-0-after.json.manifest.tsv", "trial-0-supervision.tsv"):
            with self.subTest(name=name):
                api, authority, files = publication_fixture()
                files.pop(name)
                with self.assertRaises(ValueError):
                    publisher.sampling_validate(api, authority, files)
        for key, replacement in (("request_head", "8" * 40), ("cpu_model", "other CPU"),
                                 ("policy_trusted_revision", "9" * 40), ("logical_processor_records", 0)):
            api, authority, files = publication_fixture()
            host = publisher.sampling_json(files, "host.json")
            host[key] = replacement
            files["host.json"] = json_bytes(host)
            with self.subTest(key=key), self.assertRaises(ValueError):
                publisher.sampling_validate(api, authority, files)

    def test_original_acquisition_is_independently_replayed(self):
        api, authority, files = publication_fixture()
        # Current packet's preparation stays untouched; the authenticated prior
        # acquisition has a corrupted producer inventory.
        prior = publisher.sampling_archive(api.payloads["101"])
        prior["prepared/prepared.workload.tsv"] = b"changed\n"
        api.payloads["101"] = archive([(name, raw, stat.S_IFREG) for name, raw in prior.items()])
        with self.assertRaises(ValueError):
            publisher.sampling_validate(api, authority, files)

    def test_raw_configuration_and_own_samples_determine_status(self):
        for member, mutation in (("compare.json", lambda value: value["config"].update(pairs=39)),
                                 ("pairs.json", lambda value: value.pop()),
                                 ("summary.json", lambda value: value["plan"].update(bootstrap_resamples=1999))):
            api, authority, files = publication_fixture()
            path = "trial-1/" + member
            value = publisher.sampling_json(files, path, member != "pairs.json")
            mutation(value)
            files[path] = json_bytes(value)
            result = publisher.sampling_validate(api, authority, files)
            with self.subTest(member=member):
                self.assertEqual(result["packet_state"], "incomplete")
                self.assertTrue(result["problems"])
                self.assertEqual(result["authenticated_attempt_history"][-1]["state"], "invalid")
                self.assertEqual(result["qualification_state"], "unqualified")

    def test_full_fast_quality_corpus_and_paths_are_required(self):
        api, authority, files = publication_fixture()
        summary = publisher.sampling_json(files, "throughput/summary.json")
        summary["comparisons"].pop()
        files["throughput/summary.json"] = json_bytes(summary)
        result = publisher.sampling_validate(api, authority, files)
        self.assertEqual(result["packet_state"], "incomplete")
        api, authority, files = publication_fixture()
        metadata = publisher.sampling_json(files, "throughput/metadata.json")
        metadata["compiler_provenance"][0]["path"] = "/different/compiler"
        files["throughput/metadata.json"] = json_bytes(metadata)
        self.assertEqual(publisher.sampling_validate(api, authority, files)["packet_state"], "incomplete")

    def test_failure_unavailable_costs_are_not_zero(self):
        api, authority, unused_files = publication_fixture()
        api.jobs["201"].update(status="cancelled", conclusion="cancelled", completed_at=None)
        result = publisher.sampling_observed_costs(api, authority, {})
        self.assertIsNone(result["physical_packet_wall_us"])
        self.assertIsNone(result["prep_us"])
        self.assertIsNone(result["physical_job_wall_upper_us"])
        self.assertEqual(result["platform_job_conclusion"], "cancelled")

    def test_missing_artifact_completes_owned_failure_and_keeps_charge(self):
        api, authority, unused_files = publication_fixture()
        with mock.patch.object(publisher, "sampling_authority", return_value=(api, authority)), \
                mock.patch.object(publisher, "sampling_read_artifact", side_effect=ValueError("missing raw artifact")), \
                mock.patch.object(publisher, "sampling_write", side_effect=lambda api, owner, body: dict(body, id=303)) as write:
            self.assertEqual(publisher.sampling_publish({}), 1)
        body = write.call_args[0][2]
        self.assertEqual((body["status"], body["conclusion"]), ("completed", "failure"))
        self.assertEqual(body["output"]["title"], "Incomplete unqualified sampling packet")
        for line in ("Lifecycle protocol: sampling-terminal-native-v1.",
                     "Request run 200 attempt 1: https://github.com/buster14a/buster/actions/runs/200/attempts/1",
                     "Workflow run 201 attempt 1: https://github.com/buster14a/buster/actions/runs/201/attempts/1"):
            self.assertEqual(body["output"]["summary"].count(line), 1)
        result = json.loads(body["output"]["text"].split("\n", 1)[1].rsplit("\n", 1)[0])
        self.assertEqual(result["authenticated_attempt_history"][-1]["state"], "incomplete")
        self.assertGreater(result["authenticated_attempt_history"][-1]["actions_job_occupancy_us"], 0)

    def test_complete_research_publishes_success_without_qualification(self):
        api, authority, files = publication_fixture()
        with mock.patch.object(publisher, "sampling_authority", return_value=(api, authority)), \
                mock.patch.object(publisher, "sampling_read_artifact", side_effect=lambda api, owner: (files, {}) if owner["run_id"] == "201" else
                                  (publisher.sampling_archive(api.payloads[owner["run_id"]]), {})), \
                mock.patch.object(publisher, "sampling_write", side_effect=lambda api, owner, body: dict(body, id=303)) as write:
            self.assertEqual(publisher.sampling_publish({}), 0)
        body = write.call_args[0][2]
        self.assertEqual(body["conclusion"], "success")
        self.assertEqual(body["output"]["title"], "Valid unqualified sampling packet")
        result = json.loads(body["output"]["text"].split("\n", 1)[1].rsplit("\n", 1)[0])
        self.assertEqual(result["qualification_state"], "unqualified")
        self.assertIs(result["routine_profile_enabled"], False)

    def test_native_first_attempt_history_and_whole_job_budget_bind(self):
        for field, value in (("state", "cancelled"), ("executor_run_attempt", "2"), ("physical_wall_us", "1")):
            api, authority, files = publication_fixture()
            authority["history"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                publisher.sampling_validate(api, authority, files)
        api, authority, files = publication_fixture()
        api.jobs["201"]["completed_at"] = "2026-10-09T01:00:00+00:00"
        with self.assertRaises(ValueError):
            publisher.sampling_validate(api, authority, files)

    def test_queue_rejects_already_terminal_assignment(self):
        api, authority, unused_files = publication_fixture()
        with mock.patch.object(publisher, "sampling_authority", return_value=(api, authority)), \
                mock.patch.object(publisher, "sampling_write", return_value={"id": 303, "status": "completed", "conclusion": "failure"}):
            with self.assertRaises(ValueError):
                publisher.sampling_queue({})

    def test_unknown_cli_cannot_fall_through_to_ordinary_publication(self):
        with mock.patch.object(publisher.sys, "argv", ["compiler_publish.py", "sampling-publish", "extra"]), \
                mock.patch.object(publisher, "Api", side_effect=AssertionError("unsupported args reached API")):
            self.assertEqual(publisher.main(), 1)



    def test_corrupt_zip_failure_is_terminal_and_cannot_escape_reader(self):
        import zlib
        raw = archive([("host.json", b"{}", stat.S_IFREG)])
        for exception in (zipfile.BadZipFile("invalid central directory"), zlib.error("corrupt stream")):
            with self.subTest(exception=exception):
                api, authority, unused_files = publication_fixture()
                with mock.patch.object(zipfile.ZipFile, "read", side_effect=exception), \
                        mock.patch.object(publisher, "sampling_authority", return_value=(api, authority)), \
                        mock.patch.object(publisher, "sampling_read_artifact", side_effect=lambda api, owner: (publisher.sampling_archive(raw), {})), \
                        mock.patch.object(publisher, "sampling_write", side_effect=lambda api, owner, body: dict(body, id=303)) as write:
                    self.assertEqual(publisher.sampling_publish({}), 1)
                self.assertEqual(write.call_args[0][2]["conclusion"], "failure")

    def test_current_statistics_checkout_must_match_acquired_lab(self):
        api, authority, files = publication_fixture("acquire", 0)
        row = publisher.sampling_tsv(files["acquisition.tsv"])
        row["lab_sha256"] = hashlib.sha256(b"different lab\n").hexdigest()
        files["acquisition.tsv"] = tsv_bytes(row)
        api.contents["tools/uarch_lab.py"] = b"different lab\n"
        with self.assertRaisesRegex(ValueError, "statistical replay module"):
            publisher.sampling_validate(api, authority, files)

    def test_whole_native_owner_cost_is_separate_from_packet_cost(self):
        api, authority, files = publication_fixture()
        result = publisher.sampling_validate(api, authority, files)
        self.assertGreater(result["accounting"]["native_owner_wall_us"], result["accounting"]["native_packet_wall_us"])
        self.assertIsNotNone(result["acquisition_preparation_costs"])
        self.assertTrue(result["acquisition_preparation_costs"]["complete_cost_available"])

    def test_complete_cost_is_required_and_supervision_has_real_clock_bound(self):
        api, authority, files = publication_fixture()
        files.pop("prepared/preparation-cost.json")
        with self.assertRaises(ValueError):
            publisher.sampling_validate(api, authority, files)
        for name in ("owner-supervision.tsv", "controller-1-trusted-harness-pin-supervision.tsv", "trial-0-supervision.tsv"):
            api, authority, files = publication_fixture()
            proof = publisher.sampling_tsv(files[name])
            proof["wall_us"] = "99999999999"
            files[name] = tsv_bytes(proof)
            with self.subTest(name=name), self.assertRaises(ValueError):
                publisher.sampling_validate(api, authority, files)



    def test_cancelled_executor_cannot_be_replaced_by_successful_host_job(self):
        for state in ("cancelled", "failure"):
            api, authority, files = publication_fixture()
            api.runs["101"]["conclusion"] = state
            # Its host job and retained prior artifact stay complete; the
            # attempted workflow itself must still stop this campaign.
            with self.subTest(state=state), self.assertRaises(ValueError):
                publisher.sampling_validate(api, authority, files)



    def test_acquisition_runtime_path_is_required_and_canonical(self):
        for path in (None, "", "python3", "/", "/usr/bin/../python3", "/usr//bin/python3",
                     "/usr/bin/python name", "/usr/bin/python\\name", "/" + "a" * 256):
            api, authority, files = publication_fixture("acquire", 0)
            row = publisher.sampling_tsv(files["acquisition.tsv"])
            if path is None:
                row.pop("python_path")
            else:
                row["python_path"] = path
            files["acquisition.tsv"] = tsv_bytes(row)
            with self.subTest(path=path), self.assertRaises(ValueError):
                publisher.sampling_validate(api, authority, files)



class SamplingNativeExportReplay(unittest.TestCase):
    def test_actual_native_export_zip_replays_sampling_preparation_reader(self):
        directory_label = os.environ.get("BUSTER_SAMPLING_NATIVE_EXPORT")
        if not directory_label:
            self.skipTest("run --native-export DIR after the hosted native producer fixture")
        directory = Path(directory_label)
        paths = list(directory.iterdir())
        self.assertLessEqual(len(paths), preparation.FILE_COUNT_LIMIT)
        immediate = {}
        for path in paths:
            self.assertFalse(path.is_dir(), path.name)
            self.assertFalse(path.is_symlink(), path.name)
            self.assertTrue(path.is_file(), path.name)
            self.assertLessEqual(path.stat().st_size, preparation.MEMBER_LIMIT)
            immediate[path.name] = path.read_bytes()
        self.assertLessEqual(sum(map(len, immediate.values())), preparation.TOTAL_LIMIT)
        # This expected file is generated by the trusted native hosted fixture,
        # never used as production GitHub/acquisition authority.
        expected = json.loads(immediate["fixture-plan.json"])
        immediate["closure.manifest.tsv"] = immediate["prepared.manifest.tsv"]
        payload = archive([("prepared/" + name, raw, stat.S_IFREG) for name, raw in immediate.items()])
        files = publisher.sampling_archive(payload)
        plan = {"base": expected["base"], "base_tree": expected["base_tree"], "ab1_revision": expected["head"],
                "ab2_revision": expected["secondary_head"], "source_root": expected["root"]}
        context = {"record": plan, "output": expected["output"]}
        class ImmutableFixtureApi:
            def request(self, path):
                revision = path.rsplit("/", 1)[1]
                tree = {expected["base"]: expected["base_tree"], expected["head"]: expected["head_tree"],
                        expected["secondary_head"]: expected["secondary_tree"]}[revision]
                return {"sha": revision, "tree": {"sha": tree}}
        result = publisher.sampling_prepared(ImmutableFixtureApi(), {}, files, context)
        self.assertEqual(result["bytes"], immediate["prepared.json"])
        self.assertEqual(result["sha256"], hashlib.sha256(immediate["prepared.json"]).hexdigest())
        self.assertEqual(result["record"]["arm_count"], 3)
        for role in ("baseline", "candidate", "candidate2"):
            binary = json.loads(immediate[role + ".binary.json"])
            self.assertEqual((binary["sha256"], binary["bytes"]),
                             (result["record"][role + "_sha256"], result["record"][role + "_bytes"]))
            self.assertGreater(binary["bytes"], 0)
        cost = publisher.sampling_json(files, "prepared/preparation-cost.json")
        self.assertTrue(cost["complete_cost_available"])
        self.assertEqual(cost["prepared_receipt_sha256"], result["sha256"])
        changed = dict(files)
        changed["prepared/preparation-cost.json"] = json_bytes(dict(cost, total_us=cost["total_us"] + 1))
        with self.assertRaises(ValueError):
            publisher.sampling_prepared(ImmutableFixtureApi(), {}, changed, context)



if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--native-export":
        os.environ["BUSTER_SAMPLING_NATIVE_EXPORT"] = sys.argv[2]
        unittest.main(argv=[sys.argv[0]], defaultTest="SamplingNativeExportReplay")
    else:
        unittest.main()
