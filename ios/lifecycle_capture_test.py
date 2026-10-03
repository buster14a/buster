#!/usr/bin/env python3
"""First-party POSIX controls for lifecycle command and output ownership.

Run directly with Python; no simulator or compiler is used. The shell gate
control also exercises the real GNU clock bridge.
LifecycleCaptureTests executes the supervisor with real pipes and children.
Escaped fixtures have finite release markers; tests never signal saved PIDs.
"""

import os
import importlib.util
from pathlib import Path
import re
import signal
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock


HELPER = Path(__file__).resolve().with_name("lifecycle_capture.py")
OUTPUT_LIMIT = 65536
COMMAND_SECONDS = 1
CAPTURE_SECONDS = 12
FIXTURE_LIFETIME_SECONDS = 25
REQUIRED_FIELDS = (
    "command_status", "native_status", "capture_status", "deadline_reached",
    "capture_eof", "cleanup_status", "native_kind", "launch_error",
    "keeper_reaped", "native_reaped", "group_authority_released",
    "cleanup_scope",
    "cancellation_signal", "term_group_attempted", "term_native_attempted",
    "kill_group_attempted", "kill_native_attempted", "keeper_kill_attempted",
    "group_term_result", "group_kill_result", "native_term_result",
    "native_kill_result", "native_observed_after_deadline", "keeper_release",
)
POSITIONAL_FIELDS = (
    "command_status", "native_status", "capture_status", "command_elapsed",
    "capture_elapsed", "deadline_reached", "cleanup_status", "native_kind",
    "launch_error",
)


def wait_for_path(path, seconds):
    deadline = time.monotonic() + seconds
    while not path.exists() and time.monotonic() < deadline:
        time.sleep(0.01)
    return path.exists()


@unittest.skipUnless(os.name == "posix" and hasattr(os, "fork"),
                     "lifecycle supervisor is a POSIX-only Apple CI helper")
class LifecycleCaptureTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(HELPER.is_file(), "lifecycle supervisor source missing")
        self.temporary = tempfile.TemporaryDirectory(prefix="buster-capture-")
        self.root = Path(self.temporary.name)
        self.owned_helpers = []
        self.finite_writers = []
        self.case_number = 0

    def imported_fault_case(self, keeper_exit=None, premature=False, cancel=False, native_source=None):
        specification = importlib.util.spec_from_file_location("capture_fault_control", HELPER)
        module = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(module)
        original_popen = module.subprocess.Popen
        owned = []
        prefix = self.root / "fault-phase"
        keeper_code = (
            "import os,signal,sys\n"
            "ready,control=map(int,sys.argv[1:])\n"
            "signal.signal(signal.SIGTERM,lambda *_:None)\n"
            "signal.signal(signal.SIGINT,lambda *_:None)\n"
            "os.setpgid(0,0)\n"
            "signal.pthread_sigmask(signal.SIG_UNBLOCK,{signal.SIGINT,signal.SIGTERM})\n"
            "os.write(ready,('%d %d\\n'%(os.getpid(),os.getpgrp())).encode())\n"
            + ("" if premature else "while os.read(control,1): pass\n")
            + "os._exit(%s)\n" % keeper_exit
        )

        def popen_control(arguments, **options):
            is_keeper = len(arguments) > 2 and arguments[2] == "--keeper"
            if is_keeper and keeper_exit is not None:
                arguments = [sys.executable, "-c", keeper_code, *arguments[3:]]
            process = original_popen(arguments, **options)
            owned.append(process)
            if is_keeper and cancel:
                # The supervisor's creation/register window has these signals
                # blocked. Delivery happens only after it registers ownership.
                os.kill(os.getpid(), signal.SIGTERM)
            return process

        try:
            with mock.patch.object(module.subprocess, "Popen", side_effect=popen_control):
                result = module.Supervisor(prefix, 1, 12,
                    [sys.executable, "-c", native_source or "import os;os.write(1,b'payload\\n')"]).run()
        finally:
            for process in owned:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=2)
        fields = dict(token.split("=", 1) for token in
                      Path(str(prefix) + ".supervisor-status.log").read_text().split()[1:])
        self.assertEqual(fields["keeper_reaped"], "1")
        self.assertEqual(fields["group_authority_released"], "1")
        self.assertTrue(all(process.returncode is not None for process in owned))
        return result, fields, prefix

    def test_pending_startup_cancellation_registers_and_reaps_only_owned_keeper(self):
        result, fields, prefix = self.imported_fault_case(cancel=True)
        self.assertEqual(result, 143)
        self.assertEqual(fields["native_launch"], "0")
        self.assertEqual(fields["native_status"], "unavailable")
        self.assertEqual(fields["cancellation_signal"], "15")
        self.assertEqual(fields["keeper_release"], "startup-owned-only")
        self.assertEqual(fields["cleanup_status"], "0")
        self.assertEqual(fields["term_group_attempted"], "0")
        self.assertEqual(fields["kill_group_attempted"], "0")

    def test_unexpected_nonzero_keeper_exit_cannot_hide_behind_native_success_and_eof(self):
        result, fields, prefix = self.imported_fault_case(keeper_exit=99)
        self.assertEqual(result, 1)
        self.assertEqual(fields["command_status"], "0")
        self.assertEqual(fields["native_status"], "0")
        self.assertEqual(fields["capture_eof"], "1")
        self.assertEqual(fields["capture_status"], "0")
        self.assertEqual(fields["cleanup_status"], "1")
        self.assertEqual(fields["keeper_status"], "99")
        self.assertEqual(fields["keeper_control_eof_ack"], "0")
        self.assertEqual(Path(str(prefix) + ".log").read_bytes(), b"payload\n")

    def test_premature_zero_keeper_exit_requires_control_release_ack(self):
        result, fields, prefix = self.imported_fault_case(keeper_exit=0, premature=True)
        self.assertEqual(result, 1)
        self.assertEqual(fields["command_status"], "0")
        self.assertEqual(fields["capture_eof"], "1")
        self.assertEqual(fields["cleanup_status"], "1")
        self.assertEqual(fields["keeper_status"], "0")
        self.assertEqual(fields["keeper_control_eof_ack"], "0")

    def test_failed_keeper_with_inherited_writer_cannot_hide_in_group_cleanup(self):
        native = ("import os,subprocess,sys\n"
                  "subprocess.Popen([sys.executable,'-c','import time;time.sleep(4)'])\n"
                  "os.write(1,b'payload\\n')\n")
        for keeper_exit in (0, 99):
            with self.subTest(keeper_exit=keeper_exit):
                result, fields, prefix = self.imported_fault_case(
                    keeper_exit=keeper_exit, premature=True, native_source=native)
                self.assertEqual(result, 1)
                self.assertEqual(fields["command_status"], "0")
                self.assertEqual(fields["capture_eof"], "1")
                self.assertEqual(fields["capture_status"], "0")
                self.assertEqual(fields["cleanup_status"], "1")
                self.assertEqual(fields["keeper_status"], str(keeper_exit))
                self.assertEqual(fields["keeper_release"], "final-group-signal")
                self.assertEqual(fields["kill_group_attempted"], "1")

    def test_deadline_crossing_during_native_or_eof_observation_is_not_accepted_early(self):
        specification = importlib.util.spec_from_file_location("capture_clock_control", HELPER)
        module = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(module)
        supervisor = module.Supervisor(self.root / "clock-phase", 1, 12, ["unused"])
        supervisor.started = 0
        supervisor.command_deadline = 100
        supervisor.capture_deadline = 100
        supervisor.native = mock.Mock()
        supervisor.native.poll.return_value = 0
        with mock.patch.object(module.time, "monotonic_ns", return_value=101):
            supervisor.observe_native(99)
        self.assertEqual(supervisor.facts["command_status"], 124)
        self.assertEqual(supervisor.facts["native_status"], 0)
        self.assertEqual(supervisor.facts["native_observed_after_deadline"], 1)
        for cancellation in (signal.SIGINT, signal.SIGTERM):
            cancelled = module.Supervisor(self.root / "cancel-clock-phase", 1, 12, ["unused"])
            cancelled.started = 0
            cancelled.command_deadline = 100
            cancelled.cancellation = cancellation
            cancelled.facts["cancellation_signal"] = cancellation
            cancelled.facts["command_status"] = 128 + cancellation
            cancelled.native = mock.Mock()
            cancelled.native.poll.return_value = 0
            with mock.patch.object(module.time, "monotonic_ns", return_value=101):
                cancelled.observe_native(99)
            self.assertEqual(cancelled.facts["command_status"], 128 + cancellation)
            self.assertEqual(cancelled.facts["deadline_reached"], 1)
            self.assertEqual(cancelled.facts["native_status"], 0)
            newly_cancelled = module.Supervisor(self.root / "new-cancel-clock-phase", 1, 12, ["unused"])
            newly_cancelled.started = 0
            newly_cancelled.command_deadline = 100
            newly_cancelled.native = mock.Mock()
            def record_intent_during_poll():
                newly_cancelled.note_signal(cancellation, None)
                return 0
            newly_cancelled.native.poll.side_effect = record_intent_during_poll
            with mock.patch.object(module.time, "monotonic_ns", return_value=101):
                newly_cancelled.observe_native(99)
            self.assertEqual(newly_cancelled.facts["command_status"], 128 + cancellation)
            self.assertEqual(newly_cancelled.facts["cancellation_signal"], cancellation)
            self.assertEqual(newly_cancelled.facts["deadline_reached"], 1)
        read_fd, write_fd = os.pipe()
        os.close(write_fd)
        supervisor.output_fd = read_fd
        os.set_blocking(read_fd, False)
        with mock.patch.object(module.time, "monotonic_ns", side_effect=[99, 101]):
            supervisor.drain(99)
        self.assertIsNone(supervisor.output_fd)
        self.assertEqual(supervisor.facts["capture_eof"], 0)
        self.assertEqual(supervisor.facts["capture_eof_late"], 1)
        self.assertEqual(supervisor.facts["capture_status"], 124)

    def test_signal_intent_during_terminal_publication_is_reconciled_without_new_authority(self):
        specification = importlib.util.spec_from_file_location("capture_publish_control", HELPER)
        module = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(module)
        prefix = self.root / "publish-phase"
        supervisor = module.Supervisor(prefix, 1, 12, [sys.executable, "-c", "pass"])
        original_publish = supervisor.publish
        snapshots = []
        def publish_then_signal():
            snapshots.append(supervisor.group_authority)
            original_publish()
            if len(snapshots) == 1:
                supervisor.note_signal(signal.SIGTERM, None)
        with mock.patch.object(supervisor, "publish", side_effect=publish_then_signal):
            result = supervisor.run()
        self.assertEqual(result, 143)
        self.assertEqual(snapshots, [False, False])
        fields = dict(token.split("=", 1) for token in
                      Path(str(prefix) + ".supervisor-status.log").read_text().split()[1:])
        self.assertEqual(fields["command_status"], "143")
        self.assertEqual(fields["native_status"], "0")
        self.assertEqual(fields["cancellation_signal"], "15")
        self.assertEqual(fields["capture_eof"], "1")
        self.assertEqual(fields["cleanup_status"], "0")

    def test_post_registration_fault_keeps_escaped_native_owned_and_failure_latched(self):
        for fault, cooperative in (("spawn", False), ("spawn", True), ("close", False), ("close", True)):
            with self.subTest(fault=fault, cooperative=cooperative):
                specification = importlib.util.spec_from_file_location("capture_registered_control", HELPER)
                module = importlib.util.module_from_spec(specification)
                specification.loader.exec_module(module)
                state = self.root / ("registered-%s-%s" % (fault, cooperative))
                state.mkdir()
                ready, release = state / "ready", state / "release"
                native = ("import os,signal,sys,time\nfrom pathlib import Path\n"
                          "os.setsid()\nos.close(1);os.close(2)\n"
                          + ("signal.signal(signal.SIGTERM,lambda *_:sys.exit(0))\n" if cooperative else "")
                          + "Path(sys.argv[1]).touch()\n"
                          "deadline=time.monotonic()+5\n"
                          "while not Path(sys.argv[2]).exists() and time.monotonic()<deadline:time.sleep(.01)\n")
                supervisor = module.Supervisor(state / "phase", 1, 12,
                    [sys.executable, "-c", native, str(ready), str(release)])
                original_spawn, original_close = supervisor.spawn, module.os.close
                original_close_fd = supervisor.close_fd
                watched_output_fd = None
                injected = False
                def spawn_fault():
                    original_spawn()
                    self.assertTrue(wait_for_path(ready, 2))
                    if cooperative:
                        release.touch()
                        deadline = time.monotonic() + 1
                        while supervisor.native.poll() is None and time.monotonic() < deadline:
                            time.sleep(.01)
                        self.assertEqual(supervisor.native.returncode, 0)
                    raise OSError("injected post-registration bookkeeping failure")
                def remember_close(attribute):
                    nonlocal watched_output_fd
                    if attribute == "output_write_fd" and supervisor.native is not None:
                        watched_output_fd = supervisor.output_write_fd
                    original_close_fd(attribute)
                def close_fault(descriptor):
                    nonlocal injected
                    if (not injected and supervisor.native is not None
                        and descriptor == watched_output_fd):
                        injected = True
                        self.assertTrue(wait_for_path(ready, 2))
                        original_close(descriptor)
                        raise InterruptedError("injected retired descriptor close failure")
                    return original_close(descriptor)
                try:
                    if fault == "spawn":
                        with mock.patch.object(supervisor, "spawn", side_effect=spawn_fault):
                            result = supervisor.run()
                    else:
                        with mock.patch.object(module.os, "close", side_effect=close_fault), \
                            mock.patch.object(supervisor, "close_fd", side_effect=remember_close):
                            result = supervisor.run()
                finally:
                    release.touch()
                    if supervisor.native is not None and supervisor.native.poll() is None:
                        supervisor.native.kill()
                        supervisor.native.wait(timeout=2)
                self.assertEqual(result, 125)
                fields = supervisor.facts
                self.assertNotEqual(fields["helper_error"], "none")
                self.assertEqual(fields["command_status"], 125)
                self.assertEqual(fields["native_reaped"], 1)
                self.assertEqual(fields["keeper_reaped"], 1)
                self.assertEqual(fields["group_authority_released"], 1)
                self.assertEqual(fields["cleanup_status"], 1)
                self.assertEqual(fields["term_native_attempted"], 1)
                self.assertEqual(fields["capture_eof"], 1)
                self.assertIsNotNone(supervisor.native.returncode)
                if cooperative:
                    self.assertEqual(fields["native_status"], 0)

    def test_short_capture_write_cannot_claim_exact_retention(self):
        specification = importlib.util.spec_from_file_location("capture_write_control", HELPER)
        module = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(module)
        supervisor = module.Supervisor(self.root / "short-phase", 1, 12, ["unused"])
        supervisor.started = time.monotonic_ns()
        supervisor.capture_deadline = supervisor.started + 1_000_000_000
        read_fd, write_fd = os.pipe()
        os.write(write_fd, b"ABCD")
        os.close(write_fd)
        supervisor.output_fd = read_fd
        os.set_blocking(read_fd, False)
        path = self.root / "short-output"
        with path.open("wb", buffering=0) as actual:
            class ShortWriter:
                def write(self, data):
                    return actual.write(data[:-1])
            supervisor.output = ShortWriter()
            try:
                with self.assertRaises(OSError):
                    supervisor.drain(time.monotonic_ns())
            finally:
                os.close(read_fd)
                supervisor.output_fd = None
        self.assertEqual(path.read_bytes(), b"ABC")
        self.assertEqual(supervisor.retained_bytes, 3)
        self.assertEqual(supervisor.total_bytes, 4)
        self.assertEqual(supervisor.facts["capture_eof"], 0)

    def test_shell_gate_refuses_missing_malformed_or_conflicting_supervisor_receipts(self):
        launcher = HELPER.with_name("launch_simulator.sh").read_text()
        phase_function = launcher[launcher.index("run_lifecycle_phase() {"):
                                  launcher.index("simulator_udid_is_valid() {")]
        modes = ("valid", "missing-fields", "missing-named", "extra-field",
                 "named-mismatch", "duplicate-key", "duplicate-eof", "duplicate-native-reap",
                 "duplicate-keeper-reap", "duplicate-authority", "missing-ownership",
                 "hidden-native-failure", "unattributed-success", "false-deadline-success",
                 "false-cancel-success",
                 "wrong-native", "wrong-helper", "missing-capture", "false-eof", "cleanup-failure",
                 "duplicate-version", "coherent-monitor125-success") + tuple("v5-%s-%s" % (fault, key)
                    for key in ("bridge_generation", "command_monitor_status", "command_authenticated", "caller_lost")
                    for fault in ("missing", "duplicate", "malformed", "wrong"))
        helper_source = '''import pathlib,sys
mode=MODE
prefix=pathlib.Path(sys.argv[sys.argv.index('--prefix')+1])
def write(suffix,value): pathlib.Path(str(prefix)+suffix).write_text(value)
write('.log','payload\\n')
cleanup=1 if mode=='cleanup-failure' else 0
fields='0 0 0 0 0 0 %d exit none\\n'%cleanup
named=('BUSTER_IOS_SUPERVISOR version=1 command_status=0 native_status=0 '
       'capture_status=0 command_elapsed=0 capture_elapsed=0 deadline_reached=0 '
       'cleanup_status=%d native_kind=exit launch_error=none capture_eof=1 '
       'native_reaped=1 keeper_reaped=1 group_authority_released=1 '
       'native_launch=1 cancellation_signal=0 helper_error=none\\n')%cleanup
generation=sys.argv[sys.argv.index('--bridge-generation')+1]
v5={'bridge_generation':generation,'command_monitor_status':'0','command_authenticated':'1','caller_lost':'0'}
named=named.strip()+''.join(' %s=%s'%item for item in v5.items())+'\\n'
if mode.startswith('v5-'):
 _,fault,key=mode.split('-',2)
 token=' %s=%s'%(key,v5[key])
 if fault=='missing': named=named.replace(token,'')
 elif fault=='duplicate': named=named.strip()+token+'\\n'
 elif fault=='malformed': named=named.replace(token,' %s=invalid'%key)
 else:
  wrong={'bridge_generation':'wrongGEN000','command_monitor_status':'137','command_authenticated':'0','caller_lost':'1'}
  named=named.replace(token,' %s=%s'%(key,wrong[key]))
if mode=='duplicate-version': named=named.strip()+' version=1\\n'
if mode=='coherent-monitor125-success':
 named=named.replace('command_monitor_status=0','command_monitor_status=125').replace('command_authenticated=1','command_authenticated=0')
 (prefix.parent/'command-monitor-status').write_text('125\\n')
if mode=='extra-field': fields=fields.strip()+' extra\\n'
if mode=='named-mismatch': named=named.replace('command_status=0','command_status=70')
if mode=='duplicate-key': named=named.strip()+' command_status=70\\n'
if mode=='duplicate-eof': named=named.strip()+' capture_eof=0\\n'
if mode=='duplicate-native-reap': named=named.strip()+' native_reaped=0\\n'
if mode=='duplicate-keeper-reap': named=named.strip()+' keeper_reaped=0\\n'
if mode=='duplicate-authority': named=named.strip()+' group_authority_released=0\\n'
if mode=='missing-ownership': named=named.replace(' keeper_reaped=1','')
native='70' if mode in ('wrong-native','hidden-native-failure') else '0'
if mode=='hidden-native-failure':
 fields=fields.replace('0 0 0','0 70 0',1)
 named=named.replace('native_status=0','native_status=70')
if mode=='unattributed-success':
 fields='0 unavailable 0 0 0 0 0 unavailable none\\n'
 named=named.replace('native_status=0','native_status=unavailable').replace(
   'native_kind=exit','native_kind=unavailable').replace('native_launch=1','native_launch=0')
if mode=='false-deadline-success':
 fields='0 0 0 0 0 1 0 exit none\\n'
 named=named.replace('deadline_reached=0','deadline_reached=1')
if mode=='false-cancel-success': named=named.replace('cancellation_signal=0','cancellation_signal=15')
if mode=='false-eof': named=named.replace('capture_eof=1','capture_eof=0')
if mode!='missing-fields': write('.supervisor-fields.log',fields)
if mode!='missing-named': write('.supervisor-status.log',named)
if mode!='unattributed-success': write('.native-status.log',native+'\\n')
if mode!='missing-capture': write('.log.capture-status.log',
   'BUSTER_IOS_CAPTURE total_bytes=8 retained_bytes=8 truncated=0\\n')
sys.exit(70 if mode=='wrong-helper' else cleanup)
'''
        for mode in modes:
            with self.subTest(mode=mode):
                state = self.root / ("shell-" + mode)
                state.mkdir()
                mutation = helper_source.replace("MODE", repr(mode)).rsplit("sys.exit", 1)[0]
                # Keep real READY/DONE, completion and actual exit custody.
                # Receipt faults happen inside publication, before terminal
                # status/EOF, rather than bypassing the new bridge protocol.
                state.joinpath("lifecycle_capture.py").write_text(
                    "import importlib.util,pathlib,sys\n"
                    + "spec=importlib.util.spec_from_file_location('real_capture'," + repr(str(HELPER)) + ")\n"
                    + "module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)\n"
                    + "original=module.Supervisor.publish\n"
                    + "def publish(owner):\n"
                    + (" owner.facts['cleanup_status']=1\n" if mode == "cleanup-failure" else "")
                    + " original(owner)\n"
                    + " for suffix in ('.native-status.log','.log.capture-status.log','.supervisor-fields.log','.supervisor-status.log'):\n"
                    + "  pathlib.Path(str(owner.prefix)+suffix).unlink(missing_ok=True)\n"
                    + " exec(" + repr(mutation) + ",{})\n"
                    + "module.Supervisor.publish=publish\n"
                    + "result=module.main()\n"
                    + ("sys.exit(70)\n" if mode == "wrong-helper" else "sys.exit(result)\n"))
                state.joinpath("lifecycle_capture_bridge.sh").write_bytes(HELPER.with_name("lifecycle_capture_bridge.sh").read_bytes())
                timeout_tool = shutil.which("timeout") or shutil.which("gtimeout")
                self.assertIsNotNone(timeout_tool, "GNU timeout is required for shell custody controls")
                prefix = state / "console"
                script = state / "phase.sh"
                script.write_text("#!/usr/bin/env bash\nset -euo pipefail\n"
                    + "monitor_command_timeout_seconds=1\nconsole_log_base=" + shlex.quote(str(prefix)) + "\n"
                    + "timeout_bin=" + shlex.quote(timeout_tool) + "\n"
                    + "collect_lifecycle_context() { :; }\n" + phase_function
                    + "\nif run_lifecycle_phase diagnostic-control Test " + shlex.quote(str(prefix))
                    + " 1 /usr/bin/true; then exit 0; else exit $?; fi\n")
                result = subprocess.run(["bash", str(script)], capture_output=True, timeout=5)
                status = Path(str(prefix) + ".diagnostic-control.status.log").read_text()
                self.assertEqual(result.returncode, 0 if mode == "valid" else 1,
                                 result.stderr.decode(errors="replace"))
                if mode == "cleanup-failure":
                    self.assertIn("outcome=cleanup-failure status=0 native_status=0 capture_status=0", status)
                    self.assertIn("capture_receipt=complete", status)
                elif mode == "valid":
                    self.assertIn("outcome=success", status)
                else:
                    self.assertIn("outcome=evidence-failure", status)
                if mode.startswith("v5-") or mode in ("duplicate-version", "coherent-monitor125-success"):
                    self.assertIn("BUSTER_IOS_CALLER_GATE monitor_status=0 admission=1 invocation_status=0", status)
                    self.assertIn("helper_status=0 supervisor_valid=0", status)

    def test_caller_receipt_requires_unique_typed_coherent_terminal_facts(self):
        launcher = HELPER.with_name("launch_simulator.sh").read_text()
        phase = launcher[launcher.index("run_lifecycle_phase() {"):launcher.index("simulator_udid_is_valid() {")]
        keys = ("version", "generation", "admission", "helper_status", "invocation_status", "command_monitor_status", "reason")
        compact_keys = ("admission", "helper_status", "invocation_status", "generation", "command_monitor_status", "reason")
        modes = ("valid", "missing-named", "missing-fields", "extra-fields") \
            + tuple("named-%s-%s" % (fault, key) for key in keys for fault in ("missing", "duplicate", "malformed")) \
            + tuple("compact-malformed-" + key for key in compact_keys) \
            + tuple("coherent-" + key for key in compact_keys)
        timeout_tool = shutil.which("timeout") or shutil.which("gtimeout")
        self.assertIsNotNone(timeout_tool)
        for mode in modes:
            with self.subTest(mode=mode):
                state = self.root / ("caller-" + mode)
                state.mkdir()
                for name in ("lifecycle_capture.py", "lifecycle_capture_bridge.sh"):
                    state.joinpath(name).write_bytes(HELPER.with_name(name).read_bytes())
                marker = state / "fault-applied"
                wrapper = state / "timeout"
                wrapper.write_text("#!/usr/bin/env python3\n" + "REAL=" + repr(timeout_tool) + "\n"
                    + "MODE=" + repr(mode) + "\nMARKER=" + repr(str(marker)) + "\n" + r'''
import os,pathlib,subprocess,sys
args=sys.argv[1:]
if '--bootstrap' in args: os.execv(REAL,[REAL,*args])
status=subprocess.run([REAL,*args]).returncode
if status: sys.exit(status if status>=0 else 128-status)
prefix=pathlib.Path(args[5])
named_path=pathlib.Path(str(prefix)+'.caller-status.log')
fields_path=pathlib.Path(str(prefix)+'.caller-fields.log')
fields=fields_path.read_text().split()
named=named_path.read_text().strip().split()
indices={'admission':0,'helper_status':1,'invocation_status':2,'generation':3,'command_monitor_status':4,'reason':5}
if MODE.startswith('named-'):
 _,fault,key=MODE.split('-',2)
 original=next(token for token in named if token.startswith(key+'='))
 if fault=='missing': named.remove(original)
 elif fault=='duplicate': named.append(original)
 else: named[named.index(original)]=key+'=invalid!'
elif MODE.startswith('compact-malformed-'):
 key=MODE[len('compact-malformed-'):]
 fields[indices[key]]='invalid!'
elif MODE.startswith('coherent-'):
 key=MODE[len('coherent-'):]
 wrong={'admission':'0','helper_status':'70','invocation_status':'70','generation':'wrongGEN000','command_monitor_status':'137','reason':'starting'}[key]
 fields[indices[key]]=wrong
 original=next(token for token in named if token.startswith(key+'='))
 named[named.index(original)]=key+'='+wrong
 if key=='helper_status':
  fields[2]='70'
  original=next(token for token in named if token.startswith('invocation_status='))
  named[named.index(original)]='invocation_status=70'
named_path.write_text(' '.join(named)+'\n')
fields_path.write_text(' '.join(fields)+(' extra' if MODE=='extra-fields' else '')+'\n')
if MODE=='missing-named': named_path.unlink()
if MODE=='missing-fields': fields_path.unlink()
pathlib.Path(MARKER).write_text(MODE)
sys.exit(0)
''')
                wrapper.chmod(0o755)
                prefix = state / "console"
                script = state / "phase.sh"
                script.write_text("#!/usr/bin/env bash\nset -euo pipefail\nmonitor_command_timeout_seconds=1\n"
                    + "timeout_bin=" + shlex.quote(str(wrapper)) + "\nconsole_log_base=" + shlex.quote(str(prefix)) + "\n"
                    + "collect_lifecycle_context() { :; }\n" + phase
                    + "\nif run_lifecycle_phase diagnostic-control Test " + shlex.quote(str(prefix))
                    + " 1 /usr/bin/true; then exit 0; else exit $?; fi\n")
                result = subprocess.run(["bash", str(script)], capture_output=True, timeout=5)
                self.assertTrue(marker.exists(), result.stderr.decode(errors="replace"))
                self.assertEqual(marker.read_text(), mode, "the intended fault must actually reach the real closed receipt")
                status = Path(str(prefix) + ".diagnostic-control.status.log").read_text()
                self.assertEqual(result.returncode, 0 if mode == "valid" else 1, result.stderr.decode(errors="replace"))
                self.assertIn("outcome=success" if mode == "valid" else "outcome=evidence-failure", status)

    def tearDown(self):
        # Every signal targets a still-owned direct Popen handle. No receipt PID
        # is used for cleanup; escaped writer fixtures exit on their own marker.
        for ready, release, acknowledged, escaped in self.finite_writers:
            release.touch()
        for process in self.owned_helpers:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.communicate(timeout=15)
                except subprocess.TimeoutExpired:
                    if process.poll() is None:
                        process.kill()
                    process.communicate(timeout=5)
        for ready, release, acknowledged, escaped in self.finite_writers:
            if escaped and ready.exists():
                self.assertTrue(wait_for_path(acknowledged, 5),
                                "finite escaped writer did not acknowledge release")
        self.temporary.cleanup()

    def start(self, command, command_seconds=COMMAND_SECONDS,
              capture_seconds=CAPTURE_SECONDS):
        self.case_number += 1
        prefix = self.root / ("phase-%d" % self.case_number)
        arguments = [
            sys.executable, str(HELPER), "--prefix", str(prefix),
            "--command-seconds", str(command_seconds), "--capture-seconds",
            str(capture_seconds), "--", *command,
        ]
        started = time.monotonic()
        process = subprocess.Popen(arguments, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, close_fds=True)
        self.owned_helpers.append(process)
        return process, prefix, started

    def finish(self, process, prefix, started, timeout=17):
        output, errors = process.communicate(timeout=timeout)
        elapsed = time.monotonic() - started
        named = Path(str(prefix) + ".supervisor-status.log").read_text()
        lines = named.splitlines()
        self.assertEqual(len(lines), 1, named)
        tokens = lines[0].split()
        self.assertEqual(tokens[0], "BUSTER_IOS_SUPERVISOR", named)
        fields = {}
        for token in tokens[1:]:
            self.assertIn("=", token, named)
            key, value = token.split("=", 1)
            self.assertNotIn(key, fields, named)
            fields[key] = value
        self.assertTrue(all(key in fields for key in REQUIRED_FIELDS), named)
        self.assertEqual(fields.get("version"), "1", named)
        positional = Path(str(prefix) + ".supervisor-fields.log").read_text().split()
        self.assertEqual(len(positional), len(POSITIONAL_FIELDS), positional)
        for key, value in zip(POSITIONAL_FIELDS, positional):
            self.assertEqual(value, fields[key], key)
        for key in ("command_elapsed", "capture_elapsed"):
            receipt = ".%s.log" % key.replace("_", "-")
            self.assertEqual(Path(str(prefix) + receipt).read_text().strip(), fields[key])
        for key in ("deadline_reached", "capture_eof", "keeper_reaped", "native_reaped",
                    "group_authority_released", "term_group_attempted", "term_native_attempted",
                    "kill_group_attempted", "kill_native_attempted", "keeper_kill_attempted",
                    "native_observed_after_deadline"):
            self.assertIn(fields[key], ("0", "1"), key)
        self.assertEqual(fields["group_authority_released"], "1", named)
        self.assertEqual(fields["keeper_reaped"], "1", named)
        self.assertEqual(fields["cleanup_status"], "0", named)
        self.assertLess(elapsed, CAPTURE_SECONDS + 4, named)
        return process.returncode, fields, prefix, elapsed, output, errors

    def run_command(self, command, **arguments):
        process, prefix, started = self.start(command, **arguments)
        return self.finish(process, prefix, started)

    def python_command(self, source, *arguments):
        return [sys.executable, "-c", source, *(str(value) for value in arguments)]

    def assert_complete_capture(self, fields, prefix, expected):
        self.assertEqual(fields["capture_status"], "0")
        self.assertEqual(fields["capture_eof"], "1")
        self.assertEqual(Path(str(prefix) + ".log").read_bytes(),
                         expected[:OUTPUT_LIMIT])
        receipt = Path(str(prefix) + ".log.capture-status.log").read_text()
        pattern = (r"^BUSTER_IOS_CAPTURE total_bytes=(\d+) retained_bytes=(\d+) "
                   r"truncated=([01])\s*$")
        match = re.fullmatch(pattern, receipt)
        self.assertIsNotNone(match, receipt)
        self.assertEqual(int(match.group(1)), len(expected))
        self.assertEqual(int(match.group(2)), min(len(expected), OUTPUT_LIMIT))
        self.assertEqual(match.group(3), "1" if len(expected) > OUTPUT_LIMIT else "0")

    def assert_native(self, fields, prefix, status, kind="exit"):
        self.assertEqual(fields["native_status"], str(status))
        self.assertEqual(fields["native_kind"], kind)
        self.assertEqual(fields["native_reaped"], "1")
        self.assertEqual(Path(str(prefix) + ".native-status.log").read_text().strip(),
                         str(status))

    def test_normal_and_large_binary_output_preserve_exact_prefix(self):
        for repetitions in (1, 1024):
            with self.subTest(repetitions=repetitions):
                expected = bytes(range(256)) * repetitions
                source = ("import os,sys\n"
                          "data=bytes(range(256))*int(sys.argv[1])\n"
                          "while data:\n"
                          "    data=data[os.write(1,data):]\n")
                status, fields, prefix, elapsed, output, errors = self.run_command(
                    self.python_command(source, repetitions))
                self.assertEqual(status, 0, errors)
                self.assertEqual(fields["command_status"], "0")
                self.assertEqual(fields["deadline_reached"], "0")
                # Native exit can be observed before the last queued bytes or
                # EOF. The cleanup trigger records observation order, not a
                # claim that a descendant survived the command.
                self.assertIn(fields["cleanup_scope"],
                              ("keeper-only", "private-group-and-direct-native"))
                self.assert_native(fields, prefix, 0)
                self.assert_complete_capture(fields, prefix, expected)

    def test_actual_native_status_family_is_not_a_launch_or_deadline_guess(self):
        for native in (1, 70, 124, 125, 126, 127, 137):
            with self.subTest(native=native):
                status, fields, prefix, elapsed, output, errors = self.run_command(
                    self.python_command("import sys; sys.exit(int(sys.argv[1]))", native))
                self.assertNotEqual(status, 0, errors)
                self.assertEqual(fields["command_status"], str(native))
                self.assertEqual(fields["deadline_reached"], "0")
                self.assertEqual(fields["launch_error"], "none")
                self.assertEqual(fields["cancellation_signal"], "0")
                self.assert_native(fields, prefix, native)
                self.assert_complete_capture(fields, prefix, b"")

    def test_actual_signal_status_137_is_distinguished_from_native_exit_137(self):
        status, fields, prefix, elapsed, output, errors = self.run_command(
            self.python_command("import os,signal; os.kill(os.getpid(),signal.SIGKILL)"))
        self.assertNotEqual(status, 0, errors)
        self.assertEqual(fields["command_status"], "137")
        self.assertEqual(fields["deadline_reached"], "0")
        self.assertEqual(fields["launch_error"], "none")
        self.assert_native(fields, prefix, 137, "signal")
        self.assert_complete_capture(fields, prefix, b"")

    def test_missing_and_permission_denied_launch_have_no_native_exit_receipt(self):
        denied = self.root / "not-executable"
        denied.write_text("#!/bin/sh\nexit 0\n")
        denied.chmod(0o600)
        bad_format = self.root / "bad-executable-format"
        bad_format.write_text("not an executable binary\n")
        bad_format.chmod(0o700)
        for command, command_status, launch_error in ((self.root / "missing", 127, "ENOENT"),
                                                     (denied, 126, "EACCES"),
                                                     (bad_format, 126, "ENOEXEC")):
            with self.subTest(command_status=command_status):
                status, fields, prefix, elapsed, output, errors = self.run_command([str(command)])
                self.assertNotEqual(status, 0, errors)
                self.assertEqual(fields["command_status"], str(command_status))
                self.assertEqual(fields["native_status"], "unavailable")
                self.assertEqual(fields["native_kind"], "unavailable")
                self.assertEqual(fields["launch_error"], launch_error)
                self.assertFalse(Path(str(prefix) + ".native-status.log").exists())
                self.assertEqual(fields["deadline_reached"], "0")

    def test_integer_domains_and_capture_grace_are_rejected_before_command_launch(self):
        marker = self.root / "must-not-launch"
        source = "from pathlib import Path; import sys; Path(sys.argv[1]).touch()"
        cases = (("0", "12"), ("-1", "12"), ("1.0", "12"), ("x", "12"),
                 ("9223372036854775808", "9223372036854775820"),
                 ("1", "9223372036854775808"), ("1", "0"),
                 ("1", "-12"), ("1", "12.0"), ("1", "x"), ("1", "11"))
        for command_seconds, capture_seconds in cases:
            with self.subTest(command_seconds=command_seconds, capture_seconds=capture_seconds):
                process, prefix, started = self.start(self.python_command(source, marker),
                                                     command_seconds, capture_seconds)
                output, errors = process.communicate(timeout=5)
                self.assertNotEqual(process.returncode, 0, errors)
                self.assertFalse(marker.exists())

    def test_valid_signed_64_bit_budget_is_not_converted_to_an_unbounded_float_wait(self):
        status, fields, prefix, elapsed, output, errors = self.run_command(
            self.python_command("pass"), command_seconds=9223372036854775796,
            capture_seconds=9223372036854775807)
        self.assertEqual(status, 0, errors)
        self.assertEqual(fields["command_status"], "0")
        self.assertEqual(fields["deadline_reached"], "0")
        self.assert_native(fields, prefix, 0)
        self.assert_complete_capture(fields, prefix, b"")

    def test_explicit_deadline_survives_a_term_handler_native_exit_zero(self):
        source = ("import signal,time\n"
                  "signal.signal(signal.SIGTERM,lambda number,frame:exit(0))\n"
                  "while True: time.sleep(.01)\n")
        status, fields, prefix, elapsed, output, errors = self.run_command(
            self.python_command(source))
        self.assertNotEqual(status, 0, errors)
        self.assertEqual(fields["command_status"], "124")
        self.assertEqual(fields["deadline_reached"], "1")
        self.assertEqual(fields["native_observed_after_deadline"], "1")
        self.assertEqual(fields["term_group_attempted"], "1")
        self.assertEqual(fields["cleanup_scope"], "private-group-and-direct-native")
        self.assert_native(fields, prefix, 0)
        self.assert_complete_capture(fields, prefix, b"")

    def held_writer_command(self, ignore_term=False, escape=False):
        state = self.root / ("writer-%d" % self.case_number)
        state.mkdir()
        ready = state / "ready"
        release = state / "release"
        acknowledged = state / "released"
        self.finite_writers.append((ready, release, acknowledged, escape))
        source = (
            "import os,signal,sys,time\n"
            "from pathlib import Path\n"
            "ready,release,acknowledged=map(Path,sys.argv[1:4])\n"
            "child=os.fork()\n"
            "if child==0:\n"
            "    if sys.argv[5]=='1': os.setsid()\n"
            "    if sys.argv[4]=='1': signal.signal(signal.SIGTERM,signal.SIG_IGN)\n"
            "    ready.touch()\n"
            "    deadline=time.monotonic()+%d\n"
            "    while not release.exists() and time.monotonic()<deadline: time.sleep(.01)\n"
            "    os.close(1); os.close(2)\n"
            "    acknowledged.touch()\n"
            "    os._exit(0)\n"
            "deadline=time.monotonic()+3\n"
            "while not ready.exists() and time.monotonic()<deadline: time.sleep(.01)\n"
            "os._exit(0 if ready.exists() else 91)\n"
        ) % FIXTURE_LIFETIME_SECONDS
        return self.python_command(source, ready, release, acknowledged,
                                   int(ignore_term), int(escape)), release, acknowledged

    def test_native_completion_with_inherited_writer_reaches_real_eof_after_owned_cleanup(self):
        command, release, acknowledged = self.held_writer_command()
        status, fields, prefix, elapsed, output, errors = self.run_command(command)
        self.assertEqual(status, 0, errors)
        self.assertEqual(fields["command_status"], "0")
        self.assertEqual(fields["deadline_reached"], "0")
        self.assertEqual(fields["cleanup_scope"], "private-group-and-direct-native")
        self.assertEqual(fields["term_group_attempted"], "1")
        self.assert_native(fields, prefix, 0)
        self.assert_complete_capture(fields, prefix, b"")
        self.assertLess(elapsed, 3)

    def test_term_ignoring_inherited_writer_is_killed_within_existing_grace(self):
        command, release, acknowledged = self.held_writer_command(ignore_term=True)
        status, fields, prefix, elapsed, output, errors = self.run_command(command)
        self.assertEqual(status, 0, errors)
        self.assertEqual(fields["deadline_reached"], "0")
        self.assert_native(fields, prefix, 0)
        self.assert_complete_capture(fields, prefix, b"")
        self.assertEqual(fields["term_group_attempted"], "1")
        self.assertEqual(fields["kill_group_attempted"], "1")
        self.assertEqual(fields["group_kill_result"], "sent")
        self.assertLess(elapsed, 11.5)

    def test_escaped_direct_native_remains_covered_by_owned_handle(self):
        source = ("import os,signal,time\n"
                  "os.setsid()\n"
                  "signal.signal(signal.SIGTERM,lambda number,frame:exit(0))\n"
                  "while True: time.sleep(.01)\n")
        status, fields, prefix, elapsed, output, errors = self.run_command(
            self.python_command(source))
        self.assertNotEqual(status, 0, errors)
        self.assertEqual(fields["command_status"], "124")
        self.assertEqual(fields["deadline_reached"], "1")
        self.assertEqual(fields["term_native_attempted"], "1")
        self.assert_native(fields, prefix, 0)
        self.assert_complete_capture(fields, prefix, b"")
        self.assertLess(elapsed, 3)

    def test_escaped_grandchild_pipe_is_incomplete_and_released_by_fixture_only(self):
        command, release, acknowledged = self.held_writer_command(ignore_term=True, escape=True)
        try:
            status, fields, prefix, elapsed, output, errors = self.run_command(command)
            self.assertNotEqual(status, 0, errors)
            self.assertEqual(fields["command_status"], "0")
            self.assertEqual(fields["deadline_reached"], "0")
            self.assertEqual(fields["capture_status"], "124")
            self.assertEqual(fields["capture_eof"], "0")
            self.assertEqual(fields["cleanup_scope"], "private-group-and-direct-native")
            self.assertEqual(fields["term_group_attempted"], "1")
            self.assertEqual(fields["kill_group_attempted"], "1")
            self.assert_native(fields, prefix, 0)
            self.assertFalse(Path(str(prefix) + ".log.capture-status.log").exists())
            self.assertGreaterEqual(elapsed, CAPTURE_SECONDS - 0.2)
            self.assertFalse(acknowledged.exists(), "fixture expired before release")
        finally:
            release.touch()
        self.assertTrue(wait_for_path(acknowledged, 5))

    def test_active_cancellation_retains_owned_cleanup_and_signal_result(self):
        ready = self.root / "active-native"
        source = ("import sys,time\n"
                  "from pathlib import Path\n"
                  "Path(sys.argv[1]).touch()\n"
                  "while True: time.sleep(.01)\n")
        process, prefix, started = self.start(self.python_command(source, ready),
                                             command_seconds=10, capture_seconds=21)
        self.assertTrue(wait_for_path(ready, 3), "native child never became active")
        self.assertIsNone(process.poll())
        process.send_signal(signal.SIGTERM)
        status, fields, prefix, elapsed, output, errors = self.finish(process, prefix, started)
        self.assertEqual(status, 143, errors)
        self.assertEqual(fields["command_status"], "143")
        self.assertEqual(fields["deadline_reached"], "0")
        self.assertEqual(fields["cancellation_signal"], "15")
        self.assertEqual(fields["term_group_attempted"], "1")
        self.assert_native(fields, prefix, 143, "signal")
        self.assert_complete_capture(fields, prefix, b"")
        self.assertLess(elapsed, 3)


if __name__ == "__main__":
    unittest.main()
