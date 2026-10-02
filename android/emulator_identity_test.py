#!/usr/bin/env python3
"""Independent identity fixtures and cross-invocation Android cleanup controls.

run_tests_test.sh owns SDK/workflow fixtures; this file owns real child processes.
--write-marker supplies identities for its unreaped-child regressions.
"""

import os
from pathlib import Path
import platform
import signal
import subprocess
import sys
import tempfile


def process_identity(pid, portable=False):
    if platform.system() == "Linux" and not portable:
        fields = Path(f"/proc/{pid}/stat").read_text().rsplit(") ", 1)[1].split()
        boot_id = Path("/proc/sys/kernel/random/boot_id").read_text().strip()
        identity = f"linux:{boot_id}:{fields[19]}"
    else:
        result = subprocess.run(["ps", "-o", "lstart=", "-p", str(pid)],
                                check=True, capture_output=True, text=True,
                                env={**os.environ, "LC_ALL": "C"})
        identity = "ps:" + result.stdout.strip()
    return identity


def write_marker(pid, marker, portable=False):
    marker = Path(marker)
    Path(str(marker) + ".identity").write_text(f"{pid} {process_identity(pid, portable)}\n")
    marker.write_text(f"{pid}\n")


def run_controls():
    script = Path(__file__).with_name("start_emulator_ci.sh")
    evidence = os.environ.get("BUSTER_MOBILE_TEST_EVIDENCE_DIR")
    if evidence:
        Path(evidence).mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="android-identity-", dir=evidence) as root:
        root = Path(root)
        # The ready pipe removes the SIGTERM-handler installation race.
        read_fd, write_fd = os.pipe()
        child = os.fork()
        if child == 0:
            os.close(read_fd)
            signal.signal(signal.SIGTERM, signal.SIG_IGN)
            os.write(write_fd, b"1")
            os.close(write_fd)
            signal.pause()
            os._exit(0)
        os.close(write_fd)
        try:
            assert os.read(read_fd, 1) == b"1"
            os.close(read_fd)
            marker = root / "emulator.started"
            identity_file = Path(str(marker) + ".identity")
            fake_bin = root / "bin"
            fake_bin.mkdir()
            adb_log = root / "adb.log"
            (fake_bin / "adb").write_text(
                '#!/usr/bin/env bash\nprintf "%s\\n" "$*" >>"$IDENTITY_ADB_LOG"\n')
            (fake_bin / "adb").chmod(0o755)
            env = {**os.environ, "PATH": str(fake_bin) + os.pathsep + os.environ["PATH"],
                   "BUSTER_ANDROID_EMULATOR_STARTED_MARKER": str(marker),
                   "BUSTER_ANDROID_EMULATOR_LOG": str(root / "emulator.log"),
                   "ANDROID_HOME": str(root / "sdk"),
                   "ANDROID_USER_HOME": str(root / "android"),
                   "ANDROID_AVD_HOME": str(root / "avd"),
                   "BUSTER_ANDROID_CLEANUP_TIMEOUT_SECONDS": "1",
                   "IDENTITY_ADB_LOG": str(adb_log)}

            for case in ("mismatch", "missing", "malformed", "survivor", "unavailable"):
                write_marker(child, marker)
                adb_log.unlink(missing_ok=True)
                case_env = env.copy()
                if case == "mismatch":
                    # Same numeric PID, different birth identity: simulate reuse.
                    original = process_identity(child)
                    changed = original[:-1] + ("1" if original[-1] != "1" else "2")
                    identity_file.write_text(f"{child} {changed}\n")
                elif case == "missing":
                    identity_file.unlink()
                elif case == "malformed":
                    identity_file.write_text(f"{child + 1} {process_identity(child)}\n")
                elif case == "survivor":
                    # A refused SIGKILL simulates a genuinely surviving owned task.
                    bash_env = root / "refuse-kill.sh"
                    bash_env.write_text('kill() {\n'
                                        '  if [[ $1 == -KILL ]]; then return 1; fi\n'
                                        '  builtin kill "$@"\n}\n')
                    case_env["BASH_ENV"] = str(bash_env)
                else:
                    # Exercise the portable snapshot's persistent failure with a
                    # real live PID; unknown must fail without adb or signals.
                    write_marker(child, marker, portable=True)
                    (fake_bin / "uname").write_text('#!/bin/sh\necho Darwin\n')
                    (fake_bin / "ps").write_text('#!/bin/sh\nexit 1\n')
                    (fake_bin / "uname").chmod(0o755)
                    (fake_bin / "ps").chmod(0o755)
                result = subprocess.run(["bash", str(script), "stop"], env=case_env,
                                        capture_output=True, text=True, timeout=10)
                log = result.stdout + result.stderr
                print(log, end="")
                expected = 0 if case == "mismatch" else 1
                assert result.returncode == expected, (case, result.returncode, log)
                os.kill(child, 0)
                assert marker.exists() == (expected != 0), (case, log)
                if case == "survivor":
                    assert "sending SIGKILL" in log
                    assert "remains live" in log
                    assert "emu kill" in adb_log.read_text()
                else:
                    assert not adb_log.exists(), (case, adb_log.read_text())
                    assert "sending SIG" not in log
                # A stopped/mismatched record is retired across subsequent calls.
                if case == "mismatch":
                    assert not identity_file.exists()
                    again = subprocess.run(["bash", str(script), "stop"], env=case_env,
                                           capture_output=True, text=True, timeout=10)
                    assert again.returncode == 0
                    assert not adb_log.exists()
                print(f"Android process identity evidence passed: {case}")
        finally:
            os.kill(child, signal.SIGKILL)
            os.waitpid(child, 0)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--write-marker":
        write_marker(int(sys.argv[2]), sys.argv[3], "--portable" in sys.argv[4:])
    else:
        run_controls()
