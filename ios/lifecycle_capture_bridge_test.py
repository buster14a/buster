#!/usr/bin/env python3
"""Exercise real caller clocks and descriptor custody without a simulator.

BridgeTests runs the selected monitors, the actual Bash collector, and the real owner.
All fixture processes are finite. Cleanup uses live owned Popen handles and
release markers; protocol PID/PGID/SID numbers are never signal targets.
"""

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


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT
HELPER = SOURCE_ROOT / "ios/lifecycle_capture.py"
BRIDGE = SOURCE_ROOT / "ios/lifecycle_capture_bridge.sh"
LAUNCHER = SOURCE_ROOT / "ios/launch_simulator.sh"
TIMEOUT = shutil.which("timeout") or shutil.which("gtimeout")

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
'''


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

    def start_cap(self, command, environment=None, prefix=None, command_seconds=1, capture_seconds=12):
        self.sequence += 1
        prefix = prefix or self.root / ("phase-%d" % self.sequence)
        arguments = [TIMEOUT, "--signal=KILL", "%ds" % capture_seconds, "bash", str(BRIDGE),
                     TIMEOUT, str(prefix), str(command_seconds), str(capture_seconds), "--", *command]
        output = self.root / ("caller-output-%d" % self.sequence)
        errors = self.root / ("caller-errors-%d" % self.sequence)
        started = time.monotonic()
        self.bootstrap_bounds.append(started + command_seconds + 0.25)
        with output.open("wb") as stdout, errors.open("wb") as stderr:
            process = subprocess.Popen(arguments, stdin=subprocess.DEVNULL, stdout=stdout,
                                       stderr=stderr, close_fds=True, env=environment)
        self.owned.append(process)
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
        self.assertTrue(private.is_dir())
        self.assertTrue(str(private).startswith(str(prefix) + ".capture."))
        return status, fields, prefix, private, elapsed, errors.read_bytes()

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
                 (".caller-status.log", ".supervisor-status.log", ".log")]
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
