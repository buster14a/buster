#!/usr/bin/env python3
"""Dispatch a source-free GitHub Actions broker and mirror its result.

The Forgejo job running this program keeps every durable credential locally.
GitHub durably stores nothing that grants source access: the broker receives
only a random request identifier, the exact Git object ID to fetch, and a
freshly generated read-only deploy key that is sealed into a repository secret
for the duration of one run, then deleted from GitHub and revoked on Forgejo.
A breach of GitHub's secret store between runs therefore yields no key at all,
and a breach during a run yields a key that stops working minutes later.
"""

from __future__ import annotations

import argparse
import base64
import datetime
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid


API_VERSION = "2022-11-28"
TERMINAL_STATUSES = {"completed"}
DEPLOY_KEY_TITLE_PREFIX = "github-bridge-"
DEPLOY_KEY_SECRET_NAME = "FORGEJO_DEPLOY_KEY"
STALE_DEPLOY_KEY_SECONDS = 2 * 60 * 60
REPOSITORY_RE = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+\Z")
WORKFLOW_RE = re.compile(r"[A-Za-z0-9_.-]+\Z")
OBJECT_ID_RE = re.compile(r"(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})\Z")
REF_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._/-]*\Z")


class BridgeError(RuntimeError):
    pass


class BridgeTimeout(BridgeError):
    pass


def validate_repository(value: str) -> str:
    if not REPOSITORY_RE.fullmatch(value) or ".." in value:
        raise BridgeError("bridge repositories must be OWNER/REPOSITORY")
    return value


def validate_workflow(value: str) -> str:
    if not WORKFLOW_RE.fullmatch(value):
        raise BridgeError("GitHub workflow must be a file name or numeric workflow id")
    return value


def validate_object_id(value: str) -> str:
    if not OBJECT_ID_RE.fullmatch(value):
        raise BridgeError("Forgejo SHA must be a full 40- or 64-character Git object id")
    return value.lower()


def validate_ref(value: str) -> str:
    if (
        not REF_RE.fullmatch(value)
        or ".." in value
        or value.startswith("refs/")
        or value.endswith(("/", "."))
    ):
        raise BridgeError("GitHub workflow ref must be a short branch name")
    return value


def validate_api_url(value: str) -> str:
    parsed = urllib.parse.urlsplit(value)
    if parsed.scheme != "https" or not parsed.netloc:
        raise BridgeError("the Forgejo API URL must be an https:// origin")
    return value.rstrip("/")


class JsonApiClient:
    """Minimal urllib JSON client shared by the GitHub and Forgejo halves."""

    def __init__(self, api_url: str, headers: dict[str, str]) -> None:
        self.api_url = api_url.rstrip("/")
        self.headers = headers

    def request(
        self,
        method: str,
        path: str,
        payload: object | None = None,
        accept_missing: bool = False,
    ) -> object | None:
        body = None
        if payload is not None:
            body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        request = urllib.request.Request(
            self.api_url + path,
            data=body,
            method=method,
            headers=dict(self.headers, **{"Content-Type": "application/json"}),
        )
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                response_body = response.read()
        except urllib.error.HTTPError as error:
            if accept_missing and error.code == 404:
                return None
            raise BridgeError("API %s %s failed with HTTP %d" % (method, path, error.code)) from error
        except (urllib.error.URLError, TimeoutError, OSError) as error:
            raise BridgeError("API %s %s could not be reached" % (method, path)) from error
        if not response_body:
            return None
        try:
            return json.loads(response_body.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise BridgeError("API %s %s returned malformed JSON" % (method, path)) from error


def quote_repository(repository: str) -> str:
    return "/".join(urllib.parse.quote(part, safe="") for part in repository.split("/"))


class GitHubClient(JsonApiClient):
    def __init__(self, token: str, repository: str, api_url: str = "https://api.github.com") -> None:
        if not token:
            raise BridgeError("GITHUB_BRIDGE_TOKEN is not configured")
        super().__init__(
            api_url,
            {
                "Accept": "application/vnd.github+json",
                "Authorization": "Bearer " + token,
                "User-Agent": "buster-forgejo-github-runner-bridge",
                "X-GitHub-Api-Version": API_VERSION,
            },
        )
        self.token = token
        self.repository = validate_repository(repository)
        self.active_run_id: int | None = None

    def workflow_path(self, workflow: str, suffix: str) -> str:
        return "/repos/%s/actions/workflows/%s%s" % (
            quote_repository(self.repository),
            urllib.parse.quote(workflow, safe=""),
            suffix,
        )

    def dispatch(self, workflow: str, ref: str, request_id: str, object_id: str) -> None:
        self.request(
            "POST",
            self.workflow_path(workflow, "/dispatches"),
            {"ref": ref, "inputs": {"request_id": request_id, "forgejo_sha": object_id}},
        )

    def list_runs(self, workflow: str, ref: str) -> list[dict[str, object]]:
        query = urllib.parse.urlencode({"event": "workflow_dispatch", "branch": ref, "per_page": 100})
        result = self.request("GET", self.workflow_path(workflow, "/runs?") + query)
        if not isinstance(result, dict) or not isinstance(result.get("workflow_runs"), list):
            raise BridgeError("GitHub workflow-runs response has an unexpected shape")
        return result["workflow_runs"]

    def count_active_runs(self, workflow: str) -> int:
        count = 0
        for status in ("queued", "in_progress"):
            query = urllib.parse.urlencode({"status": status, "per_page": 1})
            result = self.request("GET", self.workflow_path(workflow, "/runs?") + query)
            if not isinstance(result, dict) or not isinstance(result.get("total_count"), int):
                raise BridgeError("GitHub workflow-runs response has an unexpected shape")
            count += result["total_count"]
        return count

    def get_run(self, run_id: int) -> dict[str, object]:
        result = self.request("GET", "/repos/%s/actions/runs/%d" % (quote_repository(self.repository), run_id))
        if not isinstance(result, dict):
            raise BridgeError("GitHub workflow-run response has an unexpected shape")
        return result

    def cancel(self, run_id: int) -> None:
        self.request("POST", "/repos/%s/actions/runs/%d/cancel" % (quote_repository(self.repository), run_id))

    def secrets_public_key(self) -> dict[str, str]:
        result = self.request("GET", "/repos/%s/actions/secrets/public-key" % quote_repository(self.repository))
        if (
            not isinstance(result, dict)
            or not isinstance(result.get("key"), str)
            or not isinstance(result.get("key_id"), str)
        ):
            raise BridgeError("GitHub secrets public-key response has an unexpected shape")
        return {"key": result["key"], "key_id": result["key_id"]}

    def put_secret(self, name: str, encrypted_value: str, key_id: str) -> None:
        self.request(
            "PUT",
            "/repos/%s/actions/secrets/%s" % (quote_repository(self.repository), urllib.parse.quote(name, safe="")),
            {"encrypted_value": encrypted_value, "key_id": key_id},
        )

    def delete_secret(self, name: str) -> None:
        self.request(
            "DELETE",
            "/repos/%s/actions/secrets/%s" % (quote_repository(self.repository), urllib.parse.quote(name, safe="")),
            accept_missing=True,
        )


class ForgejoClient(JsonApiClient):
    def __init__(self, token: str, repository: str, api_url: str) -> None:
        if not token:
            raise BridgeError("FORGEJO_BRIDGE_TOKEN is not configured")
        super().__init__(
            validate_api_url(api_url),
            {
                "Accept": "application/json",
                "Authorization": "token " + token,
                "User-Agent": "buster-forgejo-github-runner-bridge",
            },
        )
        self.repository = validate_repository(repository)

    def create_deploy_key(self, title: str, public_key: str) -> int:
        result = self.request(
            "POST",
            "/repos/%s/keys" % quote_repository(self.repository),
            {"title": title, "key": public_key, "read_only": True},
        )
        if not isinstance(result, dict) or not isinstance(result.get("id"), int):
            raise BridgeError("Forgejo deploy-key response has an unexpected shape")
        return result["id"]

    def list_deploy_keys(self) -> list[dict[str, object]]:
        keys = []
        page = 1
        while True:
            result = self.request(
                "GET", "/repos/%s/keys?limit=50&page=%d" % (quote_repository(self.repository), page)
            )
            if not isinstance(result, list):
                raise BridgeError("Forgejo deploy-key list has an unexpected shape")
            # An instance may cap the requested page size below 50. Only an
            # empty page proves the sweep is complete. Collect before deleting
            # so revocation cannot shift unseen keys into an earlier page.
            if not result:
                return keys
            keys.extend(result)
            page += 1

    def delete_deploy_key(self, key_id: int) -> None:
        self.request(
            "DELETE",
            "/repos/%s/keys/%d" % (quote_repository(self.repository), key_id),
            accept_missing=True,
        )


def _scrub_file(path: str) -> None:
    try:
        size = os.path.getsize(path)
        with open(path, "r+b") as handle:
            handle.write(b"\0" * size)
            handle.flush()
            os.fsync(handle.fileno())
    except OSError:
        pass


def generate_deploy_key(request_id: str) -> tuple[str, str]:
    """Generate an ephemeral ed25519 keypair, preferring memory-backed storage.

    The key files exist only long enough to be read back; they are then
    zero-overwritten and unlinked. On Linux the directory lives in /dev/shm so
    the private key never reaches persistent media on the trusted runner
    either.
    """
    base = "/dev/shm" if os.path.isdir("/dev/shm") and os.access("/dev/shm", os.W_OK) else None
    directory = tempfile.mkdtemp(prefix="buster-bridge-", dir=base)
    key_path = os.path.join(directory, "key")
    try:
        completed = subprocess.run(
            ["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", DEPLOY_KEY_TITLE_PREFIX + request_id, "-f", key_path],
            stdin=subprocess.DEVNULL,
            capture_output=True,
        )
        if completed.returncode != 0:
            raise BridgeError("ssh-keygen could not generate the ephemeral deploy key")
        with open(key_path, "r", encoding="ascii") as handle:
            private_key = handle.read()
        with open(key_path + ".pub", "r", encoding="ascii") as handle:
            public_key = handle.read().strip()
        return private_key, public_key
    except OSError as error:
        raise BridgeError("the ephemeral deploy key could not be read back") from error
    finally:
        _scrub_file(key_path)
        shutil.rmtree(directory, ignore_errors=True)


def _pynacl_sealer():
    """Return a libsodium sealed-box closure, or None when PyNaCl is absent."""
    try:
        from nacl import encoding, public
    except ImportError:
        return None

    def seal(public_key_base64: str, value: str) -> str:
        key = public.PublicKey(public_key_base64.encode("ascii"), encoding.Base64Encoder())
        sealed = public.SealedBox(key).encrypt(value.encode("utf-8"))
        return base64.b64encode(sealed).decode("ascii")

    return seal


def set_repository_secret(client: GitHubClient, name: str, value: str) -> None:
    """Seal `value` for GitHub's secret store, never exposing it on disk or argv."""
    sealer = _pynacl_sealer()
    if sealer is not None:
        material = client.secrets_public_key()
        client.put_secret(name, sealer(material["key"], value), material["key_id"])
        return
    gh = shutil.which("gh")
    if gh is None:
        raise BridgeError("sealing the run secret needs PyNaCl (pip install pynacl) or the gh CLI")
    completed = subprocess.run(
        [gh, "secret", "set", name, "--app", "actions", "--repo", client.repository],
        input=value.encode("utf-8"),
        env=dict(os.environ, GH_TOKEN=client.token),
        capture_output=True,
    )
    if completed.returncode != 0:
        raise BridgeError("gh could not store the run secret (exit %d)" % completed.returncode)


def cleanup_stale_deploy_keys(forgejo: ForgejoClient, now: datetime.datetime | None = None) -> None:
    """Best-effort revocation of bridge keys a crashed earlier run left behind."""
    if now is None:
        now = datetime.datetime.now(datetime.timezone.utc)
    try:
        keys = forgejo.list_deploy_keys()
    except BridgeError:
        print("warning: could not list Forgejo deploy keys for stale-key cleanup", file=sys.stderr)
        return
    for key in keys:
        if not isinstance(key, dict) or not isinstance(key.get("id"), int):
            continue
        title = key.get("title")
        created_at = key.get("created_at")
        if not isinstance(title, str) or not title.startswith(DEPLOY_KEY_TITLE_PREFIX):
            continue
        if not isinstance(created_at, str):
            continue
        try:
            created = datetime.datetime.fromisoformat(created_at.replace("Z", "+00:00"))
        except ValueError:
            continue
        if (now - created).total_seconds() <= STALE_DEPLOY_KEY_SECONDS:
            continue
        try:
            forgejo.delete_deploy_key(key["id"])
            print("GITHUB_BRIDGE stale_deploy_key_revoked=%d" % key["id"], flush=True)
        except BridgeError:
            print("warning: could not revoke stale Forgejo deploy key %d" % key["id"], file=sys.stderr)


def wait_for_idle(
    client: GitHubClient,
    workflow: str,
    timeout_seconds: float,
    poll_seconds: float,
    monotonic=time.monotonic,
    sleep=time.sleep,
) -> None:
    """Serialize runs: one shared secret name means one broker run at a time."""
    deadline = monotonic() + timeout_seconds
    announced = False
    while True:
        if client.count_active_runs(workflow) == 0:
            return
        if monotonic() >= deadline:
            raise BridgeTimeout("another broker run stayed active for %d seconds" % timeout_seconds)
        if not announced:
            print("GITHUB_BRIDGE waiting_for_idle=true", flush=True)
            announced = True
        sleep(poll_seconds)


def matching_run(runs: list[dict[str, object]], request_id: str) -> dict[str, object] | None:
    expected_title = "forgejo-" + request_id
    candidates = []
    for run in runs:
        if not isinstance(run, dict):
            continue
        if expected_title not in (run.get("display_title"), run.get("run_name"), run.get("name")):
            continue
        if not isinstance(run.get("id"), int):
            continue
        candidates.append(run)
    if not candidates:
        return None
    return max(candidates, key=lambda run: int(run["id"]))


def wait_for_completion(
    client: GitHubClient,
    workflow: str,
    ref: str,
    request_id: str,
    timeout_seconds: float,
    poll_seconds: float,
    discovery_timeout_seconds: float,
    monotonic=time.monotonic,
    sleep=time.sleep,
) -> dict[str, object]:
    start = monotonic()
    deadline = start + timeout_seconds
    discovery_deadline = start + min(discovery_timeout_seconds, timeout_seconds)
    run_id = None
    announced = False
    consecutive_api_failures = 0
    while monotonic() < deadline:
        try:
            if run_id is None:
                run = matching_run(client.list_runs(workflow, ref), request_id)
                if run is not None:
                    run_id = int(run["id"])
                    client.active_run_id = run_id
            else:
                run = client.get_run(run_id)
            consecutive_api_failures = 0
        except BridgeError:
            consecutive_api_failures += 1
            if consecutive_api_failures >= 3:
                raise
            sleep(poll_seconds)
            continue
        if run_id is None:
            if monotonic() >= discovery_deadline:
                raise BridgeTimeout(
                    "the dispatched GitHub run was not discovered within %d seconds" % discovery_timeout_seconds
                )
        else:
            if not announced:
                print("GITHUB_BRIDGE run_id=%d discovered=true" % run_id, flush=True)
                announced = True
            if run.get("status") in TERMINAL_STATUSES:
                return run
        sleep(poll_seconds)
    error = BridgeTimeout("GitHub workflow did not complete within %d seconds" % timeout_seconds)
    setattr(error, "run_id", run_id)
    raise error


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--github-repository", default=os.environ.get("GITHUB_BRIDGE_REPOSITORY", ""))
    parser.add_argument("--workflow", default=os.environ.get("GITHUB_BRIDGE_WORKFLOW", "forgejo-hosted.yml"))
    parser.add_argument("--ref", default=os.environ.get("GITHUB_BRIDGE_REF", "main"))
    parser.add_argument("--sha", default=os.environ.get("FORGEJO_SHA", ""))
    parser.add_argument("--forgejo-repository", default=os.environ.get("FORGEJO_BRIDGE_REPOSITORY", ""))
    parser.add_argument("--forgejo-api-url", default=os.environ.get("FORGEJO_BRIDGE_API_URL", ""))
    parser.add_argument("--timeout-seconds", type=int, default=7500)
    parser.add_argument("--poll-seconds", type=float, default=15.0)
    parser.add_argument("--discovery-timeout-seconds", type=int, default=900)
    parser.add_argument("--idle-timeout-seconds", type=int, default=900)
    return parser


def main(argv: list[str] | None = None) -> int:
    run_id = None
    github = None
    forgejo = None
    deploy_key_id = None
    secret_stored = False
    status = 2
    try:
        arguments = build_parser().parse_args(argv)
        workflow = validate_workflow(arguments.workflow)
        ref = validate_ref(arguments.ref)
        object_id = validate_object_id(arguments.sha)
        if (
            arguments.timeout_seconds <= 0
            or arguments.poll_seconds <= 0
            or arguments.discovery_timeout_seconds <= 0
            or arguments.idle_timeout_seconds <= 0
        ):
            raise BridgeError("poll and timeout values must be positive")
        request_id = str(uuid.uuid4())
        github = GitHubClient(os.environ.get("GITHUB_BRIDGE_TOKEN", ""), arguments.github_repository)
        forgejo = ForgejoClient(
            os.environ.get("FORGEJO_BRIDGE_TOKEN", ""),
            arguments.forgejo_repository,
            arguments.forgejo_api_url,
        )

        cleanup_stale_deploy_keys(forgejo)
        wait_for_idle(github, workflow, arguments.idle_timeout_seconds, arguments.poll_seconds)

        private_key, public_key = generate_deploy_key(request_id)
        deploy_key_id = forgejo.create_deploy_key(DEPLOY_KEY_TITLE_PREFIX + request_id, public_key)
        print("GITHUB_BRIDGE deploy_key_id=%d created=true" % deploy_key_id, flush=True)
        set_repository_secret(github, DEPLOY_KEY_SECRET_NAME, private_key)
        secret_stored = True
        private_key = None

        github.dispatch(workflow, ref, request_id, object_id)
        print("GITHUB_BRIDGE request_id=%s dispatched=true" % request_id, flush=True)
        run = wait_for_completion(
            github,
            workflow,
            ref,
            request_id,
            arguments.timeout_seconds,
            arguments.poll_seconds,
            arguments.discovery_timeout_seconds,
        )
        run_id = int(run["id"])
        conclusion = str(run.get("conclusion") or "none")
        print(
            "GITHUB_BRIDGE run_id=%d conclusion=%s url=%s" % (run_id, conclusion, run.get("html_url") or "unknown"),
            flush=True,
        )
        status = 0 if conclusion == "success" else 1
    except (BridgeError, KeyboardInterrupt) as error:
        possible_run_id = getattr(error, "run_id", None)
        if isinstance(possible_run_id, int):
            run_id = possible_run_id
        if run_id is None and github is not None:
            run_id = github.active_run_id
        if github is not None and run_id is not None:
            try:
                github.cancel(run_id)
                print("GITHUB_BRIDGE run_id=%d cancellation_requested=true" % run_id, flush=True)
            except BridgeError:
                print("warning: GitHub bridge cancellation failed", file=sys.stderr)
        message = "interrupted" if isinstance(error, KeyboardInterrupt) else str(error)
        print("error: GitHub runner bridge: " + message, file=sys.stderr)
        status = 2
    finally:
        # The run credential must not outlive the run. Both halves are
        # best-effort here, but a failed secret deletion is survivable because
        # the Forgejo revocation below (or the stale-key sweep of the next
        # dispatch) makes the leaked half useless.
        if secret_stored and github is not None:
            try:
                github.delete_secret(DEPLOY_KEY_SECRET_NAME)
                print("GITHUB_BRIDGE secret_deleted=true", flush=True)
            except BridgeError:
                print("warning: could not delete the GitHub run secret", file=sys.stderr)
        if deploy_key_id is not None and forgejo is not None:
            try:
                forgejo.delete_deploy_key(deploy_key_id)
                print("GITHUB_BRIDGE deploy_key_id=%d revoked=true" % deploy_key_id, flush=True)
            except BridgeError:
                print("warning: could not revoke the Forgejo deploy key %d" % deploy_key_id, file=sys.stderr)
    return status


def interrupt(_signum: int, _frame: object) -> None:
    raise KeyboardInterrupt


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, interrupt)
    raise SystemExit(main())
