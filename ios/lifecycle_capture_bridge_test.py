#!/usr/bin/env python3
"""Exercise real caller clocks and descriptor custody without a simulator.

BridgeTests runs the selected monitors, the actual Bash collector, and the real owner.
All fixture processes are finite. Cleanup uses live owned Popen handles and
release markers; protocol PID/PGID/SID numbers are never signal targets.
"""

import json
import os
from pathlib import Path
import select
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

import gnu_timeout


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT
HELPER = SOURCE_ROOT / "ios/lifecycle_capture.py"
BRIDGE = SOURCE_ROOT / "ios/lifecycle_capture_bridge.sh"
LAUNCHER = SOURCE_ROOT / "ios/launch_simulator.sh"
TIMEOUT = None
if os.name == "posix" and hasattr(os, "fork"):
    TIMEOUT = gnu_timeout.select_timeout()

FIXTURE = r'''import importlib.util,os,pathlib,signal,subprocess,sys,time
mode,state,*args=sys.argv[1:]
state=pathlib.Path(state)
state.mkdir(parents=True,exist_ok=True)
def mark(name):
    temporary=state/(name+'.tmp')
    temporary.write_text(str(time.monotonic_ns())+'\n')
    temporary.replace(state/name)
if mode=='startup':
    disposition,seconds,*command=args
    signal.signal(signal.SIGINT,signal.SIG_DFL)
    signal.signal(signal.SIGTERM,signal.SIG_DFL)
    signal.pthread_sigmask(signal.SIG_UNBLOCK,{signal.SIGINT,signal.SIGTERM})
    signal.signal(signal.SIGUSR1,signal.SIG_DFL)
    signal.pthread_sigmask(signal.SIG_UNBLOCK,{signal.SIGUSR1})
    if disposition=='ignore':signal.signal(signal.SIGUSR1,signal.SIG_IGN)
    if disposition=='block':signal.pthread_sigmask(signal.SIG_BLOCK,{signal.SIGUSR1})
    mark('startup-entered')
    time.sleep(float(seconds))
    mark('startup-exec-admitted')
    os.execv(sys.executable,[sys.executable,*command])
elif mode=='shim':
    result,delay,close_first,*command=args
    signal.signal(signal.SIGINT,signal.SIG_DFL)
    signal.signal(signal.SIGTERM,signal.SIG_DFL)
    signal.pthread_sigmask(signal.SIG_UNBLOCK,{signal.SIGINT,signal.SIGTERM})
    inherited=[]
    for fd in range(3,8):
        try:os.fstat(fd)
        except OSError:pass
        else:inherited.append(fd)
    os.fstat(5)
    mark('invocation-entered')
    child=subprocess.Popen(command,close_fds=True,pass_fds=tuple(inherited))
    raw=child.wait(timeout=30)
    mark('actual-helper-wait-'+str(raw))
    if close_first=='1':os.close(5);mark('shim-completion-closed')
    time.sleep(float(delay))
    if result=='duplicate':
        generation=command[command.index('--bridge-generation')+1]
        os.write(5,('COMPLETE '+generation+' 0\n').encode())
    if result=='term':
        signal.signal(signal.SIGTERM,signal.SIG_DFL)
        signal.pthread_sigmask(signal.SIG_UNBLOCK,{signal.SIGTERM})
        signal.raise_signal(signal.SIGTERM)
    sys.exit(70 if result=='exit70' else raw if raw>=0 else 128-raw)
elif mode=='early':
    escape,silent,close_after=args
    child=os.fork()
    if child==0:
        if escape=='1':os.setsid()
        signal.signal(signal.SIGTERM,signal.SIG_IGN)
        closed=False
        if silent=='1':os.close(1);os.close(2);closed=True
        mark('writer-ready')
        started=time.monotonic()
        while not (state/'release').exists() and time.monotonic()<started+20:
            if float(close_after)>0 and time.monotonic()>=started+float(close_after):break
            mark('writer-heartbeat')
            time.sleep(.005)
        if not closed:os.close(1);os.close(2)
        mark('writer-ended')
        os._exit(0)
    deadline=time.monotonic()+3
    while not (state/'writer-ready').exists() and time.monotonic()<deadline:time.sleep(.005)
    mark('native-zero')
    os._exit(0 if (state/'writer-ready').exists() else 91)
elif mode=='active':
    os.setsid()
    stopped=[False]
    def term(number,frame):stopped[0]=True
    signal.signal(signal.SIGTERM,term)
    mark('active-native')
    deadline=time.monotonic()+20
    while not stopped[0] and not (state/'release').exists() and time.monotonic()<deadline:time.sleep(.005)
    if stopped[0]:mark('native-term')
    mark('native-ended')
    sys.exit(0 if stopped[0] or (state/'release').exists() else 92)
elif mode in ('delay-done','delay-publication','anchor-guard'):
    if args[:1]==['-S']:
        if args[1:2]!=[EXPECTED_OWNER_HELPER]:raise RuntimeError('unknown no-site owner prefix')
        args=args[1:]
    helper,*command=args
    spec=importlib.util.spec_from_file_location('fixture_owner',helper)
    module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if mode=='delay-done':
        original=module.CallerBridge.done
        def done(bridge):
            if not bridge.done_sent:
                mark('done-delay-entered');time.sleep(2);mark('done-delay-ended')
            original(bridge)
        module.CallerBridge.done=done
    elif mode=='delay-publication':
        original=module.atomic_text
        held=[False]
        def publish(path,text):
            if str(path).endswith('.supervisor-status.log') and not held[0]:
                held[0]=True;mark('publication-delay-entered');time.sleep(13);mark('publication-delay-ended')
            original(path,text)
        module.atomic_text=publish
    else:
        original_observe=module.Supervisor.observe_native
        original_reap=module.Supervisor.reap_keeper
        def observe(owner,now):
            # Observe real pipe EOF before native completion so this control
            # exercises ordinary pending-proof custody, not triggered cleanup.
            owner.drain(now)
            original_observe(owner,now)
        def reap(owner,now):
            if owner.command_terminal and owner.facts['capture_eof'] and not owner.command_proof_final() and owner.term_started is None:
                if not owner.group_authority or owner.release_started or owner.keeper.returncode is not None:
                    mark('forbidden-anchor-release')
                    raise AssertionError('ordinary keeper authority ended before final command proof')
                mark('pending-unreaped-anchor')
            original_reap(owner,now)
        module.Supervisor.observe_native=observe
        module.Supervisor.reap_keeper=reap
    try:result=module.main(command)
    finally:mark('owner-wrapper-ended')
    sys.exit(result)
else:raise RuntimeError('unknown finite fixture')
'''.replace("EXPECTED_OWNER_HELPER", repr(str(HELPER)))


def wait_for_path(path, seconds):
    deadline = time.monotonic() + seconds
    while not path.exists() and time.monotonic() < deadline:
        time.sleep(0.005)
    return path.exists()


def named_receipt(path, tag):
    text = path.read_text()
    tokens = text.split()
    if not tokens or tokens[0] != tag:
        raise AssertionError(text)
    fields = {}
    for token in tokens[1:]:
        key, value = token.split("=", 1)
        if key in fields:
            raise AssertionError("duplicate receipt key: " + key)
        fields[key] = value
    return fields


@unittest.skipUnless(os.name == "posix" and TIMEOUT and hasattr(os, "fork"),
                     "real POSIX/GNU timeout topology required")
class BridgeTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="buster-caller-clock-"))
        self.fixture = self.root / "fixture.py"
        self.fixture.write_text(FIXTURE)
        self.owned = []
        self.release_states = []
        self.required_end_markers = []
        self.descriptors = set()
        self.bootstrap_bounds = []
        self.sequence = 0
        self.assertTrue(HELPER.is_file() and BRIDGE.is_file())

    def tearDown(self):
        for state in self.release_states:
            (state / "release").touch()
        for descriptor in tuple(self.descriptors):
            self.close_descriptor(descriptor)
        for process in self.owned:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    if process.poll() is None:
                        process.kill()
                    process.wait(timeout=3)
        # A cancelled collector can leave its separately grouped finite GNU
        # command monitor to reach N. Do not remove its private paths sooner.
        if self.bootstrap_bounds:
            remaining = max(self.bootstrap_bounds) - time.monotonic()
            if remaining > 0:
                time.sleep(remaining)
        complete = all(wait_for_path(marker, 3) for marker in self.required_end_markers)
        if complete:
            shutil.rmtree(self.root)
        else:
            print("retained finite-custodian evidence: " + str(self.root), file=sys.stderr)
            self.fail("finite custodian did not acknowledge completion; evidence retained at " + str(self.root))

    def python_overlay(self, state, mode, *arguments):
        directory = self.root / ("bin-%d" % self.sequence)
        self.sequence += 1
        directory.mkdir()
        wrapper = directory / "python3"
        prefix = [sys.executable, str(self.fixture), mode, str(state), *arguments]
        if mode == "shim":
            prefix.append(sys.executable)
        wrapper.write_text("#!/bin/sh\nexec " + " ".join(shlex.quote(value) for value in prefix) + ' "$@"\n')
        wrapper.chmod(0o700)
        environment = dict(os.environ)
        environment["PATH"] = str(directory) + os.pathsep + environment.get("PATH", "")
        return environment

    def start_cap(self, command, environment=None, prefix=None, command_seconds=1, capture_seconds=12, cwd=None, bridge_path=None):
        self.sequence += 1
        prefix = prefix or self.root / ("phase-%d" % self.sequence)
        arguments = [TIMEOUT, "--signal=KILL", "%ds" % capture_seconds, "bash", str(bridge_path or BRIDGE),
                     TIMEOUT, str(prefix), str(command_seconds), str(capture_seconds), "--", *command]
        output = self.root / ("caller-output-%d" % self.sequence)
        errors = self.root / ("caller-errors-%d" % self.sequence)
        started = time.monotonic()
        self.bootstrap_bounds.append(started + command_seconds + 0.25)
        with output.open("wb") as stdout, errors.open("wb") as stderr:
            process = subprocess.Popen(arguments, stdin=subprocess.DEVNULL, stdout=stdout,
                                       stderr=stderr, close_fds=True, env=environment, cwd=cwd)
        self.owned.append(process)
        if not Path(prefix).is_absolute():
            prefix = Path(cwd or os.getcwd()) / prefix
        return process, prefix, started, errors

    def finish_cap(self, case, capture_seconds=12):
        process, prefix, started, errors = case
        raw = process.wait(timeout=capture_seconds + 3)
        elapsed = time.monotonic() - started
        status = raw if raw >= 0 else 128 - raw
        fields = named_receipt(Path(str(prefix) + ".caller-status.log"), "BUSTER_IOS_CALLER")
        positional = Path(str(prefix) + ".caller-fields.log").read_text().split()
        expected = [fields[key] for key in ("admission", "helper_status", "invocation_status",
                                            "generation", "command_monitor_status", "reason")]
        self.assertEqual(positional, expected)
        self.assertRegex(fields["generation"], r"^[A-Za-z0-9]{8}$")
        private = Path(Path(str(prefix) + ".caller-private-directory.log").read_text().strip())
        if not private.is_absolute():
            private = prefix.parent / private.name
        self.assertTrue(private.is_dir())
        self.assertTrue(str(private).startswith(str(prefix) + ".capture."))
        return status, fields, prefix, private, elapsed, errors.read_bytes()

    def copy_overlay(self, state, mode):
        state.mkdir()
        real_cp = shutil.which("cp")
        self.assertIsNotNone(real_cp)
        wrapper = state / "cp"
        wrapper.write_text("#!" + sys.executable + "\n" + '''import json,pathlib,subprocess,sys,time
state=pathlib.Path(STATE)
arguments=sys.argv[1:]
with (state/'calls.jsonl').open('a') as output:output.write(json.dumps(arguments)+'\\n')
operands=arguments[1:] if arguments[:1]==['--'] else arguments
if MODE in ('partial','delay'):
    subprocess.run([REAL,operands[0],operands[-1]],check=True)
    (state/'partial-entered').write_text(str(time.monotonic_ns()))
    if MODE=='partial':sys.exit(70)
    time.sleep(13.3)
    (state/'late-copy-entered').touch()
sys.exit(subprocess.run([REAL,*arguments]).returncode)
'''.replace("STATE", repr(str(state))).replace("MODE", repr(mode)).replace("REAL", repr(real_cp)))
        wrapper.chmod(0o700)
        environment = dict(os.environ)
        environment["PATH"] = str(state) + os.pathsep + environment.get("PATH", "")
        return environment

    def test_builtin_helper_paths_overwrite_old_callers_and_record_private_stages(self):
        stages = ("private-created", "ipc-created", "caller-initial-recorded", "monitor-launch",
                  "setup-observed", "invocation-launch", "invocation-registered", "ready-observed",
                  "go-authorized", "complete-observed", "completion-eof", "invocation-waited",
                  "snapshot-start", "snapshot-complete", "caller-final-recorded")
        for spelling in (BRIDGE.name, "nested path/" + BRIDGE.name):
            with self.subTest(bridge=spelling):
                cwd = self.root / ("helper-path-%d" % self.sequence)
                (cwd / Path(spelling).parent).mkdir(parents=True)
                shutil.copyfile(BRIDGE, cwd / spelling)
                shutil.copyfile(HELPER, (cwd / spelling).parent / HELPER.name)
                prefix = cwd / "phase"
                Path(str(prefix) + ".caller-status.log").write_text(
                    "BUSTER_IOS_CALLER version=1 generation=OldValid admission=1 helper_status=0 "
                    "invocation_status=0 command_monitor_status=0 reason=complete\n")
                Path(str(prefix) + ".caller-fields.log").write_text("1 0 0 OldValid 0 complete\n")
                status, caller, prefix, private, elapsed, errors = self.finish_cap(self.start_cap(
                    ["/bin/sh", "-c", "printf 'native\\n'"], prefix=prefix, cwd=cwd, bridge_path=spelling))
                self.assertEqual(status, 0, errors)
                self.assertEqual(caller["admission"], "1")
                self.assertNotEqual(caller["generation"], "OldValid")
                self.assertEqual(Path(str(prefix) + ".log").read_bytes(), b"native\n")
                observations = []
                for line in (private / "bridge-stages.log").read_text().splitlines():
                    tag, *tokens = line.split()
                    self.assertEqual(tag, "BUSTER_IOS_BRIDGE_STAGE")
                    observations.append(dict(token.split("=", 1) for token in tokens))
                self.assertEqual([int(item["ordinal"]) for item in observations], list(range(1, 16)))
                self.assertEqual([item["stage"] for item in observations], list(stages))
                seconds = [int(item["elapsed_seconds"]) for item in observations]
                self.assertEqual(seconds, sorted(seconds))
                self.assertTrue(all(value >= 0 for value in seconds))

    def test_both_caller_destinations_are_guarded_before_either_builtin_write(self):
        for suffix in (".caller-status.log", ".caller-fields.log"):
            for kind in ("symlink", "dangling", "directory"):
                with self.subTest(suffix=suffix, kind=kind):
                    self.sequence += 1
                    prefix = self.root / ("guarded-caller-%d" % self.sequence)
                    targets = [Path(str(prefix) + value) for value in
                               (".caller-status.log", ".caller-fields.log")]
                    blocked = Path(str(prefix) + suffix)
                    other = targets[1] if blocked == targets[0] else targets[0]
                    other.write_bytes(b"previous caller record\n")
                    sentinel = self.root / ("caller-sentinel-%d" % self.sequence)
                    if kind == "directory":
                        blocked.mkdir()
                    else:
                        if kind == "symlink":
                            sentinel.write_bytes(b"untouched sentinel\n")
                        blocked.symlink_to(sentinel)
                    native = self.root / ("native-forbidden-%d" % self.sequence)
                    process, actual_prefix, started, errors = self.start_cap(
                        ["/bin/sh", "-c", "printf forbidden > " + shlex.quote(str(native))], prefix=prefix)
                    self.assertEqual(process.wait(timeout=3), 125, errors.read_bytes())
                    self.assertEqual(other.read_bytes(), b"previous caller record\n")
                    self.assertFalse(native.exists())
                    if kind == "symlink":
                        self.assertEqual(sentinel.read_bytes(), b"untouched sentinel\n")
                    elif kind == "dangling":
                        self.assertFalse(sentinel.exists())
                    else:
                        self.assertEqual(list(blocked.iterdir()), [])

    def caller_phase_fixture(self, mode):
        state = self.root / ("caller-record-" + mode)
        state.mkdir()
        shutil.copyfile(HELPER, state / HELPER.name)
        shutil.copyfile(BRIDGE, state / BRIDGE.name)
        environment = dict(os.environ)
        shell_hook = state / "shell-hook"
        if mode == "partial":
            shell_hook.write_text('''if [[ $0 == */lifecycle_capture_bridge.sh && ${1:-} != --bootstrap ]]; then
printf() {
    builtin printf "$@"
    if [[ ${1:-} == BUSTER_IOS_CALLER* && ${3:-} == 1 ]]; then return 70; fi
}
fi
''')
        else:
            shell_hook.write_text('''if [[ $0 == */lifecycle_capture_bridge.sh && ${1:-} != --bootstrap ]]; then
trap 'exit 70' EXIT
fi
''')
        environment["BASH_ENV"] = str(shell_hook)
        source = LAUNCHER.read_text()
        phase = source[source.index("run_lifecycle_phase() {"):source.index("simulator_udid_is_valid() {")]
        prefix = state / "console"
        script = state / "phase.sh"
        script.write_text("#!/bin/bash\nset -euo pipefail\nmonitor_command_timeout_seconds=1\n"
            "timeout_bin=" + shlex.quote(TIMEOUT) + "\nconsole_log_base=" + shlex.quote(str(prefix))
            + "\ncollect_lifecycle_context() { :; }\n" + phase
            + "\nif run_lifecycle_phase control Test " + shlex.quote(str(prefix))
            + " 1 /bin/sh -c ':'; then exit 0; else exit $?; fi\n")
        process = subprocess.Popen(["bash", str(script)], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   env=environment, close_fds=True)
        self.owned.append(process)
        output, errors = process.communicate(timeout=5)
        self.assertEqual(process.returncode, 1, errors)
        status = Path(str(prefix) + ".control.status.log").read_text()
        self.assertIn("outcome=evidence-failure", status)
        actual_collector = 125 if mode == "partial" else 70
        self.assertIn("BUSTER_IOS_CALLER_GATE monitor_status=%d admission=0" % actual_collector, status)
        phase_prefix = Path(str(prefix) + ".control")
        caller = named_receipt(Path(str(phase_prefix) + ".caller-status.log"), "BUSTER_IOS_CALLER")
        positional = Path(str(phase_prefix) + ".caller-fields.log").read_text().split()
        self.assertEqual(caller["admission"], "1")
        self.assertEqual(caller["helper_status"], "0")
        self.assertEqual(caller["invocation_status"], "0")
        private = Path(Path(str(phase_prefix) + ".caller-private-directory.log").read_text().strip())
        owner = self.supervisor(private / phase_prefix.name)
        self.assertEqual(owner["command_status"], "0")
        self.assertEqual(owner["cleanup_status"], "0")
        return caller, positional

    def test_partial_final_caller_record_cannot_admit_older_second_record(self):
        caller, positional = self.caller_phase_fixture("partial")
        self.assertEqual(positional[0], "0")
        self.assertEqual(positional[3], caller["generation"])
        self.assertEqual(positional[-1], "starting")

    def test_full_matching_caller_records_cannot_admit_actual_collector_exit70(self):
        caller, positional = self.caller_phase_fixture("nonzero")
        self.assertEqual(positional, [caller[key] for key in
            ("admission", "helper_status", "invocation_status", "generation", "command_monitor_status", "reason")])

    def test_owner_and_keeper_skip_unused_finite_site_initialization(self):
        site = self.root / "finite-site-hook"
        site.mkdir()
        entered, completed = site / "entered", site / "completed"
        (site / "sitecustomize.py").write_text(
            "import time\nfrom pathlib import Path\nPath(" + repr(str(entered)) + ").touch()\n"
            "time.sleep(13)\nPath(" + repr(str(completed)) + ").touch()\n")
        environment = dict(os.environ)
        environment["PYTHONPATH"] = str(site)
        command = ["/bin/sh", "-c", 'printf "%s\\n%s\\n" "$1" "$PYTHONPATH"', "native", "payload argument"]
        status, caller, prefix, private, elapsed, errors = self.finish_cap(self.start_cap(command, environment))
        self.assertEqual(status, 0, errors)
        self.assertEqual(caller["admission"], "1")
        self.assertEqual(Path(str(prefix) + ".log").read_bytes(),
                         ("payload argument\n" + str(site) + "\n").encode())
        owner = self.supervisor(prefix)
        for field in ("command_status", "capture_status", "cleanup_status", "keeper_status"):
            self.assertEqual(owner[field], "0")
        for field in ("native_reaped", "keeper_reaped", "keeper_control_eof_ack"):
            self.assertEqual(owner[field], "1")
        self.assertFalse(entered.exists())
        self.assertFalse(completed.exists())

    def test_snapshot_uses_one_batch_and_preserves_binary_bytes_for_relative_prefixes(self):
        payload = bytes(range(256)) * 256
        for spelling in ("phase name", "relative parent/phase name", "nested/relative parent/phase name"):
            with self.subTest(prefix=spelling):
                state = self.root / ("copy-%d" % self.sequence)
                environment = self.copy_overlay(state, "normal")
                cwd = state / "work"
                cwd.mkdir()
                (cwd / Path(spelling).parent).mkdir(parents=True, exist_ok=True)
                command = [sys.executable, "-c", "import os;os.write(1,bytes(range(256))*256)"]
                status, fields, prefix, private, elapsed, errors = self.finish_cap(
                    self.start_cap(command, environment, prefix=Path(spelling), cwd=cwd))
                self.assertEqual(status, 0, errors)
                self.assertEqual(fields["admission"], "1")
                self.assertEqual(fields["helper_status"], fields["invocation_status"])
                self.assertEqual(Path(str(prefix) + ".log").read_bytes(), payload)
                calls = [json.loads(line) for line in (state / "calls.jsonl").read_text().splitlines()]
                self.assertEqual(len(calls), 1)
                self.assertEqual(len(calls[0]), 9)  # --, seven optional sources, parent
                self.assertEqual(calls[0][-1], str(Path(spelling).parent))
                for suffix in (".log", ".native-status.log", ".command-elapsed.log", ".capture-elapsed.log",
                               ".log.capture-status.log", ".supervisor-fields.log", ".supervisor-status.log"):
                    self.assertEqual(Path(str(prefix) + suffix).read_bytes(),
                                     Path(str(private / prefix.name) + suffix).read_bytes())

    def test_snapshot_partial_copy_failure_cannot_admit_missing_last_receipts(self):
        state = self.root / "partial-copy"
        environment = self.copy_overlay(state, "partial")
        prefix = self.root / "partial-stable-prefix"
        # A previous admitted generation must not survive this failed copy.
        Path(str(prefix) + ".caller-status.log").write_text(
            "BUSTER_IOS_CALLER version=1 generation=OldValid admission=1 helper_status=0 "
            "invocation_status=0 command_monitor_status=0 reason=complete\n")
        Path(str(prefix) + ".caller-fields.log").write_text("1 0 0 OldValid 0 complete\n")
        status, fields, prefix, private, elapsed, errors = self.finish_cap(
            self.start_cap([sys.executable, "-c", "print('partial')"], environment, prefix=prefix))
        self.assertEqual(status, 1, errors)
        self.assertEqual(fields["admission"], "0")
        self.assertEqual(fields["reason"], "snapshot")
        self.assertEqual(fields["helper_status"], "0")
        self.assertEqual(fields["invocation_status"], "0")
        self.assertEqual(Path(str(prefix) + ".log").read_bytes(), b"partial\n")
        self.assertFalse(Path(str(prefix) + ".log.capture-status.log").exists())
        self.assertFalse(Path(str(prefix) + ".supervisor-status.log").exists())
        self.assertTrue(Path(str(private / prefix.name) + ".supervisor-status.log").exists())
        self.assertEqual(len((state / "calls.jsonl").read_text().splitlines()), 1)

    def test_snapshot_partial_copy_interruption_retains_capture_cap_and_refuses_admission(self):
        state = self.root / "delayed-copy"
        environment = self.copy_overlay(state, "delay")
        status, fields, prefix, private, elapsed, errors = self.finish_cap(
            self.start_cap([sys.executable, "-c", "print('bounded')"], environment))
        self.assertIn(status, (124, 137), errors)
        self.assertEqual(fields["admission"], "0")
        self.assertGreaterEqual(elapsed, 11.8)
        self.assertLess(elapsed, 13)
        self.assertFalse(Path(str(prefix) + ".supervisor-status.log").exists())
        self.assertTrue(Path(str(private / prefix.name) + ".supervisor-status.log").exists())
        entered_ns = int((state / "partial-entered").read_text())
        time.sleep(max(0, (entered_ns + 13_300_000_000 - time.monotonic_ns()) / 1_000_000_000))
        self.assertFalse((state / "late-copy-entered").exists())

    def test_snapshot_keeps_native_status_optional_for_actual_launch_failure(self):
        state = self.root / "optional-native"
        environment = self.copy_overlay(state, "normal")
        status, fields, prefix, private, elapsed, errors = self.finish_cap(
            self.start_cap([str(self.root / "missing-command")], environment))
        self.assertEqual(status, 0, errors)
        self.assertEqual(fields["admission"], "1")
        self.assertEqual(fields["helper_status"], "127")
        self.assertEqual(fields["invocation_status"], "127")
        self.assertFalse(Path(str(prefix) + ".native-status.log").exists())
        self.assertEqual(self.supervisor(prefix)["native_status"], "unavailable")
        calls = [json.loads(line) for line in (state / "calls.jsonl").read_text().splitlines()]
        self.assertEqual(len(calls), 1)
        self.assertEqual(len(calls[0]), 8)

    def test_snapshot_refuses_symlink_or_nonregular_destinations_before_copy(self):
        for kind in ("symlink", "dangling-symlink", "directory"):
            with self.subTest(destination=kind):
                state = self.root / ("blocked-copy-" + kind)
                environment = self.copy_overlay(state, "normal")
                prefix = self.root / ("blocked-stable-" + kind)
                destination = Path(str(prefix) + ".log")
                sentinel = state / "sentinel"
                payload = b"retain\x00\xff"
                if kind == "directory":
                    destination.mkdir()
                    sentinel = destination / "sentinel"
                    sentinel.write_bytes(payload)
                else:
                    if kind == "symlink":
                        sentinel.write_bytes(payload)
                    destination.symlink_to(sentinel)
                status, fields, prefix, private, elapsed, errors = self.finish_cap(
                    self.start_cap([sys.executable, "-c", "print('must not replace')"],
                                   environment, prefix=prefix))
                self.assertEqual(status, 1, errors)
                self.assertEqual(fields["admission"], "0")
                self.assertEqual(fields["reason"], "snapshot")
                self.assertEqual(fields["helper_status"], "0")
                self.assertEqual(fields["invocation_status"], "0")
                self.assertFalse((state / "calls.jsonl").exists())
                self.assertFalse(Path(str(prefix) + ".supervisor-status.log").exists())
                self.assertTrue(Path(str(private / prefix.name) + ".supervisor-status.log").exists())
                if kind == "dangling-symlink":
                    self.assertFalse(sentinel.exists())
                else:
                    self.assertEqual(sentinel.read_bytes(), payload)
                if kind != "directory":
                    self.assertTrue(destination.is_symlink())
                    self.assertEqual(destination.readlink(), sentinel)

    def test_extracted_phase_partial_copy_is_evidence_failure_with_complete_private_owner(self):
        state = self.root / "phase-partial-copy"
        environment = self.copy_overlay(state, "partial")
        shutil.copyfile(HELPER, state / HELPER.name)
        shutil.copyfile(BRIDGE, state / BRIDGE.name)
        source = LAUNCHER.read_text()
        phase = source[source.index("run_lifecycle_phase() {"):source.index("simulator_udid_is_valid() {")]
        prefix = state / "console"
        script = state / "phase.sh"
        script.write_text("#!/bin/bash\nset -euo pipefail\nmonitor_command_timeout_seconds=1\n"
            "timeout_bin=" + shlex.quote(TIMEOUT) + "\nconsole_log_base=" + shlex.quote(str(prefix))
            + "\ncollect_lifecycle_context() { :; }\n" + phase
            + "\nif run_lifecycle_phase control Test " + shlex.quote(str(prefix))
            + " 1 " + shlex.quote(sys.executable) + " -c pass; then exit 0; else exit $?; fi\n")
        process = subprocess.Popen(["bash", str(script)], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   env=environment, close_fds=True)
        self.owned.append(process)
        output, errors = process.communicate(timeout=5)
        self.assertEqual(process.returncode, 1, errors)
        status = Path(str(prefix) + ".control.status.log").read_text()
        self.assertIn("outcome=evidence-failure", status)
        self.assertIn("BUSTER_IOS_CALLER_GATE monitor_status=1 admission=0 invocation_status=0", status)
        self.assertIn("reason=snapshot", status)
        private = Path(Path(str(prefix) + ".control.caller-private-directory.log").read_text().strip())
        owner = self.supervisor(private / "console.control")
        self.assertEqual(owner["command_status"], "0")
        self.assertEqual(owner["capture_status"], "0")
        self.assertEqual(owner["cleanup_status"], "0")

    def supervisor(self, prefix):
        return named_receipt(Path(str(prefix) + ".supervisor-status.log"), "BUSTER_IOS_SUPERVISOR")

    def close_descriptor(self, descriptor):
        if descriptor in self.descriptors:
            self.descriptors.remove(descriptor)
            os.close(descriptor)

    def owner_driver(self, command, mode=None, state=None, command_seconds=1, capture_seconds=12,
                     authorize=True):
        self.sequence += 1
        prefix = self.root / ("direct-owner-%d" % self.sequence)
        generation = "Direct01"
        pipes = {name: os.pipe() for name in ("control", "ready", "completion", "lifetime", "adjudication")}
        self.descriptors.update(descriptor for pair in pipes.values() for descriptor in pair)
        child = {name: pair[0] if name in ("control", "lifetime", "adjudication") else pair[1]
                 for name, pair in pipes.items()}
        parent = {name: pair[1] if child[name] == pair[0] else pair[0] for name, pair in pipes.items()}
        arguments = ["--prefix", str(prefix), "--command-seconds", str(command_seconds),
                     "--capture-seconds", str(capture_seconds), "--bridge-generation", generation]
        for name, descriptor in child.items():
            arguments.extend(("--bridge-%s-fd" % name, str(descriptor)))
        arguments.extend(("--", *command))
        invocation = ([sys.executable, str(self.fixture), mode, str(state), str(HELPER), *arguments]
                      if mode else [sys.executable, str(HELPER), *arguments])
        started = time.monotonic()
        with (self.root / ("direct-errors-%d" % self.sequence)).open("wb") as errors:
            process = subprocess.Popen(invocation, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                       stderr=errors, close_fds=True, pass_fds=tuple(child.values()))
        self.owned.append(process)
        for descriptor in child.values():
            self.close_descriptor(descriptor)
        ready = self.read_frame(parent["ready"], 3).decode().split()
        self.assertEqual(ready[:2], ["READY", generation])
        self.assertEqual(len(ready), 5)
        self.assertEqual(ready[2], ready[3])
        self.assertEqual(ready[2], ready[4])
        if authorize:
            os.write(parent["control"], ("GO %s\n" % generation).encode())
        return process, prefix, parent, generation, started

    def read_frame(self, descriptor, seconds):
        result = b""
        deadline = time.monotonic() + seconds
        while not result.endswith(b"\n") and len(result) < 256:
            remaining = deadline - time.monotonic()
            self.assertGreater(remaining, 0, "bounded protocol read timed out")
            self.assertTrue(select.select([descriptor], [], [], remaining)[0])
            chunk = os.read(descriptor, 1)
            self.assertTrue(chunk, "protocol EOF before complete frame")
            result += chunk
        self.assertTrue(result.endswith(b"\n") and len(result) < 256, result)
        return result

    def finish_owner(self, case):
        process, prefix, descriptors, generation, started = case
        complete = self.read_frame(descriptors["completion"], 4).decode().split()
        self.assertEqual(complete[:2], ["COMPLETE", generation])
        self.assertEqual(len(complete), 3)
        self.assertTrue(select.select([descriptors["completion"]], [], [], 3)[0])
        self.assertEqual(os.read(descriptors["completion"], 1), b"")
        status = process.wait(timeout=3)
        self.assertEqual(status, int(complete[2]))
        return status, self.supervisor(prefix), time.monotonic() - started

    def test_closed_completion_pipe_reports_failure_without_traceback_or_status_invention(self):
        cases = ((0, None, False, 125), (137, None, False, 124),
                 (0, signal.SIGINT, False, 130), (0, signal.SIGTERM, False, 143),
                 (137, None, True, 124))
        for monitor_status, signum, caller_lost, expected in cases:
            with self.subTest(monitor_status=monitor_status, signum=signum, caller_lost=caller_lost):
                case = self.owner_driver(["/bin/sh", "-c", "printf completion-control"],
                                         command_seconds=10, capture_seconds=21)
                process, prefix, descriptors, generation, started = case
                self.assertEqual(self.read_frame(descriptors["ready"], 3),
                                 ("DONE %s\n" % generation).encode())
                # DONE precedes final monitor proof. Close the only completion
                # reader before authorizing terminal publication: real EPIPE.
                self.close_descriptor(descriptors["completion"])
                if signum is not None:
                    os.write(descriptors["lifetime"], ("CANCEL %s %d\n" % (generation, signum)).encode())
                if caller_lost:
                    self.close_descriptor(descriptors["lifetime"])
                os.write(descriptors["adjudication"],
                         ("COMMAND-MONITOR %s %d\n" % (generation, monitor_status)).encode())
                self.close_descriptor(descriptors["adjudication"])
                self.close_descriptor(descriptors["control"])
                self.assertEqual(process.wait(timeout=4), expected)
                owner = self.supervisor(prefix)
                self.assertEqual(owner["helper_error"], "BrokenPipeError")
                self.assertEqual(owner["cleanup_status"], "1")
                self.assertEqual(owner["command_status"], str(expected))
                self.assertEqual(owner["native_status"], "0")
                self.assertEqual(owner["deadline_reached"], "1" if monitor_status else "0")
                self.assertEqual(owner["cancellation_signal"], str(signum or 0))
                self.assertEqual(owner["caller_lost"], str(int(caller_lost)))
                self.assertEqual(owner["native_reaped"], "1")
                self.assertEqual(owner["group_authority_released"], "1")
                if caller_lost:
                    # Caller loss has no post-cap reap/drain grace. Final owned
                    # dispatch is required even when reap is not yet observed.
                    self.assertEqual(owner["kill_group_attempted"], "1")
                    self.assertEqual(owner["capture_status"], "124")
                else:
                    self.assertEqual(owner["keeper_reaped"], "1")
                    self.assertEqual(owner["capture_status"], "0")
                    self.assertEqual(Path(str(prefix) + ".log").read_bytes(), b"completion-control")
                errors = (self.root / ("direct-errors-%d" % self.sequence)).read_text()
                self.assertEqual(errors, "")
                self.assertLess(time.monotonic() - started, 5)

    def test_native_probe_receipts_accept_only_explicit_optional_unavailability(self):
        script = SOURCE_ROOT / "ios/launch_diagnostics_simulator_test.sh"
        status_log = self.root / "native-probe.status"
        output_log = self.root / "native-probe.output"
        run_log = self.root / "native-probe.run"
        native_commands = self.root / "native-probe.commands"
        command = "simctl spawn 01234567-89AB-CDEF-0123-456789ABCDEF ps -A\n"
        prefix = "BUSTER_IOS_PHASE phase=diagnostic-process-table label=probe-control "
        suffix = ("elapsed_seconds=42 deadline_seconds=10 output_limit_bytes=65536 "
                  "command_elapsed_seconds=unavailable capture_elapsed_seconds=42 ")
        expired = (prefix + "outcome=evidence-failure status=125 native_status=unavailable capture_status=124 "
                   + suffix + "capture_receipt=incomplete\n"
                   + "BUSTER_IOS_CALLER_GATE monitor_status=137 admission=0 invocation_status=unavailable "
                   "generation=Direct01 reason=starting\n"
                   + "BUSTER_IOS_CAPTURE incomplete=1 reason=missing-or-empty-receipt\n")
        expired += "command: xcrun " + command
        warning = "warning: iOS diagnostic unavailable name=process-table outcome=evidence-failure status=1;\n"
        complete = prefix + "outcome=success status=0 native_status=0 capture_status=0 " + suffix + "capture_receipt=complete\n"
        complete += "command: xcrun " + command
        declined = (complete.replace("outcome=success status=0 native_status=0",
                                     "outcome=timeout status=124 native_status=unavailable")
                    + "BUSTER_IOS_SUPERVISOR_GATE helper_status=124 supervisor_valid=1 deadline_reached=1 cleanup_status=0\n"
                    + "BUSTER_IOS_SUPERVISOR version=1 native_launch=0\n")
        cases = [
            ("success", complete, "", b"", 0),
            ("command-failure", complete.replace("outcome=success status=0 native_status=0",
                                                "outcome=command-failure status=70 native_status=70"), warning, b"stderr", 0),
            ("native-124", complete.replace("outcome=success status=0 native_status=0",
                                           "outcome=command-failure status=124 native_status=124"), warning, b"stderr", 0),
            ("timeout", complete.replace("outcome=success status=0 native_status=0",
                                         "outcome=timeout status=124 native_status=143"), warning, b"stderr", 0),
            ("capture-expired", expired, warning, None, 0),
            ("declined-before-admission", declined, warning, b"", 0),
            ("success-without-native-attempt", complete, "", b"", 1),
            ("declined-without-proof", declined.replace("supervisor_valid=1", "supervisor_valid=0"), warning, b"", 1),
            ("declined-with-native-admission", declined.replace("native_launch=0", "native_launch=1"), warning, b"", 1),
            ("capture-expired-124", expired.replace("monitor_status=137", "monitor_status=124"), warning, None, 0),
            ("limit", complete, "", b"x" * 65536, 0),
            ("oversized", complete, "", b"x" * 65537, 1),
            ("missing-output", complete, "", None, 1),
            ("missing-status", "", warning, None, 1),
            ("missing-command", expired.split("command:")[0], warning, None, 1),
            ("wrong-command", expired.replace("simctl spawn 01234567-89AB-CDEF-0123-456789ABCDEF ps",
                                              "simctl spawn 01234567-89AB-CDEF-0123-456789ABCDEF log"), warning, None, 1),
            ("missing-warning", expired, "", None, 1),
            ("wrong-probe-warning", expired, warning.replace("process-table", "unified-log"), None, 1),
            ("traceback", expired, warning + "Traceback (most recent call last):\n", None, 1),
            ("non-timer-refusal", expired.replace("monitor_status=137", "monitor_status=1"), warning, None, 1),
            ("malformed-caller", expired.replace("generation=Direct01", "generation=bad"), warning, None, 1),
            ("missing-capture-reason", expired.split("BUSTER_IOS_CAPTURE")[0], warning, None, 1),
            ("wrong-deadline", expired.replace("deadline_seconds=10", "deadline_seconds=11"), warning, None, 1),
            ("partial-capture", complete.replace("capture_status=0", "capture_status=124"), warning, b"", 1),
        ]
        for label, receipt, run, output, expected in cases:
            with self.subTest(label=label):
                status_log.write_text(receipt)
                run_log.write_text(run)
                native_commands.write_text("" if label.startswith("declined-") or label.startswith("capture-expired")
                                           or label == "success-without-native-attempt" else command)
                output_log.unlink(missing_ok=True)
                if output is not None:
                    output_log.write_bytes(output)
                result = subprocess.run(["/bin/bash", str(script), "--check-probe-receipt",
                                         str(status_log), str(output_log), str(run_log), "process-table", "10", str(native_commands)],
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=3)
                self.assertEqual(result.returncode, expected, result.stderr.decode())

    def test_observed_startup_deadline_survives_caller_loss_without_native_admission(self):
        for monitor_status, signum in ((137, None), (None, None), (137, signal.SIGINT), (137, signal.SIGTERM)):
            with self.subTest(monitor_status=monitor_status, signum=signum):
                native = self.root / ("declined-native-%d" % self.sequence)
                case = self.owner_driver(["/bin/sh", "-c", "printf forbidden > " + shlex.quote(str(native))],
                                         command_seconds=10, capture_seconds=21, authorize=False)
                process, prefix, descriptors, generation, started = case
                # This is our registered direct child. Stop it only to make both
                # already-owned channels ready before its next pre-GO pump.
                process.send_signal(signal.SIGSTOP)
                stop_bound = time.monotonic() + 2
                while True:
                    observed, raw = os.waitpid(process.pid, os.WUNTRACED | os.WNOHANG)
                    if observed:
                        self.assertTrue(os.WIFSTOPPED(raw))
                        break
                    self.assertLess(time.monotonic(), stop_bound)
                    time.sleep(.005)
                if monitor_status is not None:
                    os.write(descriptors["adjudication"],
                             ("COMMAND-MONITOR %s %d\n" % (generation, monitor_status)).encode())
                    self.close_descriptor(descriptors["adjudication"])
                if signum is not None:
                    os.write(descriptors["lifetime"], ("CANCEL %s %d\n" % (generation, signum)).encode())
                self.close_descriptor(descriptors["lifetime"])
                self.close_descriptor(descriptors["control"])
                process.send_signal(signal.SIGCONT)
                status, owner, elapsed = self.finish_owner(case)
                expected = 128 + signum if signum is not None else 124 if monitor_status else 125
                self.assertEqual(status, expected)
                self.assertEqual(owner["command_status"], str(expected))
                self.assertEqual(owner["deadline_reached"], "1" if monitor_status else "0")
                self.assertEqual(owner["capture_status"], "124")
                self.assertEqual(owner["caller_lost"], "1")
                self.assertEqual(owner["command_monitor_status"], str(monitor_status) if monitor_status else "pending")
                self.assertEqual(owner["native_launch"], "0")
                self.assertEqual(owner["keeper_status"], "unavailable")
                self.assertEqual(owner["cleanup_status"], "0")
                self.assertEqual(owner["helper_error"], "none")
                self.assertFalse(native.exists())
                self.assertLess(elapsed, 3)

    def test_actual_invocation70_refuses_real_helper_token0(self):
        state = self.root / "shim70"
        environment = self.python_overlay(state, "shim", "exit70", "0", "0")
        status, fields, prefix, private, elapsed, errors = self.finish_cap(
            self.start_cap([sys.executable, "-c", "pass"], environment))
        self.assertEqual(status, 1, errors)
        self.assertEqual(fields["admission"], "0")
        self.assertEqual(fields["helper_status"], "0")
        self.assertEqual(fields["invocation_status"], "70")
        self.assertEqual(fields["reason"], "invocation-status")
        self.assertTrue((state / "actual-helper-wait-0").exists())
        self.assertFalse(Path(str(prefix) + ".supervisor-status.log").exists())

    def test_actual_invocation_signal143_refuses_real_helper_token0(self):
        state = self.root / "shim143"
        environment = self.python_overlay(state, "shim", "term", "0", "0")
        status, fields, prefix, private, elapsed, errors = self.finish_cap(
            self.start_cap([sys.executable, "-c", "pass"], environment))
        self.assertEqual(status, 1, errors)
        self.assertEqual(fields["admission"], "0")
        self.assertEqual(fields["helper_status"], "0")
        self.assertEqual(fields["invocation_status"], "143")
        self.assertEqual(fields["reason"], "invocation-status")

    def startup_case(self, disposition):
        state = self.root / ("startup-" + disposition)
        environment = self.python_overlay(state, "startup", disposition, "13")
        native_marker = self.root / "must-not-admit-native"
        command = [sys.executable, "-c", "import pathlib,sys;pathlib.Path(sys.argv[1]).touch()", str(native_marker)]
        status, fields, prefix, private, elapsed, errors = self.finish_cap(self.start_cap(command, environment))
        self.assertIn(status, (124, 137), errors)
        self.assertEqual(fields["admission"], "0")
        self.assertTrue((state / "startup-entered").exists())
        self.assertFalse((state / "startup-exec-admitted").exists())
        self.assertFalse(native_marker.exists())
        self.assertGreaterEqual(elapsed, 11.8)
        self.assertLess(elapsed, 13)
        self.assertFalse(Path(str(prefix) + ".supervisor-status.log").exists())
        # A timer result alone cannot prove that delayed startup was killed.
        # Its finite delay starts at entry, not at the outer timer's launch.
        entered_ns = int((state / "startup-entered").read_text())
        now_ns = time.monotonic_ns()
        self.assertGreater(entered_ns, 0)
        self.assertLessEqual(entered_ns, now_ns)
        time.sleep(max(0, (entered_ns + 13_300_000_000 - now_ns) / 1_000_000_000))
        self.assertFalse((state / "startup-exec-admitted").exists())
        self.assertFalse(native_marker.exists())

    def test_finite_startup13_is_bounded_by_caller_cap12_default_signals(self):
        self.startup_case("default")

    def test_finite_startup13_is_bounded_by_caller_cap12_ignored_usr1(self):
        self.startup_case("ignore")

    def test_finite_startup13_is_bounded_by_caller_cap12_blocked_usr1(self):
        self.startup_case("block")

    def test_early_native_zero_accepts_escaped_real_eof_after_command_bound(self):
        state = self.root / "finite-escaped-writer"
        state.mkdir()
        self.release_states.append(state)
        self.required_end_markers.append(state / "writer-ended")
        command = [sys.executable, str(self.fixture), "early", str(state), "1", "0", "3"]
        status, fields, prefix, private, elapsed, errors = self.finish_cap(self.start_cap(command))
        self.assertEqual(status, 0, errors)
        self.assertEqual(fields["admission"], "1")
        self.assertEqual(fields["command_monitor_status"], "0")
        owner = self.supervisor(prefix)
        self.assertEqual(owner["native_status"], "0")
        self.assertEqual(owner["command_status"], "0")
        self.assertEqual(owner["deadline_reached"], "0")
        self.assertEqual(owner["capture_eof"], "1")
        self.assertEqual(owner["command_authenticated"], "1")
        self.assertGreater(elapsed, 2.8)
        self.assertLess(elapsed, 5)

    def test_late_done_cannot_authenticate_observed_native_zero_after_monitor_expiry(self):
        state = self.root / "late-done"
        environment = self.python_overlay(state, "delay-done")
        status, fields, prefix, private, elapsed, errors = self.finish_cap(
            self.start_cap([sys.executable, "-c", "pass"], environment))
        self.assertEqual(status, 0, errors)
        self.assertEqual(fields["admission"], "1")
        self.assertEqual(fields["helper_status"], "124")
        self.assertIn(fields["command_monitor_status"], ("124", "137"))
        owner = self.supervisor(prefix)
        self.assertEqual(owner["native_status"], "0")
        self.assertEqual(owner["command_status"], "124")
        self.assertEqual(owner["command_authenticated"], "0")
        self.assertEqual(owner["deadline_reached"], "1")
        self.assertTrue((state / "done-delay-entered").exists())

    def test_monitor_zero_is_provisional_until_final_eof_and_duplicate137_is_refused(self):
        state = self.root / "silent-anchor"
        state.mkdir()
        self.release_states.append(state)
        command = [sys.executable, str(self.fixture), "early", str(state), "0", "1", "0"]
        case = self.owner_driver(command, "anchor-guard", state)
        process, prefix, descriptors, generation, started = case
        self.assertEqual(self.read_frame(descriptors["ready"], 3), ("DONE %s\n" % generation).encode())
        os.write(descriptors["adjudication"], ("COMMAND-MONITOR %s 0\n" % generation).encode())
        self.assertTrue(wait_for_path(state / "pending-unreaped-anchor", 2))
        self.assertFalse((state / "forbidden-anchor-release").exists())
        self.assertFalse(Path(str(prefix) + ".supervisor-status.log").exists())
        self.assertIsNone(process.poll())
        os.write(descriptors["adjudication"], ("COMMAND-MONITOR %s 137\n" % generation).encode())
        self.close_descriptor(descriptors["adjudication"])
        self.close_descriptor(descriptors["control"])
        status, owner, elapsed = self.finish_owner(case)
        self.assertEqual(status, 125)
        self.assertEqual(owner["command_authenticated"], "0")
        self.assertEqual(owner["helper_error"], "BridgeProtocolError")
        self.assertEqual(owner["native_status"], "0")
        self.assertEqual(owner["capture_eof"], "1")
        self.assertEqual(owner["keeper_reaped"], "1")
        self.assertEqual(owner["keeper_release"], "final-group-signal")
        self.assertEqual(owner["group_kill_result"], "sent")
        self.assertFalse((state / "forbidden-anchor-release").exists())

    def test_caller_lifetime_eof_immediately_cleans_escaped_direct_native_without_signal_invention(self):
        state = self.root / "caller-eof-native"
        state.mkdir()
        self.release_states.append(state)
        command = [sys.executable, str(self.fixture), "active", str(state)]
        case = self.owner_driver(command, command_seconds=10, capture_seconds=21)
        process, prefix, descriptors, generation, started = case
        self.assertTrue(wait_for_path(state / "active-native", 3))
        lost = time.monotonic()
        self.close_descriptor(descriptors["lifetime"])
        status, owner, elapsed = self.finish_owner(case)
        self.assertNotEqual(status, 0)
        self.assertEqual(owner["caller_lost"], "1")
        self.assertEqual(owner["cancellation_signal"], "0")
        self.assertEqual(owner["capture_status"], "124")
        self.assertEqual(owner["term_native_attempted"], "1")
        self.assertEqual(owner["kill_native_attempted"], "1")
        self.assertEqual(owner["kill_group_attempted"], "1")
        self.assertEqual(owner["group_authority_released"], "1")
        self.assertLess(time.monotonic() - lost, 2)
        if owner["native_reaped"] == "0" or owner["keeper_reaped"] == "0":
            self.assertEqual(owner["cleanup_status"], "1")

    def test_old_residual_private_generation_cannot_replace_later_stable_receipts(self):
        state = self.root / "old-publication"
        environment = self.python_overlay(state, "delay-publication")
        self.required_end_markers.append(state / "owner-wrapper-ended")
        prefix = self.root / "repeated-stable-prefix"
        status, old, prefix, old_private, elapsed, errors = self.finish_cap(
            self.start_cap([sys.executable, "-c", "print('old')"], environment, prefix=prefix))
        self.assertIn(status, (124, 137), errors)
        self.assertEqual(old["admission"], "0")
        self.assertTrue((state / "publication-delay-entered").exists())
        self.assertFalse((state / "publication-delay-ended").exists())
        status, new, prefix, new_private, elapsed, errors = self.finish_cap(
            self.start_cap([sys.executable, "-c", "print('new')"], prefix=prefix))
        self.assertEqual(status, 0, errors)
        self.assertEqual(new["admission"], "1")
        self.assertNotEqual(old["generation"], new["generation"])
        self.assertNotEqual(old_private, new_private)
        paths = [Path(str(prefix) + suffix) for suffix in
                 (".caller-status.log", ".caller-fields.log", ".log", ".native-status.log",
                  ".command-elapsed.log", ".capture-elapsed.log", ".log.capture-status.log",
                  ".supervisor-fields.log", ".supervisor-status.log")]
        before = {path: path.read_bytes() for path in paths}
        self.assertEqual(before[Path(str(prefix) + ".log")], b"new\n")
        self.assertTrue(wait_for_path(state / "owner-wrapper-ended", 4))
        self.assertTrue(Path(str(old_private / prefix.name) + ".supervisor-status.log").exists())
        self.assertEqual({path: path.read_bytes() for path in paths}, before)

    def test_completion_eof_cannot_bypass_actual_invocation_wait_under_capture_clock(self):
        state = self.root / "early-completion-eof"
        environment = self.python_overlay(state, "shim", "preserve", "13", "1")
        status, fields, prefix, private, elapsed, errors = self.finish_cap(
            self.start_cap([sys.executable, "-c", "pass"], environment))
        self.assertIn(status, (124, 137), errors)
        self.assertEqual(fields["admission"], "0")
        self.assertTrue((state / "actual-helper-wait-0").exists())
        self.assertTrue((state / "shim-completion-closed").exists())
        self.assertTrue(Path(str(private / prefix.name) + ".supervisor-status.log").exists())
        self.assertFalse(Path(str(prefix) + ".supervisor-status.log").exists())
        self.assertGreaterEqual(elapsed, 11.8)
        self.assertLess(elapsed, 13)

    def test_active_escaped_direct_native_cancellation2_and15_keeps_owned_cleanup(self):
        for signum in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signum=signum):
                state = self.root / ("active-cancel-%d" % signum)
                state.mkdir()
                self.release_states.append(state)
                command = [sys.executable, str(self.fixture), "active", str(state)]
                case = self.owner_driver(command, command_seconds=10, capture_seconds=21)
                process, prefix, descriptors, generation, started = case
                self.assertTrue(wait_for_path(state / "active-native", 3))
                os.write(descriptors["lifetime"], ("CANCEL %s %d\n" % (generation, signum)).encode())
                self.assertEqual(self.read_frame(descriptors["ready"], 3), ("DONE %s\n" % generation).encode())
                os.write(descriptors["adjudication"], ("COMMAND-MONITOR %s 0\n" % generation).encode())
                self.close_descriptor(descriptors["adjudication"])
                self.close_descriptor(descriptors["control"])
                status, owner, elapsed = self.finish_owner(case)
                self.assertEqual(status, 128 + signum)
                self.assertEqual(owner["cancellation_signal"], str(signum))
                self.assertEqual(owner["command_status"], str(128 + signum))
                self.assertEqual(owner["native_status"], "0")
                self.assertEqual(owner["term_native_attempted"], "1")
                self.assertEqual(owner["native_reaped"], "1")
                self.assertEqual(owner["keeper_reaped"], "1")
                self.assertEqual(owner["group_authority_released"], "1")
                self.assertEqual(owner["caller_lost"], "0")
                self.assertTrue((state / "native-term").exists())
                self.assertLess(elapsed, 3)

    def test_malformed_go_and_partial_monitor_frames_cannot_admit_native_success(self):
        marker = self.root / "malformed-go-must-not-run"
        command = [sys.executable, "-c", "import pathlib,sys;pathlib.Path(sys.argv[1]).touch()", str(marker)]
        case = self.owner_driver(command, authorize=False)
        process, prefix, descriptors, generation, started = case
        os.write(descriptors["control"], b"GO Wrong123\n")
        self.close_descriptor(descriptors["control"])
        status, owner, elapsed = self.finish_owner(case)
        self.assertEqual(status, 125)
        self.assertEqual(owner["native_launch"], "0")
        self.assertEqual(owner["command_authenticated"], "0")
        self.assertEqual(owner["helper_error"], "BridgeProtocolError")
        self.assertFalse(marker.exists())
        case = self.owner_driver([sys.executable, "-c", "pass"])
        process, prefix, descriptors, generation, started = case
        self.assertEqual(self.read_frame(descriptors["ready"], 3), ("DONE %s\n" % generation).encode())
        os.write(descriptors["adjudication"], ("COMMAND-MONITOR %s 0" % generation).encode())
        self.close_descriptor(descriptors["adjudication"])
        self.close_descriptor(descriptors["control"])
        status, owner, elapsed = self.finish_owner(case)
        self.assertEqual(status, 125)
        self.assertEqual(owner["native_status"], "0")
        self.assertEqual(owner["command_authenticated"], "0")
        self.assertEqual(owner["helper_error"], "BridgeProtocolError")

    def test_duplicate_completion_frame_is_refused_before_snapshot_admission(self):
        state = self.root / "duplicate-completion"
        environment = self.python_overlay(state, "shim", "duplicate", "0", "0")
        status, fields, prefix, private, elapsed, errors = self.finish_cap(
            self.start_cap([sys.executable, "-c", "pass"], environment))
        self.assertEqual(status, 1, errors)
        self.assertEqual(fields["admission"], "0")
        self.assertEqual(fields["reason"], "completion-trailing-data")
        self.assertEqual(fields["helper_status"], "0")
        self.assertFalse(Path(str(prefix) + ".supervisor-status.log").exists())

    def phase_cancellation_case(self, stage, signum):
        state = self.root / ("phase-%s-%d" % (stage, signum))
        state.mkdir()
        shutil.copyfile(HELPER, state / HELPER.name)
        shutil.copyfile(BRIDGE, state / BRIDGE.name)
        markers = state / "markers"
        if stage == "startup":
            environment = self.python_overlay(markers, "startup", "default", "13")
            trigger = markers / "startup-entered"
        else:
            environment = self.python_overlay(markers, "shim", "preserve", "2", "0")
            trigger = markers / "actual-helper-wait-0"
        monitor_shim = state / "timeout-shim"
        monitor_shim.write_text("#!" + sys.executable + "\n"
            "import os,pathlib,signal,subprocess,sys,time\n"
            "real=" + repr(TIMEOUT) + "\n"
            "if '12s' not in sys.argv:os.execv(real,[real,*sys.argv[1:]])\n"
            "owned=subprocess.Popen([real,*sys.argv[1:]],close_fds=True)\n"
            "trigger=pathlib.Path(" + repr(str(trigger)) + ")\n"
            "deadline=time.monotonic()+3\n"
            "while not trigger.exists() and time.monotonic()<deadline:time.sleep(.005)\n"
            "if not trigger.exists():raise RuntimeError('real cancellation stage not reached')\n"
            "owned.send_signal(" + str(signum) + ")\n"
            "raw=owned.wait(timeout=4)\n"
            "status=raw if raw>=0 else 128-raw\n"
            "pathlib.Path(" + repr(str(state / "actual-monitor-status")) + ").write_text(str(status))\n"
            "sys.exit(status)\n")
        monitor_shim.chmod(0o700)
        source = LAUNCHER.read_text()
        phase = source[source.index("run_lifecycle_phase() {"):source.index("simulator_udid_is_valid() {")]
        prefix = state / "console"
        script = state / "phase.sh"
        script.write_text("#!/bin/bash\nset -euo pipefail\nmonitor_command_timeout_seconds=1\n"
            "timeout_bin=" + shlex.quote(str(monitor_shim)) + "\nconsole_log_base=" + shlex.quote(str(prefix))
            + "\ncollect_lifecycle_context() { :; }\n" + phase
            + "\nif run_lifecycle_phase cancellation Test " + shlex.quote(str(prefix))
            + " 1 " + shlex.quote(sys.executable) + " -c pass; then exit 0; else exit $?; fi\n")
        self.bootstrap_bounds.append(time.monotonic() + 1.25)
        with (state / "phase-output").open("wb") as stdout, (state / "phase-errors").open("wb") as stderr:
            process = subprocess.Popen(["bash", str(script)], stdout=stdout, stderr=stderr,
                                       env=environment, close_fds=True)
        self.owned.append(process)
        process.wait(timeout=7)
        errors = (state / "phase-errors").read_bytes()
        self.assertEqual(process.returncode, 128 + signum, errors)
        actual = int((state / "actual-monitor-status").read_text())
        self.assertEqual(actual, 128 + signum, errors)
        status = Path(str(prefix) + ".cancellation.status.log").read_text()
        self.assertIn("outcome=cancelled status=%d" % (128 + signum), status)
        self.assertIn("BUSTER_IOS_CALLER_GATE monitor_status=%d admission=0" % (128 + signum), status)
        self.assertIn("capture_status=124", status)

    def test_extracted_phase_actual_startup_cancellation2_and15(self):
        for signum in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signum=signum):
                self.phase_cancellation_case("startup", signum)

    def test_extracted_phase_actual_publication_cancellation2_and15(self):
        for signum in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signum=signum):
                self.phase_cancellation_case("publication", signum)

    def test_extracted_phase_actual_invocation143_mismatch_remains_evidence_failure(self):
        state = self.root / "phase-invocation143"
        state.mkdir()
        shutil.copyfile(HELPER, state / HELPER.name)
        shutil.copyfile(BRIDGE, state / BRIDGE.name)
        environment = self.python_overlay(state / "markers", "shim", "term", "0", "0")
        source = LAUNCHER.read_text()
        phase = source[source.index("run_lifecycle_phase() {"):source.index("simulator_udid_is_valid() {")]
        prefix = state / "console"
        script = state / "phase.sh"
        script.write_text("#!/bin/bash\nset -euo pipefail\nmonitor_command_timeout_seconds=1\n"
            "timeout_bin=" + shlex.quote(TIMEOUT) + "\nconsole_log_base=" + shlex.quote(str(prefix))
            + "\ncollect_lifecycle_context() { :; }\n" + phase
            + "\nif run_lifecycle_phase control Test " + shlex.quote(str(prefix))
            + " 1 " + shlex.quote(sys.executable) + " -c pass; then exit 0; else exit $?; fi\n")
        process = subprocess.Popen(["bash", str(script)], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   env=environment, close_fds=True)
        self.owned.append(process)
        output, errors = process.communicate(timeout=5)
        self.assertEqual(process.returncode, 1, errors)
        status = Path(str(prefix) + ".control.status.log").read_text()
        self.assertIn("outcome=evidence-failure", status)
        self.assertNotIn("outcome=cancelled", status)
        self.assertIn("BUSTER_IOS_CALLER_GATE monitor_status=1 admission=0 invocation_status=143", status)


if __name__ == "__main__":
    unittest.main()
