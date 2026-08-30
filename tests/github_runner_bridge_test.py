#!/usr/bin/env python3
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import urllib.error
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPOSITORY_ROOT / ".forgejo" / "scripts" / "github_runner_bridge.py"
SPEC = importlib.util.spec_from_file_location("github_runner_bridge", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
bridge = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = bridge
SPEC.loader.exec_module(bridge)

NONCE = "00000000-0000-4000-8000-000000000000"


class FakeClock:
    def __init__(self):
        self.value = 0.0

    def monotonic(self):
        return self.value

    def sleep(self, seconds):
        self.value += seconds


class FakeGitHub:
    def __init__(self, runs=None, updates=None):
        self.runs = list(runs or [])
        self.updates = list(updates or [])
        self.active_run_id = None
        self.dispatched = []
        self.cancelled = []
        self.deleted_secrets = []
        self.active_counts = []

    def dispatch(self, *arguments):
        self.dispatched.append(arguments)

    def list_runs(self, _workflow, _ref):
        return self.runs.pop(0) if self.runs else []

    def count_active_runs(self, _workflow):
        return self.active_counts.pop(0) if self.active_counts else 0

    def get_run(self, _run_id):
        return self.updates.pop(0)

    def cancel(self, run_id):
        self.cancelled.append(run_id)

    def delete_secret(self, name):
        self.deleted_secrets.append(name)


class FakeForgejo:
    def __init__(self, keys=None):
        self.keys = list(keys or [])
        self.created = []
        self.deleted = []

    def create_deploy_key(self, title, public_key):
        self.created.append((title, public_key))
        return 7

    def list_deploy_keys(self):
        return self.keys

    def delete_deploy_key(self, key_id):
        self.deleted.append(key_id)


class FakeResponse:
    def __init__(self, body=b""):
        self.body = body

    def __enter__(self):
        return self

    def __exit__(self, _kind, _value, _traceback):
        return False

    def read(self):
        return self.body


def http_error(code):
    return urllib.error.HTTPError("https://api.example", code, "error", {}, io.BytesIO(b""))


def workflow_run(run_id=91, status="queued", conclusion=None):
    return {
        "id": run_id,
        "display_title": "forgejo-" + NONCE,
        "created_at": "2026-08-28T12:00:00Z",
        "status": status,
        "conclusion": conclusion,
        "html_url": "https://github.example/runs/%d" % run_id,
    }


class ValidationTests(unittest.TestCase):
    def test_validates_configuration_without_accepting_paths_or_short_shas(self):
        self.assertEqual(bridge.validate_repository("owner/broker.repo"), "owner/broker.repo")
        self.assertEqual(bridge.validate_workflow("forgejo-hosted.yml"), "forgejo-hosted.yml")
        self.assertEqual(bridge.validate_ref("release/main"), "release/main")
        self.assertEqual(bridge.validate_object_id("A" * 40), "a" * 40)
        self.assertEqual(bridge.validate_object_id("b" * 64), "b" * 64)
        self.assertEqual(bridge.validate_api_url("https://forge.example/api/v1/"), "https://forge.example/api/v1")
        for invalid in ("owner/repo/extra", "../repo", "owner/"):
            with self.assertRaises(bridge.BridgeError):
                bridge.validate_repository(invalid)
        with self.assertRaises(bridge.BridgeError):
            bridge.validate_workflow("directory/workflow.yml")
        with self.assertRaises(bridge.BridgeError):
            bridge.validate_ref("main..backup")
        with self.assertRaises(bridge.BridgeError):
            bridge.validate_ref("refs/heads/main")
        with self.assertRaises(bridge.BridgeError):
            bridge.validate_object_id("abc123")
        with self.assertRaises(bridge.BridgeError):
            bridge.validate_api_url("http://forge.example/api/v1")

    def test_dispatch_sends_only_ref_nonce_and_exact_sha(self):
        client = bridge.GitHubClient("secret", "owner/broker")
        client.request = mock.Mock(return_value=None)
        client.dispatch("forgejo-hosted.yml", "main", "nonce", "a" * 40)
        client.request.assert_called_once_with(
            "POST",
            "/repos/owner/broker/actions/workflows/forgejo-hosted.yml/dispatches",
            {"ref": "main", "inputs": {"request_id": "nonce", "forgejo_sha": "a" * 40}},
        )


class HttpWiringTests(unittest.TestCase):
    def test_github_dispatch_list_and_cancel(self):
        listed = json.dumps({"workflow_runs": [workflow_run()]}).encode()
        responses = [FakeResponse(), FakeResponse(listed), FakeResponse()]
        client = bridge.GitHubClient("secret", "owner/broker")
        with mock.patch.object(bridge.urllib.request, "urlopen", side_effect=responses) as urlopen:
            client.dispatch("forgejo-hosted.yml", "main", "nonce", "a" * 40)
            self.assertEqual(len(client.list_runs("forgejo-hosted.yml", "main")), 1)
            client.cancel(91)

        dispatch_request = urlopen.call_args_list[0].args[0]
        self.assertEqual(dispatch_request.get_method(), "POST")
        self.assertEqual(dispatch_request.get_header("Authorization"), "Bearer secret")
        self.assertEqual(
            dispatch_request.full_url,
            "https://api.github.com/repos/owner/broker/actions/workflows/forgejo-hosted.yml/dispatches",
        )
        self.assertEqual(json.loads(dispatch_request.data), {
            "ref": "main",
            "inputs": {"request_id": "nonce", "forgejo_sha": "a" * 40},
        })
        self.assertIn("event=workflow_dispatch", urlopen.call_args_list[1].args[0].full_url)
        self.assertIn("branch=main", urlopen.call_args_list[1].args[0].full_url)
        self.assertTrue(urlopen.call_args_list[2].args[0].full_url.endswith("/actions/runs/91/cancel"))

    def test_github_secret_lifecycle_wiring(self):
        material = json.dumps({"key": "cHVibGlj", "key_id": "568250167242549743"}).encode()
        responses = [FakeResponse(material), FakeResponse(), http_error(404)]
        client = bridge.GitHubClient("secret", "owner/broker")
        with mock.patch.object(bridge.urllib.request, "urlopen", side_effect=responses) as urlopen:
            self.assertEqual(client.secrets_public_key()["key_id"], "568250167242549743")
            client.put_secret("FORGEJO_DEPLOY_KEY", "sealed", "568250167242549743")
            client.delete_secret("FORGEJO_DEPLOY_KEY")

        self.assertTrue(urlopen.call_args_list[0].args[0].full_url.endswith("/actions/secrets/public-key"))
        put_request = urlopen.call_args_list[1].args[0]
        self.assertEqual(put_request.get_method(), "PUT")
        self.assertTrue(put_request.full_url.endswith("/actions/secrets/FORGEJO_DEPLOY_KEY"))
        self.assertEqual(json.loads(put_request.data), {
            "encrypted_value": "sealed",
            "key_id": "568250167242549743",
        })
        # A missing secret on DELETE is success, not an error: the credential
        # is already gone.
        self.assertEqual(urlopen.call_args_list[2].args[0].get_method(), "DELETE")

    def test_forgejo_deploy_key_wiring(self):
        created = json.dumps({"id": 7}).encode()
        responses = [FakeResponse(created), http_error(404)]
        client = bridge.ForgejoClient("forgejo-token", "buster/buster", "https://forge.example/api/v1")
        with mock.patch.object(bridge.urllib.request, "urlopen", side_effect=responses) as urlopen:
            self.assertEqual(client.create_deploy_key("github-bridge-nonce", "ssh-ed25519 AAAA key"), 7)
            client.delete_deploy_key(7)

        create_request = urlopen.call_args_list[0].args[0]
        self.assertEqual(create_request.get_method(), "POST")
        self.assertEqual(create_request.get_header("Authorization"), "token forgejo-token")
        self.assertEqual(create_request.full_url, "https://forge.example/api/v1/repos/buster/buster/keys")
        self.assertEqual(json.loads(create_request.data), {
            "title": "github-bridge-nonce",
            "key": "ssh-ed25519 AAAA key",
            "read_only": True,
        })
        self.assertTrue(urlopen.call_args_list[1].args[0].full_url.endswith("/repos/buster/buster/keys/7"))

    def test_http_timeout_and_malformed_json_fail_closed(self):
        client = bridge.GitHubClient("secret", "owner/broker")
        with mock.patch.object(bridge.urllib.request, "urlopen", side_effect=TimeoutError()):
            with self.assertRaises(bridge.BridgeError):
                client.list_runs("forgejo-hosted.yml", "main")
        with mock.patch.object(bridge.urllib.request, "urlopen", return_value=FakeResponse(b"not-json")):
            with self.assertRaises(bridge.BridgeError):
                client.list_runs("forgejo-hosted.yml", "main")


class SealingTests(unittest.TestCase):
    class FakeSecretClient:
        token = "secret"
        repository = "owner/broker"

        def __init__(self):
            self.stored = []

        def secrets_public_key(self):
            return {"key": "cHVibGlj", "key_id": "42"}

        def put_secret(self, name, encrypted_value, key_id):
            self.stored.append((name, encrypted_value, key_id))

    def test_pynacl_path_seals_and_stores(self):
        client = self.FakeSecretClient()
        with mock.patch.object(bridge, "_pynacl_sealer", return_value=lambda key, value: "sealed:%s:%s" % (key, value)):
            bridge.set_repository_secret(client, "FORGEJO_DEPLOY_KEY", "PRIVATE")
        self.assertEqual(client.stored, [("FORGEJO_DEPLOY_KEY", "sealed:cHVibGlj:PRIVATE", "42")])

    def test_gh_fallback_receives_value_on_stdin_never_argv(self):
        client = self.FakeSecretClient()
        completed = mock.Mock(returncode=0)
        with mock.patch.object(bridge, "_pynacl_sealer", return_value=None), \
                mock.patch.object(bridge.shutil, "which", return_value="/usr/bin/gh"), \
                mock.patch.object(bridge.subprocess, "run", return_value=completed) as run:
            bridge.set_repository_secret(client, "FORGEJO_DEPLOY_KEY", "PRIVATE")
        arguments = run.call_args.args[0]
        self.assertEqual(arguments[:3], ["/usr/bin/gh", "secret", "set"])
        self.assertNotIn("PRIVATE", " ".join(arguments))
        self.assertEqual(run.call_args.kwargs["input"], b"PRIVATE")
        self.assertEqual(run.call_args.kwargs["env"]["GH_TOKEN"], "secret")

    def test_no_sealer_fails_closed_before_any_credential_moves(self):
        client = self.FakeSecretClient()
        with mock.patch.object(bridge, "_pynacl_sealer", return_value=None), \
                mock.patch.object(bridge.shutil, "which", return_value=None):
            with self.assertRaises(bridge.BridgeError):
                bridge.set_repository_secret(client, "FORGEJO_DEPLOY_KEY", "PRIVATE")
        self.assertEqual(client.stored, [])


@unittest.skipUnless(shutil.which("ssh-keygen"), "ssh-keygen is unavailable")
class KeypairTests(unittest.TestCase):
    def test_generates_ed25519_pair_and_removes_the_files(self):
        with tempfile.TemporaryDirectory() as parent:
            key_directory = os.path.join(parent, "buster-bridge-test")
            os.mkdir(key_directory, 0o700)
            with mock.patch.object(bridge.tempfile, "mkdtemp", return_value=key_directory):
                private_key, public_key = bridge.generate_deploy_key(NONCE)
            self.assertFalse(os.path.exists(key_directory))
        self.assertTrue(private_key.startswith("-----BEGIN OPENSSH PRIVATE KEY-----"))
        self.assertTrue(public_key.startswith("ssh-ed25519 "))
        self.assertTrue(public_key.endswith("github-bridge-" + NONCE))


class StaleKeyTests(unittest.TestCase):
    def test_revokes_only_old_bridge_keys(self):
        import datetime
        now = datetime.datetime(2026, 8, 30, 12, 0, 0, tzinfo=datetime.timezone.utc)
        forgejo = FakeForgejo(keys=[
            {"id": 1, "title": "github-bridge-old", "created_at": "2026-08-30T09:00:00Z"},
            {"id": 2, "title": "github-bridge-fresh", "created_at": "2026-08-30T11:30:00Z"},
            {"id": 3, "title": "operator-laptop", "created_at": "2020-01-01T00:00:00Z"},
            {"id": 4, "title": "github-bridge-broken", "created_at": "not-a-date"},
            "not-a-key",
        ])
        bridge.cleanup_stale_deploy_keys(forgejo, now=now)
        self.assertEqual(forgejo.deleted, [1])


class PollingTests(unittest.TestCase):
    def test_idle_wait_serializes_on_the_shared_secret(self):
        github = FakeGitHub()
        github.active_counts = [1, 1, 0]
        clock = FakeClock()
        bridge.wait_for_idle(github, "forgejo-hosted.yml", 60, 1, monotonic=clock.monotonic, sleep=clock.sleep)
        self.assertEqual(github.active_counts, [])
        github.active_counts = [1, 1, 1]
        with self.assertRaises(bridge.BridgeTimeout):
            bridge.wait_for_idle(github, "forgejo-hosted.yml", 2, 1, monotonic=clock.monotonic, sleep=clock.sleep)

    def test_discovers_nonce_and_waits_for_success(self):
        queued = workflow_run()
        completed = workflow_run(status="completed", conclusion="success")
        github = FakeGitHub(runs=[[queued]], updates=[completed])
        clock = FakeClock()
        result = bridge.wait_for_completion(
            github,
            "forgejo-hosted.yml",
            "main",
            NONCE,
            timeout_seconds=30,
            poll_seconds=1,
            discovery_timeout_seconds=10,
            monotonic=clock.monotonic,
            sleep=clock.sleep,
        )
        self.assertEqual(result["conclusion"], "success")
        self.assertEqual(github.active_run_id, 91)

    def test_ignores_wrong_or_malformed_runs(self):
        wrong = workflow_run(run_id=1)
        wrong["display_title"] = "forgejo-someone-else"
        old = workflow_run(run_id=2)
        old["display_title"] = "forgejo-also-someone-else"
        self.assertIsNone(bridge.matching_run([wrong, old, "not-a-run"], NONCE))

    def test_accepts_custom_run_name_and_prefers_the_newest_id(self):
        renamed = workflow_run(run_id=3)
        renamed["display_title"] = "Workflow dispatch"
        renamed["run_name"] = "forgejo-" + NONCE
        newest = workflow_run(run_id=9)
        self.assertEqual(bridge.matching_run([renamed, newest], NONCE)["id"], 9)

    def test_timeout_retains_discovered_run_for_cancellation(self):
        github = FakeGitHub(runs=[[workflow_run()]], updates=[workflow_run(), workflow_run()])
        clock = FakeClock()
        with self.assertRaises(bridge.BridgeTimeout) as caught:
            bridge.wait_for_completion(
                github,
                "forgejo-hosted.yml",
                "main",
                NONCE,
                timeout_seconds=2,
                poll_seconds=1,
                discovery_timeout_seconds=2,
                monotonic=clock.monotonic,
                sleep=clock.sleep,
            )
        self.assertEqual(caught.exception.run_id, 91)

    def test_undiscovered_dispatch_fails_before_the_full_timeout(self):
        github = FakeGitHub(runs=[[], [], [], [], []])
        clock = FakeClock()
        with self.assertRaises(bridge.BridgeTimeout):
            bridge.wait_for_completion(
                github,
                "forgejo-hosted.yml",
                "main",
                NONCE,
                timeout_seconds=1000,
                poll_seconds=1,
                discovery_timeout_seconds=3,
                monotonic=clock.monotonic,
                sleep=clock.sleep,
            )
        self.assertLess(clock.value, 10)


class MainTests(unittest.TestCase):
    ARGV = [
        "--github-repository", "owner/broker",
        "--sha", "a" * 40,
        "--forgejo-repository", "buster/buster",
        "--forgejo-api-url", "https://forge.example/api/v1",
    ]

    def setUp(self):
        self.environment = mock.patch.dict(
            os.environ,
            {"GITHUB_BRIDGE_TOKEN": "secret", "FORGEJO_BRIDGE_TOKEN": "forgejo-token"},
            clear=True,
        )
        self.environment.start()
        self.github = FakeGitHub()
        self.forgejo = FakeForgejo()
        patches = [
            mock.patch.object(bridge, "GitHubClient", return_value=self.github),
            mock.patch.object(bridge, "ForgejoClient", return_value=self.forgejo),
            mock.patch.object(bridge, "generate_deploy_key", return_value=("PRIVATE", "ssh-ed25519 AAAA test")),
            mock.patch.object(bridge, "set_repository_secret"),
            mock.patch.object(bridge, "wait_for_idle"),
        ]
        started = [patch.start() for patch in patches]
        self.set_secret = started[3]
        for patch in patches:
            self.addCleanup(patch.stop)

    def tearDown(self):
        self.environment.stop()

    def test_success_revokes_every_credential(self):
        completed = workflow_run(status="completed", conclusion="success")
        with mock.patch.object(bridge, "wait_for_completion", return_value=completed):
            status = bridge.main(self.ARGV)
        self.assertEqual(status, 0)
        self.assertEqual(len(self.github.dispatched), 1)
        self.assertEqual(self.github.dispatched[0][3], "a" * 40)
        self.assertEqual(self.github.deleted_secrets, ["FORGEJO_DEPLOY_KEY"])
        self.assertEqual(self.forgejo.deleted, [7])
        self.assertEqual(self.github.cancelled, [])

    def test_failed_conclusion_fails_closed_and_still_cleans_up(self):
        completed = workflow_run(status="completed", conclusion="failure")
        with mock.patch.object(bridge, "wait_for_completion", return_value=completed):
            status = bridge.main(self.ARGV)
        self.assertEqual(status, 1)
        self.assertEqual(self.github.deleted_secrets, ["FORGEJO_DEPLOY_KEY"])
        self.assertEqual(self.forgejo.deleted, [7])

    def test_timeout_requests_cancellation_and_cleans_up(self):
        timeout = bridge.BridgeTimeout("timed out")
        timeout.run_id = 91
        with mock.patch.object(bridge, "wait_for_completion", side_effect=timeout):
            status = bridge.main(self.ARGV)
        self.assertEqual(status, 2)
        self.assertEqual(self.github.cancelled, [91])
        self.assertEqual(self.github.deleted_secrets, ["FORGEJO_DEPLOY_KEY"])
        self.assertEqual(self.forgejo.deleted, [7])

    def test_sealing_failure_never_dispatches_and_revokes_the_deploy_key(self):
        self.set_secret.side_effect = bridge.BridgeError("no sealer")
        status = bridge.main(self.ARGV)
        self.assertEqual(status, 2)
        self.assertEqual(self.github.dispatched, [])
        self.assertEqual(self.github.deleted_secrets, [])
        self.assertEqual(self.forgejo.deleted, [7])


class WorkflowContractTests(unittest.TestCase):
    def test_broker_workflow_keeps_the_privacy_properties(self):
        broker = (REPOSITORY_ROOT / ".forgejo" / "github-bridge" / "forgejo-hosted.yml").read_text()
        self.assertIn("permissions: {}", broker)
        self.assertNotIn("actions/checkout", broker)
        self.assertNotIn("upload-artifact", broker)
        self.assertNotIn("actions/cache", broker)
        self.assertNotIn("test_all_combinations_ci", broker)
        self.assertNotIn("apt-get", broker)
        self.assertNotIn("brew install", broker)
        self.assertNotIn("choco install", broker)
        self.assertIn("clang -Isrc", broker)
        self.assertIn('rm -rf "$source_dir/.git"', broker)
        # The deploy key may only travel through a pipe into ssh-agent: a
        # heredoc or herestring would become a shell temporary file on disk.
        self.assertNotIn("<<<", broker)
        self.assertIn("| ssh-add -", broker)
        self.assertNotIn("set -x", broker)
        # The memory-backed workspace and its supporting hygiene.
        self.assertIn("swapoff -a", broker)
        self.assertIn("mount -t tmpfs", broker)
        self.assertIn("noswap", broker)
        self.assertIn("hdiutil attach -nomount ram://", broker)
        self.assertIn("mdutil -i off", broker)
        self.assertIn("ulimit -c 0", broker)
        # Exactly one secret: the per-run deploy key. Everything else is a
        # variable, so the broker's secret store is empty between runs.
        self.assertEqual(broker.count("secrets."), 1)
        self.assertIn("secrets.FORGEJO_DEPLOY_KEY", broker)
        self.assertIn("vars.FORGEJO_CLONE_URL", broker)
        self.assertIn("vars.FORGEJO_KNOWN_HOSTS", broker)

    def test_forgejo_workflow_only_dispatches_from_trusted_main(self):
        forgejo = (REPOSITORY_ROOT / ".forgejo" / "workflows" / "github-hosted.yml").read_text()
        self.assertIn("branches: [main]", forgejo)
        self.assertNotIn("workflow_dispatch:", forgejo)
        self.assertIn("vars.GH_HOSTED_RUNNERS_ENABLED == 'true'", forgejo)
        self.assertIn("github.ref == 'refs/heads/main'", forgejo)
        self.assertIn("persist-credentials: false", forgejo)
        self.assertRegex(forgejo, r"checkout@[0-9a-f]{40}")


if __name__ == "__main__":
    unittest.main()
