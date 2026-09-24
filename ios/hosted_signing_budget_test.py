#!/usr/bin/env python3
"""Exercise hosted iOS CI policy without an SDK or simulator."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HOSTED = {
    "GITHUB_ACTIONS": "true",
    "RUNNER_ENVIRONMENT": "github-hosted",
    "RUNNER_OS": "macOS",
    "BUSTER_IOS_ARCH": "arm64",
}
WATCHED = (
    "BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS",
    "BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS",
    "BUSTER_IOS_BOOT_TIMEOUT_SECONDS",
    "BUSTER_IOS_INSTALL_TIMEOUT_SECONDS",
    "BUSTER_IOS_SHUTDOWN_TIMEOUT_SECONDS",
)
FAKE_TOOL = r'''#!/usr/bin/env bash
set -euo pipefail
case "${0##*/}" in
    cmake)
        if [[ ${1:-} == --version ]]; then
            echo "fake cmake for signing-policy test"
        elif [[ ${1:-} == --build ]]; then
            build=$2
            config=
            shift 2
            while [[ $# -gt 0 ]]; do
                if [[ $1 == --config ]]; then config=$2; shift; fi
                shift
            done
            [[ $config == Debug || $config == Release ]] || exit 99
            mkdir -p "$build/$config/ide.app"
            printf '%s\n' 'fake simulator executable' > "$build/$config/ide.app/ide"
        else
            [[ ${1:-} == --warn-uninitialized ]] || exit 99
            while [[ $# -gt 0 ]]; do
                if [[ $1 == -B ]]; then mkdir -p "$2"; exit 0; fi
                shift
            done
            exit 99
        fi
        ;;
    xcrun)
        [[ "$*" == '--sdk iphonesimulator --show-sdk-path' ]] || exit 99
        echo /fake/iphonesimulator.sdk
        ;;
    lipo)
        [[ $# -eq 3 && $2 == -verify_arch && $3 == x86_64 ]] || exit 99
        ;;
    *) exit 99 ;;
esac
'''


class HostedSigningBudgetTest(unittest.TestCase):
    def invoke(self, overrides: dict[str, str], status: int = 0) -> tuple[int, dict | None]:
        with tempfile.TemporaryDirectory(prefix="buster-ios-signing-") as temporary:
            root = Path(temporary)
            (root / "ios").mkdir()
            (root / "bin").mkdir()
            shutil.copyfile(ROOT / "ios/test_ci.sh", root / "ios/test_ci.sh")
            tool = root / "bin/fake-tool"
            tool.write_text(FAKE_TOOL, encoding="utf-8")
            tool.chmod(0o755)
            for name in ("cmake", "xcrun", "lipo"):
                (root / "bin" / name).symlink_to(tool)
            observer = root / "observe.py"
            observer.write_text(
                "import json, os, sys\n"
                "from pathlib import Path\n"
                f"names = {WATCHED!r}\n"
                "Path(os.environ['BUSTER_SIGNING_TEST_RECORD']).write_text(json.dumps({\n"
                "    'environment': {name: os.environ.get(name) for name in names},\n"
                "    'arguments': sys.argv[1:],\n"
                "}))\n"
                "sys.exit(int(os.environ['BUSTER_SIGNING_TEST_STATUS']))\n",
                encoding="utf-8",
            )
            (root / "ios/launch_simulator.sh").write_text(
                '#!/usr/bin/env bash\nset -euo pipefail\n'
                'exec python3 "$BUSTER_SIGNING_TEST_OBSERVER" "$@"\n',
                encoding="utf-8",
            )
            # The wrapper must be exercised independently of a developer's SDK,
            # CI flags and timeout overrides. Explicit rows below supply them.
            env = {
                name: value for name, value in os.environ.items()
                if not name.startswith(("BUSTER_IOS_", "GITHUB_", "RUNNER_"))
            }
            record = root / "observed.json"
            env.update({
                "PATH": str(root / "bin") + os.pathsep + env.get("PATH", ""),
                "BUSTER_IOS_BUILD_DIRECTORY": str(root / "build with spaces"),
                "BUSTER_SIGNING_TEST_OBSERVER": str(observer),
                "BUSTER_SIGNING_TEST_RECORD": str(record),
                "BUSTER_SIGNING_TEST_STATUS": str(status),
            })
            env.update(overrides)
            result = subprocess.run(
                ["bash", str(root / "ios/test_ci.sh"), "--all"],
                cwd=root, env=env, text=True, capture_output=True, timeout=15, check=False,
            )
            self.assertNotEqual(result.returncode, 99, result.stdout + result.stderr)
            return result.returncode, json.loads(record.read_text()) if record.exists() else None

    def test_scoped_default_and_explicit_overrides(self) -> None:
        rows = [
            ("hosted-arm64", HOSTED, "180", "300"),
            ("empty-is-unspecified", {**HOSTED, WATCHED[0]: ""}, "180", "300"),
            ("explicit-short", {**HOSTED, WATCHED[0]: "1"}, "1", "300"),
            ("explicit-long", {**HOSTED, WATCHED[0]: "240"}, "240", "300"),
            ("explicit-zero", {**HOSTED, WATCHED[0]: "0"}, "0", "300"),
            ("explicit-invalid", {**HOSTED, WATCHED[0]: "bad"}, "bad", "300"),
            ("local-arm64", {"BUSTER_IOS_ARCH": "arm64"}, None, None),
            ("self-hosted-arm64", {**HOSTED, "RUNNER_ENVIRONMENT": "self-hosted"}, None, "300"),
            ("unknown-runner", {**HOSTED, "RUNNER_ENVIRONMENT": ""}, None, "300"),
            ("not-github", {**HOSTED, "GITHUB_ACTIONS": "false"}, None, None),
            ("not-macos", {**HOSTED, "RUNNER_OS": "Linux"}, None, "300"),
            ("local-intel", {"BUSTER_IOS_ARCH": "x86_64"}, None, None),
        ]
        for name, environment, signing, launch in rows:
            with self.subTest(case=name):
                status, observation = self.invoke(environment)
                self.assertEqual(status, 0)
                self.assertIsNotNone(observation)
                assert observation is not None
                observed = observation["environment"]
                self.assertEqual(observed[WATCHED[0]], signing)
                self.assertEqual(observed[WATCHED[1]], launch)
                for untouched in WATCHED[2:]:
                    self.assertIsNone(observed[untouched])
                self.assertEqual(observation["arguments"][::2], ["--batch", observation["arguments"][2], observation["arguments"][4]])
                self.assertEqual(observation["arguments"][1::2], ["Debug", "Release"])

    def test_explicit_unrelated_deadlines_are_preserved(self) -> None:
        overrides = {**HOSTED, **{name: str(41 + index) for index, name in enumerate(WATCHED)}}
        status, observation = self.invoke(overrides)
        self.assertEqual(status, 0)
        self.assertIsNotNone(observation)
        assert observation is not None
        self.assertEqual(observation["environment"], {name: overrides[name] for name in WATCHED})

    def test_native_failure_and_interruption_are_not_masked(self) -> None:
        for expected in (7, 124, 130, 143):
            with self.subTest(status=expected):
                actual, observation = self.invoke(HOSTED, status=expected)
                self.assertEqual(actual, expected)
                self.assertIsNotNone(observation)

    def test_hosted_intel_compile_only_boundary_is_unchanged(self) -> None:
        status, observation = self.invoke({**HOSTED, "BUSTER_IOS_ARCH": "x86_64"})
        self.assertEqual(status, 0)
        self.assertIsNone(observation)


SHUTDOWN_TEST_UDID = "00000000-0000-0000-0000-000000000739"
REPLACEMENT_TEST_UDID = "00000000-0000-0000-0000-000000000740"
SHUTDOWN_FAKE_TOOL = r'''#!/usr/bin/env bash
set -euo pipefail

tool=${0##*/}
state=${BUSTER_SHUTDOWN_TEST_STATE:?BUSTER_SHUTDOWN_TEST_STATE is required}
log=${BUSTER_SHUTDOWN_TEST_LOG:?BUSTER_SHUTDOWN_TEST_LOG is required}
case "$tool" in
    codesign)
        exit 0
        ;;
    xcodebuild)
        if [[ ${1:-} == -version ]]; then
            printf 'Xcode fake\nBuild version fake\n'
            exit 0
        fi
        exit 97
        ;;
    xcrun)
        printf '%s\n' "$*" >>"$log"
        if [[ ${1:-} == --sdk ]]; then
            if [[ ${2:-} == iphonesimulator && ${3:-} == --show-sdk-version ]]; then
                printf '26.5\n'
                exit 0
            fi
            exit 97
        fi
        [[ ${1:-} == simctl ]] || exit 97
        shift
        command=${1:-}
        shift || true
        case "$command" in
            list)
                if [[ ${1:-} == devices ]]; then
                    if [[ -f $state/shutdown-transition && " $* " == *" -j "* ]]; then
                        shutdown_count=0
                        if [[ -f $state/shutdown-count ]]; then
                            read -r shutdown_count <"$state/shutdown-count"
                        fi
                        probe_sleep=${BUSTER_SHUTDOWN_TEST_PROBE_SLEEP_SECONDS:-0}
                        probe_status=${BUSTER_SHUTDOWN_TEST_PROBE_STATUS:-0}
                        probe_json=${BUSTER_SHUTDOWN_TEST_JSON:-}
                        if [[ $shutdown_count -ge 2 ]]; then
                            probe_sleep=${BUSTER_SHUTDOWN_TEST_RECOVERY_PROBE_SLEEP_SECONDS:-0}
                            probe_status=${BUSTER_SHUTDOWN_TEST_RECOVERY_PROBE_STATUS:-0}
                            probe_json=${BUSTER_SHUTDOWN_TEST_RECOVERY_JSON:-}
                        fi
                        sleep "$probe_sleep"
                        if [[ $probe_status -ne 0 ]]; then
                            exit "$probe_status"
                        fi
                        if [[ -n $probe_json ]]; then
                            printf '%s
' "$probe_json"
                        else
                            post_udid=$(cat "$state/shutdown-transition")
                            post_state=$(cat "$state/shutdown-state")
                            printf '{"devices":{"com.apple.CoreSimulator.SimRuntime.iOS-26-5":[{"name":"buster-ci","udid":"%s","isAvailable":true,"state":"%s"}]}}
' \
                                "$post_udid" "$post_state"
                        fi
                    elif [[ " $* " == *" -j "* ]]; then
                        if [[ ${BUSTER_SHUTDOWN_TEST_BORROWED:-0} == 1 ]]; then
                            printf '{"devices":{"com.apple.CoreSimulator.SimRuntime.iOS-26-5":[{"name":"buster-ci","udid":"%s","isAvailable":true,"state":"Shutdown"}]}}\n' \
                                "$BUSTER_SHUTDOWN_TEST_UDID"
                        else
                            printf '{"devices":{}}\n'
                        fi
                    else
                        printf 'fake iOS simulator devices\n'
                    fi
                elif [[ ${1:-} == runtimes ]]; then
                    if [[ " $* " == *" -j "* ]]; then
                        printf '%s\n' '{"runtimes":[{"identifier":"com.apple.CoreSimulator.SimRuntime.iOS-26-5","version":"26.5","isAvailable":true,"supportedDeviceTypes":[{"identifier":"com.apple.CoreSimulator.SimDeviceType.iPhone-17-Pro","name":"iPhone 17 Pro","productFamily":"iPhone"}]}]}'
                    else
                        printf 'fake iOS simulator runtimes\n'
                    fi
                else
                    exit 97
                fi
                ;;
            create)
                create_count=0
                if [[ -f $state/create-count ]]; then
                    read -r create_count <"$state/create-count"
                fi
                create_count=$((create_count + 1))
                printf '%s\n' "$create_count" >"$state/create-count"
                if [[ $create_count -eq 1 ]]; then
                    printf '%s\n' "$BUSTER_SHUTDOWN_TEST_UDID"
                else
                    printf '%s\n' "$BUSTER_BOOT_TEST_REPLACEMENT_UDID"
                fi
                ;;
            delete|boot|install)
                ;;
            bootstatus)
                boot_count=0
                if [[ -f $state/bootstatus-count ]]; then
                    read -r boot_count <"$state/bootstatus-count"
                fi
                boot_count=$((boot_count + 1))
                printf '%s\n' "$boot_count" >"$state/bootstatus-count"
                case ${BUSTER_BOOT_TEST_MODE:-success} in
                    first-timeout|continuation-native-124)
                        if [[ $boot_count -eq 1 ]]; then
                            printf 'readiness still migrating\n'
                            sleep 60
                        elif [[ ${BUSTER_BOOT_TEST_MODE:-} == continuation-native-124 ]]; then
                            exit 124
                        fi
                        ;;
                    first-device-timeout)
                        if [[ ${1:-} == "$BUSTER_SHUTDOWN_TEST_UDID" ]]; then
                            printf 'readiness still migrating\n'
                            sleep 60
                        fi
                        ;;
                    always-timeout)
                        printf 'readiness still migrating\n'
                        sleep 60
                        ;;
                    *) ;;
                esac
                printf 'Device booted\n'
                ;;
            launch)
                if [[ ${BUSTER_SHUTDOWN_TEST_APP_RESULT:-success} == failure ]]; then
                    printf 'BUSTER_IOS_RESULT: FAILURE\n'
                else
                    printf 'BUSTER_IOS_RESULT: SUCCESS\n'
                fi
                ;;
            spawn)
                printf 'fake simulator diagnostic\n'
                ;;
            shutdown)
                shutdown_count=0
                if [[ -f $state/shutdown-count ]]; then
                    read -r shutdown_count <"$state/shutdown-count"
                fi
                shutdown_count=$((shutdown_count + 1))
                printf '%s
' "$shutdown_count" >"$state/shutdown-count"
                shutdown_mode=${BUSTER_SHUTDOWN_TEST_SHUTDOWN_MODE:-timeout}
                shutdown_state=${BUSTER_SHUTDOWN_TEST_POST_STATE:-Shutdown}
                if [[ $shutdown_count -ge 2 ]]; then
                    shutdown_mode=${BUSTER_SHUTDOWN_TEST_RECOVERY_MODE:-success}
                    shutdown_state=${BUSTER_SHUTDOWN_TEST_RECOVERY_POST_STATE:-Shutdown}
                fi
                case "$shutdown_mode" in
                    success)
                        printf '%s
' "${1:-}" >"$state/shutdown-transition"
                        printf '%s
' "$shutdown_state" >"$state/shutdown-state"
                        : >"$state/shutdown"
                        ;;
                    reject)
                        exit 9
                        ;;
                    numeric-124)
                        exit 124
                        ;;
                    timeout)
                        printf '%s
' "${1:-}" >"$state/shutdown-transition"
                        printf '%s
' "$shutdown_state" >"$state/shutdown-state"
                        sleep 60
                        : >"$state/shutdown"
                        ;;
                    *) exit 97 ;;
                esac
                ;;
            *) exit 97 ;;
        esac
        ;;
    *) exit 97 ;;
esac
'''


class ShutdownPostconditionTest(unittest.TestCase):
    def invoke(
        self,
        *,
        shutdown_mode: str = "timeout",
        app_result: str = "success",
        post_state: str = "Shutdown",
        post_json: str | None = None,
        probe_status: int = 0,
        probe_sleep: int = 0,
        recovery_mode: str = "success",
        recovery_post_state: str = "Shutdown",
        recovery_json: str | None = None,
        recovery_probe_status: int = 0,
        recovery_probe_sleep: int = 0,
        hosted: bool = True,
        explicit: bool = False,
        borrowed: bool = False,
    ) -> dict[str, object]:
        with tempfile.TemporaryDirectory(prefix="buster-ios-shutdown-") as temporary:
            state = Path(temporary)
            fake_bin = state / "bin"
            app = state / "Debug" / "ide.app"
            fake_bin.mkdir()
            app.mkdir(parents=True)
            tool = fake_bin / "fake-tool"
            tool.write_text(SHUTDOWN_FAKE_TOOL, encoding="utf-8")
            tool.chmod(0o755)
            for name in ("codesign", "xcodebuild", "xcrun"):
                (fake_bin / name).symlink_to(tool)
            log = state / "xcrun.log"
            log.write_text("", encoding="utf-8")
            env = {
                name: value
                for name, value in os.environ.items()
                if not name.startswith(
                    ("BUSTER_IOS_", "BUSTER_SHUTDOWN_TEST_", "GITHUB_", "RUNNER_")
                )
            }
            console = state / "console.log"
            env.update(
                {
                    "PATH": str(fake_bin) + os.pathsep + env.get("PATH", ""),
                    "RUNNER_TEMP": str(state),
                    "BUSTER_IOS_ARCH": "arm64",
                    "BUSTER_IOS_CONSOLE_LOG": str(console),
                    "BUSTER_IOS_BOOT_TIMEOUT_SECONDS": "2",
                    "BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS": "2",
                    "BUSTER_IOS_INSTALL_TIMEOUT_SECONDS": "2",
                    "BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS": "3",
                    "BUSTER_IOS_SHUTDOWN_TIMEOUT_SECONDS": "1",
                    "BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS": "1",
                    "BUSTER_SHUTDOWN_TEST_STATE": str(state),
                    "BUSTER_SHUTDOWN_TEST_LOG": str(log),
                    "BUSTER_SHUTDOWN_TEST_UDID": SHUTDOWN_TEST_UDID,
                    "BUSTER_SHUTDOWN_TEST_SHUTDOWN_MODE": shutdown_mode,
                    "BUSTER_SHUTDOWN_TEST_APP_RESULT": app_result,
                    "BUSTER_SHUTDOWN_TEST_POST_STATE": post_state,
                    "BUSTER_SHUTDOWN_TEST_PROBE_STATUS": str(probe_status),
                    "BUSTER_SHUTDOWN_TEST_PROBE_SLEEP_SECONDS": str(probe_sleep),
                    "BUSTER_SHUTDOWN_TEST_RECOVERY_MODE": recovery_mode,
                    "BUSTER_SHUTDOWN_TEST_RECOVERY_POST_STATE": recovery_post_state,
                    "BUSTER_SHUTDOWN_TEST_RECOVERY_PROBE_STATUS": str(recovery_probe_status),
                    "BUSTER_SHUTDOWN_TEST_RECOVERY_PROBE_SLEEP_SECONDS": str(recovery_probe_sleep),
                    "BUSTER_SHUTDOWN_TEST_BORROWED": "1" if borrowed else "0",
                }
            )
            if hosted:
                env.update(
                    {
                        "GITHUB_ACTIONS": "true",
                        "RUNNER_ENVIRONMENT": "github-hosted",
                        "RUNNER_OS": "macOS",
                        "RUNNER_ARCH": "ARM64",
                    }
                )
            else:
                env["GITHUB_ACTIONS"] = "false"
            if explicit:
                env["BUSTER_IOS_SIMULATOR_UDID"] = SHUTDOWN_TEST_UDID
            if post_json is not None:
                env["BUSTER_SHUTDOWN_TEST_JSON"] = post_json
            if recovery_json is not None:
                env["BUSTER_SHUTDOWN_TEST_RECOVERY_JSON"] = recovery_json
            result = subprocess.run(
                [
                    "bash",
                    str(ROOT / "ios/launch_simulator.sh"),
                    "--batch",
                    "Debug",
                    str(app),
                ],
                cwd=ROOT,
                env=env,
                text=True,
                capture_output=True,
                timeout=20,
                check=False,
            )
            shutdown_log = Path(str(console) + ".shutdown.status.log")
            postcondition_log = Path(
                str(console) + ".shutdown-postcondition.result.log"
            )
            recovery_log = Path(str(console) + ".shutdown-recovery.result.log")
            recovery_postcondition_log = Path(
                str(console) + ".shutdown-recovery-postcondition.result.log"
            )
            return {
                "status": result.returncode,
                "output": result.stdout + result.stderr,
                "commands": log.read_text(encoding="utf-8").splitlines(),
                "shutdown": shutdown_log.read_text(encoding="utf-8")
                if shutdown_log.exists()
                else "",
                "postcondition": postcondition_log.read_text(encoding="utf-8")
                if postcondition_log.exists()
                else "",
                "recovery": recovery_log.read_text(encoding="utf-8")
                if recovery_log.exists()
                else "",
                "recovery_postcondition": recovery_postcondition_log.read_text(
                    encoding="utf-8"
                )
                if recovery_postcondition_log.exists()
                else "",
            }

    def test_direct_and_verified_shutdown_are_distinct_successes(self) -> None:
        direct = self.invoke(shutdown_mode="success")
        self.assertEqual(direct["status"], 0, direct["output"])
        self.assertIn("shutdown_outcome=success", direct["shutdown"])
        self.assertIn("shutdown_disposition=direct-success", direct["shutdown"])
        self.assertEqual(direct["postcondition"], "")

        verified = self.invoke()
        self.assertEqual(verified["status"], 0, verified["output"])
        self.assertIn("shutdown_status=124", verified["shutdown"])
        self.assertIn("shutdown_outcome=timeout", verified["shutdown"])
        self.assertIn(
            "shutdown_disposition=verified-shutdown-after-timeout",
            verified["shutdown"],
        )
        self.assertIn("result_status=0", verified["shutdown"])
        self.assertIn("state=Shutdown", verified["postcondition"])
        self.assertEqual(verified["commands"].count("simctl list devices -j"), 1)

    def test_verified_cleanup_preserves_an_application_failure(self) -> None:
        result = self.invoke(app_result="failure")
        self.assertEqual(result["status"], 1, result["output"])
        self.assertIn("prior_status=1", result["shutdown"])
        self.assertIn(
            "shutdown_disposition=verified-shutdown-after-timeout",
            result["shutdown"],
        )
        self.assertIn("result_status=1", result["shutdown"])

    def test_non_shutdown_postcondition_recovers_once(self) -> None:
        result = self.invoke(post_state="Booted")
        self.assertEqual(result["status"], 0, result["output"])
        self.assertIn("shutdown_status=124", result["shutdown"])
        self.assertIn("shutdown_outcome=timeout", result["shutdown"])
        self.assertIn("postcondition_state=Shutdown", result["shutdown"])
        self.assertIn(
            "shutdown_disposition=recovered-shutdown-after-timeout",
            result["shutdown"],
        )
        self.assertIn("state=non-shutdown", result["postcondition"])
        self.assertIn("initial_state=non-shutdown", result["recovery"])
        self.assertIn("retry_status=0", result["recovery"])
        self.assertIn("retry_outcome=success", result["recovery"])
        self.assertIn("final_state=Shutdown", result["recovery"])
        self.assertIn(
            "disposition=recovered-shutdown-after-timeout",
            result["recovery"],
        )
        self.assertIn("state=Shutdown", result["recovery_postcondition"])
        self.assertEqual(
            result["commands"].count("simctl shutdown " + SHUTDOWN_TEST_UDID),
            2,
        )
        self.assertEqual(result["commands"].count("simctl list devices -j"), 2)

    def test_timed_out_retry_can_be_verified_by_final_state(self) -> None:
        result = self.invoke(
            post_state="Booted",
            recovery_mode="timeout",
            recovery_post_state="Shutdown",
        )
        self.assertEqual(result["status"], 0, result["output"])
        self.assertIn("retry_status=124", result["recovery"])
        self.assertIn("retry_outcome=timeout", result["recovery"])
        self.assertIn("final_state=Shutdown", result["recovery"])
        self.assertIn(
            "shutdown_disposition=recovered-shutdown-after-timeout",
            result["shutdown"],
        )

    def test_non_shutdown_application_failure_is_not_retried(self) -> None:
        result = self.invoke(app_result="failure", post_state="Booted")
        self.assertEqual(result["status"], 1, result["output"])
        self.assertIn("prior_status=1", result["shutdown"])
        self.assertIn(
            "shutdown_disposition=unresolved-failure",
            result["shutdown"],
        )
        self.assertEqual(result["recovery"], "")
        self.assertEqual(
            result["commands"].count("simctl shutdown " + SHUTDOWN_TEST_UDID),
            1,
        )

    def test_shutdown_recovery_failures_remain_failed(self) -> None:
        target = SHUTDOWN_TEST_UDID
        other = "00000000-0000-0000-0000-000000000001"
        cases = [
            ("retry-reject", {"recovery_mode": "reject"}),
            ("retry-native-124", {"recovery_mode": "numeric-124"}),
            (
                "retry-timeout-still-booted",
                {
                    "recovery_mode": "timeout",
                    "recovery_post_state": "Booted",
                },
            ),
            ("final-booted", {"recovery_post_state": "Booted"}),
            ("final-booting", {"recovery_post_state": "Booting"}),
            (
                "final-shutting-down",
                {"recovery_post_state": "Shutting Down"},
            ),
            ("final-probe-reject", {"recovery_probe_status": 9}),
            ("final-probe-timeout", {"recovery_probe_sleep": 60}),
            ("final-malformed", {"recovery_json": "{"}),
            ("final-missing", {"recovery_json": '{"devices":{}}'}),
            (
                "final-other",
                {
                    "recovery_json": '{"devices":{"runtime":[{"udid":"'
                    + other
                    + '","state":"Shutdown"}]}}'
                },
            ),
            (
                "final-duplicate",
                {
                    "recovery_json": '{"devices":{"runtime":[{"udid":"'
                    + target
                    + '","state":"Shutdown"},{"udid":"'
                    + target
                    + '","state":"Shutdown"}]}}'
                },
            ),
        ]
        for name, arguments in cases:
            with self.subTest(case=name):
                result = self.invoke(post_state="Booted", **arguments)
                self.assertEqual(result["status"], 1, result["output"])
                self.assertIn(
                    "shutdown_disposition=unresolved-failure",
                    result["shutdown"],
                )
                self.assertIn("result_status=1", result["shutdown"])
                self.assertNotIn(
                    "disposition=recovered-shutdown-after-timeout",
                    result["recovery"],
                )
                self.assertEqual(
                    result["commands"].count(
                        "simctl shutdown " + SHUTDOWN_TEST_UDID
                    ),
                    2,
                )

    def test_unresolved_postconditions_fail_closed(self) -> None:
        target = SHUTDOWN_TEST_UDID
        other = "00000000-0000-0000-0000-000000000001"
        cases = [
            ("missing", {"post_json": '{"devices":{}}'}),
            (
                "other-device",
                {
                    "post_json": '{"devices":{"runtime":[{"udid":"'
                    + other
                    + '","state":"Shutdown"}]}}'
                },
            ),
            (
                "duplicate",
                {
                    "post_json": '{"devices":{"runtime":[{"udid":"'
                    + target
                    + '","state":"Shutdown"},{"udid":"'
                    + target
                    + '","state":"Shutdown"}]}}'
                },
            ),
            ("malformed", {"post_json": "{"}),
            (
                "missing-state",
                {
                    "post_json": '{"devices":{"runtime":[{"udid":"'
                    + target
                    + '"}]}}'
                },
            ),
            ("probe-reject", {"probe_status": 9}),
            ("probe-timeout", {"probe_sleep": 60}),
        ]
        for name, arguments in cases:
            with self.subTest(case=name):
                result = self.invoke(**arguments)
                self.assertEqual(result["status"], 1, result["output"])
                self.assertIn(
                    "shutdown_disposition=unresolved-failure",
                    result["shutdown"],
                )
                self.assertIn("result_status=1", result["shutdown"])
                self.assertNotIn(
                    "verified-shutdown-after-timeout",
                    result["postcondition"],
                )

    def test_non_timeout_and_ineligible_devices_are_not_reconciled(self) -> None:
        cases = [
            ("reject", {"shutdown_mode": "reject"}, "1"),
            ("numeric-124", {"shutdown_mode": "numeric-124"}, "1"),
            ("explicit", {"explicit": True}, "0"),
            ("local", {"hosted": False}, "0"),
            ("borrowed", {"borrowed": True}, "0"),
        ]
        for name, arguments, eligibility in cases:
            with self.subTest(case=name):
                result = self.invoke(**arguments)
                self.assertEqual(result["status"], 1, result["output"])
                self.assertIn(
                    "postcondition_eligibility=" + eligibility,
                    result["shutdown"],
                )
                self.assertIn(
                    "shutdown_disposition=unresolved-failure",
                    result["shutdown"],
                )
                self.assertEqual(result["postcondition"], "")
                self.assertNotIn(
                    "simctl list devices -j", result["commands"]
                )


class BootReadinessContinuationTest(unittest.TestCase):
    """Exercise the real launcher with an invocation-owned fake simulator."""

    def invoke(
        self, mode: str, *, continuation: bool = True,
        hosted: bool = True, explicit: bool = False, empty_capture: bool = False,
    ) -> dict[str, object]:
        with tempfile.TemporaryDirectory(prefix="buster-ios-readiness-") as temporary:
            state = Path(temporary)
            fake_bin = state / "bin"
            fake_bin.mkdir()
            tool = fake_bin / "fake-tool"
            tool.write_text(SHUTDOWN_FAKE_TOOL, encoding="utf-8")
            tool.chmod(0o755)
            for name in ("codesign", "xcodebuild", "xcrun"):
                (fake_bin / name).symlink_to(tool)
            if empty_capture:
                python = fake_bin / "python3"
                python.write_text(
                    '#!/usr/bin/env bash\n'
                    'if [[ ${1:-} == -c && ${2:-} == *"remaining = 65536"* ]]; then\n'
                    '    path=${@: -1}\n'
                    '    if [[ $path == *.bootstatus.log ]]; then\n'
                    '        : >"$path"\n'
                    '        : >"${path}.capture-status.log"\n'
                    '        cat >/dev/null\n'
                    '        exit 0\n'
                    '    fi\n'
                    'fi\n'
                    'exec "$BUSTER_BOOT_TEST_REAL_PYTHON" "$@"\n',
                    encoding="utf-8",
                )
                python.chmod(0o755)
            apps = []
            for label in ("Debug", "Release"):
                app = state / label / "ide.app"
                app.mkdir(parents=True)
                apps.extend((label, str(app)))
            console = state / "console.log"
            commands = state / "xcrun.log"
            commands.write_text("", encoding="utf-8")
            env = {
                name: value for name, value in os.environ.items()
                if not name.startswith(("BUSTER_IOS_", "BUSTER_BOOT_TEST_", "BUSTER_SHUTDOWN_TEST_", "GITHUB_", "RUNNER_"))
            }
            env.update({
                "PATH": str(fake_bin) + os.pathsep + env.get("PATH", ""),
                "RUNNER_TEMP": str(state),
                "BUSTER_IOS_ARCH": "arm64",
                "BUSTER_IOS_CONSOLE_LOG": str(console),
                "BUSTER_IOS_BOOT_TIMEOUT_SECONDS": "1",
                "BUSTER_IOS_CODESIGN_TIMEOUT_SECONDS": "2",
                "BUSTER_IOS_INSTALL_TIMEOUT_SECONDS": "2",
                "BUSTER_IOS_LAUNCH_TIMEOUT_SECONDS": "3",
                "BUSTER_IOS_SHUTDOWN_TIMEOUT_SECONDS": "2",
                "BUSTER_IOS_MONITOR_COMMAND_TIMEOUT_SECONDS": "1",
                "BUSTER_SHUTDOWN_TEST_STATE": str(state),
                "BUSTER_SHUTDOWN_TEST_LOG": str(commands),
                "BUSTER_SHUTDOWN_TEST_UDID": SHUTDOWN_TEST_UDID,
                "BUSTER_SHUTDOWN_TEST_SHUTDOWN_MODE": "success",
                "BUSTER_BOOT_TEST_REPLACEMENT_UDID": REPLACEMENT_TEST_UDID,
                "BUSTER_BOOT_TEST_MODE": mode,
            })
            if empty_capture:
                env["BUSTER_BOOT_TEST_REAL_PYTHON"] = shutil.which("python3") or "python3"
            if continuation:
                env["BUSTER_IOS_BOOT_CONTINUATION_SECONDS"] = "1"
            if hosted:
                env.update({
                    "GITHUB_ACTIONS": "true",
                    "RUNNER_ENVIRONMENT": "github-hosted",
                    "RUNNER_OS": "macOS",
                    "RUNNER_ARCH": "ARM64",
                })
            else:
                env["GITHUB_ACTIONS"] = "false"
            if explicit:
                env["BUSTER_IOS_SIMULATOR_UDID"] = SHUTDOWN_TEST_UDID
            result = subprocess.run(
                ["bash", str(ROOT / "ios/launch_simulator.sh"), "--batch", *apps],
                cwd=ROOT, env=env, text=True, capture_output=True, timeout=35, check=False,
            )
            evidence = {
                path.name.removeprefix("console.log."): path.read_text(encoding="utf-8")
                for path in state.glob("console*") if path.is_file()
            }
            return {
                "status": result.returncode,
                "output": result.stdout + result.stderr,
                "commands": commands.read_text(encoding="utf-8").splitlines(),
                "evidence": evidence,
            }

    def test_first_timeout_continues_on_same_device_and_runs_both_apps(self) -> None:
        result = self.invoke("first-timeout")
        self.assertEqual(result["status"], 0, result["output"])
        commands = result["commands"]
        evidence = result["evidence"]
        self.assertEqual(sum(line.startswith("simctl create ") for line in commands), 1)
        self.assertEqual(commands.count(f"simctl bootstatus {SHUTDOWN_TEST_UDID} -b"), 2)
        self.assertEqual(commands.count(f"simctl shutdown {SHUTDOWN_TEST_UDID}"), 1)
        self.assertNotIn(f"simctl delete {SHUTDOWN_TEST_UDID}", commands)
        self.assertIn("outcome=success-after-continuation", result["output"])
        self.assertIn("BUSTER_IOS_BOOT_DISPOSITION=continued-original-boot-success", result["output"])
        self.assertIn("outcome=timeout", evidence["boot.attempt-1.bootstatus.status.log"])
        self.assertIn("capture_receipt=complete", evidence["boot.attempt-1.bootstatus.status.log"])
        self.assertIn(f"SIMULATOR_UDID={SHUTDOWN_TEST_UDID}", evidence["boot.attempt-1.bootstatus.lifecycle-context.log"])
        self.assertIn("SIMULATOR_RUNTIME=com.apple.CoreSimulator.SimRuntime.iOS-26-5", evidence["boot.attempt-1.bootstatus.lifecycle-context.log"])
        for label in ("Debug", "Release"):
            self.assertTrue(any(line.startswith(f"simctl install {SHUTDOWN_TEST_UDID} ") and f"/{label}/" in line for line in commands))
            self.assertIn("BUSTER_IOS_RESULT: SUCCESS", evidence[f"console.{label}.log"])

    def test_both_devices_timeout_without_running_apps(self) -> None:
        result = self.invoke("always-timeout")
        self.assertEqual(result["status"], 1, result["output"])
        commands = result["commands"]
        evidence = result["evidence"]
        self.assertEqual(commands.count(f"simctl bootstatus {SHUTDOWN_TEST_UDID} -b"), 2)
        self.assertEqual(commands.count(f"simctl bootstatus {REPLACEMENT_TEST_UDID} -b"), 1)
        self.assertEqual(sum(line.startswith("simctl create ") for line in commands), 2)
        self.assertNotIn("simctl install", "\n".join(commands))
        self.assertIn("BUSTER_IOS_BOOT_DISPOSITION=unrecovered-failure", result["output"])
        self.assertIn(f"SIMULATOR_UDID={REPLACEMENT_TEST_UDID}", evidence["boot.attempt-2.bootstatus.lifecycle-context.log"])
        self.assertIn("capture_receipt=complete", evidence["boot.attempt-2.bootstatus.status.log"])

    def test_native_124_on_continuation_does_not_replace(self) -> None:
        result = self.invoke("continuation-native-124")
        self.assertEqual(result["status"], 124, result["output"])
        commands = result["commands"]
        self.assertEqual(commands.count(f"simctl bootstatus {SHUTDOWN_TEST_UDID} -b"), 2)
        self.assertEqual(sum(line.startswith("simctl create ") for line in commands), 1)
        self.assertNotIn("simctl install", "\n".join(commands))
        self.assertIn(
            "outcome=command-failure status=124 native_status=124",
            result["evidence"]["boot.attempt-1.bootstatus-continue.status.log"],
        )

    def test_shortened_deadline_without_opt_in_keeps_single_replacement(self) -> None:
        result = self.invoke("first-timeout", continuation=False)
        self.assertEqual(result["status"], 0, result["output"])
        commands = result["commands"]
        self.assertEqual(commands.count(f"simctl bootstatus {SHUTDOWN_TEST_UDID} -b"), 1)
        self.assertEqual(commands.count(f"simctl bootstatus {REPLACEMENT_TEST_UDID} -b"), 1)
        self.assertNotIn("BUSTER_IOS_BOOT_CONTINUATION", result["output"])
        self.assertIn("BUSTER_IOS_BOOT_DISPOSITION=recovered-infrastructure-failure", result["output"])

    def test_explicit_and_local_devices_do_not_continue_or_replace(self) -> None:
        for name, overrides in (("explicit", {"explicit": True}), ("local", {"hosted": False})):
            with self.subTest(case=name):
                result = self.invoke("always-timeout", **overrides)
                self.assertEqual(result["status"], 124, result["output"])
                commands = result["commands"]
                self.assertEqual(commands.count(f"simctl bootstatus {SHUTDOWN_TEST_UDID} -b"), 1)
                self.assertNotIn("BUSTER_IOS_BOOT_CONTINUATION", result["output"])
                self.assertNotIn("simctl install", "\n".join(commands))

    def test_empty_capture_receipt_is_reported_and_cannot_run_apps(self) -> None:
        result = self.invoke("always-timeout", explicit=True, empty_capture=True)
        self.assertEqual(result["status"], 124, result["output"])
        phase = result["evidence"]["boot.attempt-1.bootstatus.status.log"]
        self.assertIn("capture_status=0", phase)
        self.assertIn("capture_receipt=incomplete", phase)
        self.assertIn("BUSTER_IOS_CAPTURE incomplete=1 reason=missing-or-empty-receipt", phase)
        self.assertNotIn("simctl install", "\n".join(result["commands"]))


if __name__ == "__main__":
    unittest.main(verbosity=2)
