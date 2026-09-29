#!/usr/bin/env python3
"""Standing authorization for the existing retirement writer (#1791).

Ownership: protected-main policy and code, never candidate code. The controller
publishes one immutable request artifact before dispatch. authorize_request
checks its GitHub run provenance, exact request, live policy and kill switch.
The ordinary writer still reconstructs, tests, attests and leases publication.

Map: policy/request validation; bounded artifact reader; live run checks;
authorize_request. No branch updates or benchmark execution live here.
"""
from __future__ import annotations

import base64
import fnmatch
import hashlib
import io
import json
from pathlib import Path
import re
import urllib.error
import urllib.parse
import urllib.request
import zipfile

POLICY_PATH = ".github/native-retirement-automation.json"
POLICY_SCHEMA = "buster-native-retirement-automation-policy-v1"
REQUEST_SCHEMA = "buster-native-retirement-automation-request-v1"
CONTROLLER_PATH = ".github/workflows/native-retirement-automation.yml"
WRITER_PATH = ".github/workflows/native-retirement-integration.yml"
BOT_LOGIN = "github-actions[bot]"
BOT_ID = 41898282
MAX_JSON_BYTES = 16 * 1024
MAX_ARCHIVE_BYTES = 128 * 1024
MAX_API_PAGES = 10
CONTROLLER_EVENTS = frozenset(("push", "schedule", "workflow_run", "workflow_dispatch"))
POLICY_KEYS = frozenset(("schema", "repository", "enabled", "epoch", "classes",
                         "paused_pull_requests"))
REQUEST_KEYS = frozenset(("schema", "repository", "number", "base", "head",
                          "source_head", "classification", "policy_sha256",
                          "controller_run_id", "controller_attempt", "key"))
KEY_FIELDS = ("schema", "repository", "number", "base", "head", "source_head",
              "classification", "policy_sha256")
# A standing grant cannot modify itself, credential/admission boundaries or
# acceptance budgets. Those remain explicit owner-authorized transitions.
OWNER_ONLY_PATHS = (
    ".github/*", ".gitattributes",
    "tools/native_retirement_automation.py", "tools/native_retirement_controller.py",
    "tools/native_retirement_integration.py", "tools/native_retirement_merge_gate.py",
    "tools/merge_conflict_preflight.py", "tools/native_retirement_performance*",
    "tools/throughput/retirement_stats*", "tools/bench_service/deploy/*",
    "tools/bench_service/profiles/*", "tools/bench_service/workflow_policy_test.py",
)


class AutomationError(ValueError):
    """An unavailable or invalid authorization remains blocking."""


class AutomationMoved(AutomationError):
    """A positively identified immutable-input change, not a test failure."""
    def __init__(self, message: str, exit_code: int):
        super().__init__(message)
        self.exit_code = exit_code


def canonical(value: dict) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def digest(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def _unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise AutomationError("duplicate JSON field")
        result[key] = value
    return result


def decode(raw: bytes | str) -> dict:
    if isinstance(raw, str):
        raw = raw.encode()
    if not raw or len(raw) > MAX_JSON_BYTES:
        raise AutomationError("automation JSON is empty or over budget")
    value = json.loads(raw, object_pairs_hook=_unique)
    if not isinstance(value, dict):
        raise AutomationError("automation JSON must be an object")
    return value


def positive(value, label: str) -> int:
    if type(value) is not int or value <= 0:
        raise AutomationError(label + " must be a positive integer")
    return value


def hex_value(value, length: int, label: str) -> str:
    if not isinstance(value, str) or re.fullmatch("[0-9a-f]{" + str(length) + "}", value) is None:
        raise AutomationError(label + " is not a canonical digest")
    return value


def validate_policy(value: dict, repository: str) -> dict:
    if (set(value) != POLICY_KEYS or value.get("schema") != POLICY_SCHEMA or
            value.get("repository") != repository or type(value.get("enabled")) is not bool):
        raise AutomationError("invalid standing automation policy")
    positive(value["epoch"], "policy epoch")
    classes = value["classes"]
    if (not isinstance(classes, list) or not classes or
            any(type(kind) is not str for kind in classes) or
            len(set(classes)) != len(classes) or
            not set(classes) <= {"ordinary", "bootstrap", "policy"}):
        raise AutomationError("invalid standing transition classes")
    paused = value["paused_pull_requests"]
    if not isinstance(paused, list) or len(paused) > 1000:
        raise AutomationError("invalid paused PR list")
    for number in paused:
        positive(number, "paused PR")
    if len(set(paused)) != len(paused):
        raise AutomationError("duplicate paused PR")
    return value


def read_policy(api, base: str) -> tuple[dict, str]:
    """Read the current remote policy, not a workflow-start variable snapshot."""
    hex_value(base, 40, "base")
    before = api.request("git/ref/heads/main")["object"]["sha"]
    if before != base:
        raise AutomationMoved("main moved before automation authorization", 75)
    record = api.request("contents/" + POLICY_PATH, ref="main")
    if record.get("type") != "file" or record.get("encoding") != "base64":
        raise AutomationError("automation policy is not a regular repository file")
    encoded = record.get("content")
    if not isinstance(encoded, str) or len(encoded) > MAX_JSON_BYTES * 2:
        raise AutomationError("automation policy exceeds its byte budget")
    raw = base64.b64decode("".join(encoded.split()), validate=True)
    value = validate_policy(decode(raw), api.repository)
    after = api.request("git/ref/heads/main")["object"]["sha"]
    if after != base:
        raise AutomationMoved("main moved while reading the standing policy", 75)
    return value, digest(raw)


def require_enabled(api, policy: dict) -> None:
    if not policy["enabled"]:
        raise AutomationError("standing automation policy is disabled")
    workflow = api.request("actions/workflows/" + Path(CONTROLLER_PATH).name)
    if workflow.get("path") != CONTROLLER_PATH or workflow.get("state") != "active":
        raise AutomationError("controller disabled or unavailable; standing authorization revoked")


def require_scope(policy: dict, number: int, classification: dict) -> None:
    if number in policy["paused_pull_requests"]:
        raise AutomationError("PR is paused by the standing policy")
    if classification.get("kind") not in policy["classes"]:
        raise AutomationError("transition class is outside the standing grant")
    paths = classification.get("changed_paths")
    if not isinstance(paths, list) or not paths or any(not isinstance(path, str) for path in paths):
        raise AutomationError("missing trusted candidate classification")
    for path in paths:
        if any(fnmatch.fnmatchcase(path, pattern) for pattern in OWNER_ONLY_PATHS):
            raise AutomationError("owner-authorized transition required for " + path)


def request_key(request: dict) -> str:
    return digest(canonical({key: request[key] for key in KEY_FIELDS}))


def validate_request(request: dict, repository: str) -> dict:
    if (set(request) != REQUEST_KEYS or request.get("schema") != REQUEST_SCHEMA or
            request.get("repository") != repository):
        raise AutomationError("invalid automation request schema or repository")
    positive(request["number"], "request PR")
    positive(request["controller_run_id"], "controller run")
    if type(request["controller_attempt"]) is not int or request["controller_attempt"] != 1:
        raise AutomationError("automation requires a fresh controller attempt")
    for field in ("base", "head", "source_head"):
        hex_value(request[field], 40, field)
    for field in ("policy_sha256", "key"):
        hex_value(request[field], 64, field)
    if request["classification"] not in ("ordinary", "bootstrap", "policy"):
        raise AutomationError("invalid automation transition class")
    if request["key"] != request_key(request):
        raise AutomationError("automation request identity mismatch")
    return request


def new_request(repository: str, number: int, base: str, head: str, source: str,
                kind: str, policy_digest: str, controller_run: int) -> dict:
    request = dict(schema=REQUEST_SCHEMA, repository=repository, number=number,
                   base=base, head=head, source_head=source, classification=kind,
                   policy_sha256=policy_digest, controller_run_id=controller_run,
                   controller_attempt=1)
    request["key"] = request_key(request)
    return validate_request(request, repository)


def writer_title(key: str) -> str:
    return "Native retirement request " + hex_value(key, 64, "request key")


def artifact_name(run_id: int) -> str:
    return "native-retirement-automation-request-" + str(positive(run_id, "controller run"))


def is_bot(actor: dict | None) -> bool:
    return (isinstance(actor, dict) and actor.get("login") == BOT_LOGIN and
            type(actor.get("id")) is int and actor["id"] == BOT_ID and actor.get("type") == "Bot")


def verify_run(run: dict, repository: str, run_id: int, path: str, base: str,
               events: frozenset[str], *, bot: bool = False) -> dict:
    if (type(run.get("id")) is not int or run["id"] != run_id or
            run.get("repository", {}).get("full_name") != repository or
            run.get("path") != path or run.get("head_branch") != "main" or
            run.get("head_sha") != base or run.get("event") not in events or
            type(run.get("run_attempt")) is not int or run["run_attempt"] != 1):
        raise AutomationError("untrusted workflow/run/attempt identity")
    if bot and (not is_bot(run.get("actor")) or not is_bot(run.get("triggering_actor"))):
        raise AutomationError("automation dispatcher identity mismatch")
    return run


def collection(api, path: str, key: str, **query) -> list:
    rows = []
    for page in range(1, MAX_API_PAGES + 1):
        data = api.request(path, per_page=100, page=page, **query)
        items = data.get(key) if isinstance(data, dict) else None
        if not isinstance(items, list):
            raise AutomationError("malformed paginated GitHub response")
        rows.extend(items)
        if len(items) < 100:
            return rows
    raise AutomationError("GitHub response exceeds the bounded pagination limit")


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, newurl):
        return None


def _bounded_read(response, maximum: int) -> bytes:
    raw = response.read(maximum + 1)
    if len(raw) > maximum:
        raise AutomationError("artifact exceeds the bounded download budget")
    return raw


def download_archive(api, artifact_id: int) -> bytes:
    """Never forward the API Authorization header to the signed object URL."""
    positive(artifact_id, "artifact id")
    url = "https://api.github.com/repos/" + api.repository + "/actions/artifacts/" + str(artifact_id) + "/zip"
    request = urllib.request.Request(url, headers={
        "Authorization": "Bearer " + api.token, "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
    })
    opener = urllib.request.build_opener(_NoRedirect())
    try:
        with opener.open(request, timeout=30) as response:
            return _bounded_read(response, MAX_ARCHIVE_BYTES)
    except urllib.error.HTTPError as error:
        if error.code != 302:
            raise
        location = error.headers.get("Location", "")
        error.close()
    parsed = urllib.parse.urlsplit(location)
    host = parsed.hostname or ""
    if (parsed.scheme != "https" or parsed.username or parsed.password or parsed.port not in (None, 443) or
            not (host.endswith(".blob.core.windows.net") or host.endswith(".actions.githubusercontent.com"))):
        raise AutomationError("untrusted artifact download redirect")
    # No automatic second redirect and no credentials on this request.
    with opener.open(urllib.request.Request(location), timeout=30) as response:
        return _bounded_read(response, MAX_ARCHIVE_BYTES)


def request_from_archive(raw: bytes) -> dict:
    if len(raw) > MAX_ARCHIVE_BYTES:
        raise AutomationError("request archive over budget")
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        files = archive.infolist()
        if (len(files) != 1 or files[0].filename != "request.json" or
                files[0].is_dir() or files[0].file_size > MAX_JSON_BYTES or files[0].flag_bits & 1):
            raise AutomationError("request artifact must contain exactly one bounded request.json")
        with archive.open(files[0]) as source:
            return decode(_bounded_read(source, MAX_JSON_BYTES))


def read_request_artifact(api, request: dict) -> dict:
    run_id = request["controller_run_id"]
    artifacts = collection(api, "actions/runs/" + str(run_id) + "/artifacts", "artifacts")
    matches = [item for item in artifacts if item.get("name") == artifact_name(run_id)]
    if len(matches) != 1:
        raise AutomationError("controller request artifact is absent or ambiguous")
    artifact = matches[0]
    if (artifact.get("expired") is not False or type(artifact.get("size_in_bytes")) is not int or
            not 0 < artifact["size_in_bytes"] <= MAX_ARCHIVE_BYTES):
        raise AutomationError("controller request artifact is expired or over budget")
    raw = download_archive(api, positive(artifact.get("id"), "artifact id"))
    expected_digest = artifact.get("digest")
    if expected_digest != "sha256:" + digest(raw):
        raise AutomationError("controller artifact digest is missing or mismatched")
    actual = validate_request(request_from_archive(raw), api.repository)
    if actual != request:
        raise AutomationError("dispatch does not match the trusted controller artifact")
    return {"id": artifact["id"], "sha256": digest(raw)}


def authorize_request(api, pr: dict, actor: str, context: dict, classification: dict) -> dict:
    request = validate_request(decode(context["request_json"]), api.repository)
    if (request["number"] != pr["number"] or request["head"] != pr["head"]["sha"] or
            request["base"] != context["expected_base"] or
            request["source_head"] != context["source_head"] or
            request["classification"] != classification["kind"] or request["key"] != context["request_key"]):
        raise AutomationError("resolved candidate differs from the controller request")
    base = request["base"]
    expected_ref = api.repository + "/" + WRITER_PATH + "@refs/heads/main"
    if (actor != BOT_LOGIN or context.get("actor_id") != str(BOT_ID) or
            context.get("triggering_actor") != BOT_LOGIN or context.get("event_name") != "workflow_dispatch" or
            context.get("workflow_ref") != expected_ref or context.get("workflow_sha") != base or
            context.get("run_attempt") != "1"):
        raise AutomationError("automation requires a fresh trusted machine dispatch on main")
    run_text = context.get("run_id", "")
    if not isinstance(run_text, str) or re.fullmatch(r"[1-9][0-9]*", run_text) is None:
        raise AutomationError("missing automation writer run identity")
    run_id = int(run_text)
    run = api.request("actions/runs/" + str(run_id))
    verify_run(run, api.repository, run_id, WRITER_PATH, base, frozenset(("workflow_dispatch",)), bot=True)
    if run.get("display_title") != writer_title(request["key"]) or run.get("status") != "in_progress":
        raise AutomationError("writer request title or active attempt does not match")
    policy, policy_digest = read_policy(api, base)
    require_enabled(api, policy)
    if policy_digest != request["policy_sha256"]:
        raise AutomationError("standing grant changed after scheduling")
    require_scope(policy, pr["number"], classification)
    controller = api.request("actions/runs/" + str(request["controller_run_id"]))
    verify_run(controller, api.repository, request["controller_run_id"], CONTROLLER_PATH,
               base, CONTROLLER_EVENTS)
    if context.get("publication"):
        if controller.get("status") != "completed" or controller.get("conclusion") != "success":
            raise AutomationError("controller did not successfully complete its dispatch")
    elif not (controller.get("status") == "in_progress" or
              (controller.get("status") == "completed" and controller.get("conclusion") == "success")):
        raise AutomationError("controller request was cancelled or failed")
    artifact = read_request_artifact(api, request)
    # The final live checks follow the potentially slow artifact download.
    final_policy, final_digest = read_policy(api, base)
    require_enabled(api, final_policy)
    if final_digest != policy_digest:
        raise AutomationError("standing grant changed during authorization")
    live_pr = api.request("pulls/" + str(pr["number"]))
    if live_pr.get("head", {}).get("sha") != request["head"]:
        raise AutomationMoved("PR head moved during automation authorization", 76)
    if (live_pr.get("state") != "open" or live_pr.get("draft") is not False or
            live_pr.get("base", {}).get("ref") != "main" or
            live_pr.get("base", {}).get("repo", {}).get("full_name") != api.repository or
            (live_pr.get("head", {}).get("repo") or {}).get("full_name") != api.repository):
        raise AutomationError("PR eligibility changed during authorization")
    return {
        "number": pr["number"], "head": request["head"], "author": pr["user"]["login"],
        "dispatcher": actor, "dispatcher_permission": "standing-automation",
        "transition_kind": classification["kind"], "maintainer_approvals": [],
        "authorization": {
            "mode": "automation", "request": request, "policy_epoch": policy["epoch"],
            "policy_sha256": policy_digest, "controller_artifact": artifact,
            "actor": {"login": BOT_LOGIN, "id": BOT_ID}, "run_id": run_id,
            "run_attempt": 1, "workflow_ref": expected_ref, "workflow_sha": base,
        },
    }
