#!/usr/bin/env python3
"""Exercise the real iOS CI wrapper's signing policy without an SDK or simulator."""

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


if __name__ == "__main__":
    unittest.main(verbosity=2)
