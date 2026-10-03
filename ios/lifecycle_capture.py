#!/usr/bin/env python3
"""Own one lifecycle command and its bounded output capture (#2428).

Supervisor.run owns a same-session private group anchored by an unreaped keeper.
Native completion, real pipe EOF and owned cleanup are separate observations.
keeper_main handles the private ready/control protocol; it never owns payload
output. Only the first OUTPUT_LIMIT bytes are retained, while the pipe is drained.
"""
import argparse
import errno
import fcntl
import os
from pathlib import Path
import re
import select
import signal
import subprocess
import sys
import tempfile
import time

OUTPUT_LIMIT = 65536
READ_QUANTUM = 65536
KILL_GRACE_NS = 10_000_000_000
MAX_SECONDS = 9223372036854775807
CANCEL_SIGNALS = {signal.SIGINT, signal.SIGTERM}


class BridgeProtocolError(RuntimeError):
    pass


class CallerBridge:
    """Private caller custody; positive command proof requires the whole frame."""
    def __init__(self, generation, control, ready, completion, lifetime, adjudication):
        self.generation = generation
        self.readers = dict(control=control, lifetime=lifetime, adjudication=adjudication)
        self.ready = ready
        self.completion = completion
        self.buffers = {key: b"" for key in self.readers}
        self.eof = set()
        self.go = False
        self.done_sent = False
        self.command_authenticated = False
        self.command_final = False
        self.caller_lost = False

    def write(self, descriptor, text):
        payload = text.encode("ascii")
        if os.write(descriptor, payload) != len(payload):
            raise BridgeProtocolError("short protocol write")

    def prepare(self, owner):
        for descriptor, mode in ((self.readers["control"], os.O_RDONLY),
                                 (self.ready, os.O_WRONLY), (self.completion, os.O_WRONLY),
                                 (self.readers["lifetime"], os.O_RDONLY),
                                 (self.readers["adjudication"], os.O_RDONLY)):
            if fcntl.fcntl(descriptor, fcntl.F_GETFL) & os.O_ACCMODE != mode:
                raise BridgeProtocolError("incorrect private descriptor access")
        for descriptor in (*self.readers.values(), self.ready, self.completion):
            os.set_inheritable(descriptor, False)
        for descriptor in self.readers.values():
            os.set_blocking(descriptor, False)
        # No child has been created. Both GNU monitored groups are left before
        # READY can authorize GO and keeper/native admission.
        signal.pthread_sigmask(signal.SIG_UNBLOCK, CANCEL_SIGNALS | {signal.SIGUSR1, signal.SIGALRM})
        signal.signal(signal.SIGUSR1, signal.SIG_DFL)
        os.setsid()
        owner.facts["bridge_generation"] = self.generation
        owner.facts["command_monitor_status"] = "pending"
        owner.facts["command_authenticated"] = 0
        owner.facts["caller_lost"] = 0
        owner.facts["bridge_control_eof"] = 0
        self.write(self.ready, "READY %s %d %d %d\n" %
                   (self.generation, os.getpid(), os.getpgrp(), os.getsid(0)))

    def read_channels(self):
        for key, descriptor in tuple(self.readers.items()):
            if descriptor is None:
                continue
            try:
                chunk = os.read(descriptor, 256)
            except BlockingIOError:
                continue
            if chunk:
                self.buffers[key] += chunk
                if len(self.buffers[key]) > 256:
                    raise BridgeProtocolError("oversized protocol frame")
            else:
                self.readers[key] = None
                self.eof.add(key)
                os.close(descriptor)

    def pump(self, owner):
        self.read_channels()
        expected = ("GO %s\n" % self.generation).encode("ascii")
        control = self.buffers["control"]
        if b"\n" in control:
            if control != expected:
                raise BridgeProtocolError("invalid GO frame")
            self.go = True
        elif "control" in self.eof and control:
            raise BridgeProtocolError("partial GO frame")
        if "control" in self.eof:
            owner.facts["bridge_control_eof"] = 1
            if not self.done_sent:
                owner.begin_term(time.monotonic_ns())

        lifetime = self.buffers["lifetime"]
        if b"\n" in lifetime:
            messages = [("CANCEL %s %d\n" % (self.generation, signum)).encode("ascii")
                        for signum in CANCEL_SIGNALS]
            if lifetime not in messages:
                raise BridgeProtocolError("invalid cancellation frame")
            owner.note_signal(int(lifetime.split()[-1]), None)
        elif "lifetime" in self.eof and lifetime:
            raise BridgeProtocolError("partial cancellation frame")
        if "lifetime" in self.eof:
            if not self.caller_lost:
                elapsed = (time.monotonic_ns() - owner.started) // 1_000_000_000
                owner.facts["caller_loss_elapsed"] = elapsed
                if not owner.facts["capture_eof"]:
                    owner.facts["capture_elapsed"] = elapsed
            self.caller_lost = True
            owner.facts["caller_lost"] = 1
            owner.facts["capture_status"] = 124
            owner.begin_term(time.monotonic_ns())
            # Caller authority has ended. Never grant a new post-cap grace.
            owner.final_cleanup(time.monotonic_ns())

        if "adjudication" in self.eof and not self.command_final:
            self.command_final = True
            pieces = self.buffers["adjudication"].split(b" ")
            if (len(pieces) != 3 or pieces[0] != b"COMMAND-MONITOR"
                    or pieces[1] != self.generation.encode("ascii")
                    or not pieces[2].endswith(b"\n") or pieces[2].count(b"\n") != 1):
                raise BridgeProtocolError("invalid command monitor frame")
            raw = pieces[2][:-1]
            if not raw.isdigit() or len(raw) > 3 or (len(raw) > 1 and raw.startswith(b"0")) or int(raw) > 255:
                raise BridgeProtocolError("invalid command monitor status")
            status = int(raw)
            owner.facts["command_monitor_status"] = status
            if status == 0:
                if not self.done_sent:
                    raise BridgeProtocolError("monitor0 without native DONE")
                self.command_authenticated = True
                owner.facts["command_authenticated"] = 1
            elif status in (124, 137):
                owner.facts["deadline_reached"] = 1
                owner.facts["command_status"] = 124
                owner.begin_term(time.monotonic_ns())
            else:
                raise BridgeProtocolError("command monitor failed")
        owner.reconcile_cancellation()

    def done(self):
        if not self.done_sent:
            self.done_sent = True
            try:
                self.write(self.ready, "DONE %s\n" % self.generation)
            except BrokenPipeError:
                # B can be killed at its deadline. Its separate actual monitor
                # result, not a successful write, decides command acceptance.
                pass

    def close(self, owner):
        for key, descriptor in tuple(self.readers.items()):
            self.readers[key] = None
            if descriptor is not None:
                try:
                    os.close(descriptor)
                except OSError as error:
                    owner.helper_failure(error)
        for attribute in ("ready", "completion"):
            descriptor = getattr(self, attribute)
            setattr(self, attribute, None)
            if descriptor is not None:
                try:
                    os.close(descriptor)
                except OSError as error:
                    owner.helper_failure(error)


def seconds_value(value):
    if not value.isascii() or not value.isdecimal() or value.startswith("0"):
        raise argparse.ArgumentTypeError("seconds must be positive decimal integers")
    result = int(value)
    if not 1 <= result <= MAX_SECONDS:
        raise argparse.ArgumentTypeError("seconds exceed the signed64 caller domain")
    return result


def atomic_text(path, text):
    descriptor, staging = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="ascii", newline="\n") as output:
            output.write(text)
        os.replace(staging, path)
    finally:
        if os.path.exists(staging):
            os.unlink(staging)


def keeper_main(ready_fd, control_fd):
    # A caught handler resets at argv exec; SIG_IGN would survive exec.
    signal.signal(signal.SIGTERM, lambda signum, frame: None)
    signal.signal(signal.SIGINT, lambda signum, frame: None)
    signal.signal(signal.SIGCHLD, signal.SIG_DFL)
    os.setpgid(0, 0)
    signal.pthread_sigmask(signal.SIG_UNBLOCK, CANCEL_SIGNALS)
    os.write(ready_fd, ("%d %d\n" % (os.getpid(), os.getpgrp())).encode("ascii"))
    while os.read(control_fd, 1):
        pass
    os.close(control_fd)
    os.write(ready_fd, b"released\n")
    os.close(ready_fd)
    # This dedicated keeper owns no payload or buffered output. Its complete
    # protocol and descriptor cleanup must lead directly to the real zero exit,
    # without an interpreter-finalization tail inside the bounded release join.
    os._exit(0)


class Supervisor:
    def __init__(self, prefix, command_seconds, capture_seconds, command, bridge=None):
        self.prefix = Path(prefix)
        self.command_seconds = command_seconds
        self.capture_seconds = capture_seconds
        self.command = command
        self.bridge = bridge
        self.keeper = None
        self.native = None
        self.group_authority = False
        self.control_fd = None
        self.output_fd = None
        self.output_write_fd = None
        self.ready_fd = None
        self.output = None
        self.command_terminal = False
        self.release_started = False
        self.release_deadline = None
        self.term_started = None
        self.cancellation = 0
        self.total_bytes = 0
        self.retained_bytes = 0
        self.keeper_receipts = b""
        self.facts_dirty = False
        self.facts = dict(version=1, command_status=125, native_status="unavailable",
                          capture_status=124, command_elapsed=0, capture_elapsed=0,
                          deadline_reached=0, capture_eof=0, capture_eof_late=0, cleanup_status=0,
                          native_kind="unavailable", launch_error="none", native_launch=0,
                          cancellation_signal=0, native_observed_after_deadline=0,
                          keeper_pid="unavailable", keeper_pgid="unavailable",
                          keeper_status="unavailable", keeper_kind="unavailable", keeper_control_eof_ack=0,
                          keeper_reaped=0, native_reaped=0, group_authority_released=0,
                          cleanup_scope="keeper-only", keeper_release="none",
                          term_group_attempted=0, term_native_attempted=0,
                          kill_group_attempted=0, kill_native_attempted=0,
                          keeper_kill_attempted=0, group_term_result="not-attempted",
                          group_kill_result="not-attempted", native_term_result="not-attempted",
                          native_kill_result="not-attempted", helper_error="none")

    def path(self, suffix):
        return Path(str(self.prefix) + suffix)

    def note_signal(self, signum, frame):
        if not self.cancellation:
            self.cancellation = signum

    def reconcile_cancellation(self):
        changed = False
        if self.cancellation:
            changed = (self.facts["cancellation_signal"] != self.cancellation
                       or self.facts["command_status"] != 128 + self.cancellation)
            self.facts["cancellation_signal"] = self.cancellation
            self.facts["command_status"] = 128 + self.cancellation
        return changed

    def pump_bridge(self):
        if self.bridge is not None:
            self.bridge.pump(self)

    def native_done(self):
        if self.bridge is not None and self.command_terminal:
            self.bridge.done()

    def command_proof_final(self):
        return self.bridge is None or self.bridge.command_final

    def command_proof_positive(self):
        return self.bridge is None or self.bridge.command_authenticated

    def helper_failure(self, error):
        self.facts["helper_error"] = type(error).__name__
        self.facts["cleanup_status"] = 1
        if not self.facts["deadline_reached"] and not self.cancellation:
            self.facts["command_status"] = 125
        self.facts_dirty = True

    def close_fd(self, attribute):
        descriptor = getattr(self, attribute)
        if descriptor is not None:
            # End descriptor ownership before close; an error must neither
            # retry a potentially reused FD nor bypass owned child cleanup.
            setattr(self, attribute, None)
            try:
                os.close(descriptor)
            except OSError as error:
                self.helper_failure(error)

    def close_output(self):
        if self.output is not None:
            output = self.output
            self.output = None
            try:
                output.close()
            except OSError as error:
                self.helper_failure(error)

    def observe_native(self, now):
        if self.native is not None and not self.command_terminal:
            status = self.native.poll()
            if status is not None:
                now = time.monotonic_ns()
                if now >= self.command_deadline and not self.facts["deadline_reached"]:
                    self.facts["deadline_reached"] = 1
                    if not self.cancellation:
                        self.facts["command_status"] = 124
                self.command_terminal = True
                self.facts["native_reaped"] = 1
                self.facts["native_kind"] = "signal" if status < 0 else "exit"
                self.facts["native_status"] = 128 - status if status < 0 else status
                self.facts["native_observed_after_deadline"] = self.facts["deadline_reached"]
                self.facts["command_elapsed"] = (now - self.started) // 1_000_000_000
                if not self.facts["deadline_reached"] and not self.cancellation and self.facts["helper_error"] == "none":
                    self.facts["command_status"] = self.facts["native_status"]
                self.native_done()
        self.reconcile_cancellation()

    def signal_native(self, signum, fact):
        if self.native is not None and not self.command_terminal:
            self.facts[fact + "_native_attempted"] = 1
            result = "sent"
            try:
                # send_signal's internal poll may reap; observe that transition.
                self.native.send_signal(signum)
                if self.native.returncode is not None:
                    result = "gone"
            except ProcessLookupError:
                result = "gone"
            except OSError as error:
                result = "error-%d" % error.errno
                self.facts["cleanup_status"] = 1
            self.facts["native_" + fact + "_result"] = result
            self.observe_native(time.monotonic_ns())

    def signal_group(self, signum, fact):
        if self.group_authority:
            self.facts[fact + "_group_attempted"] = 1
            result = "sent"
            try:
                # The keeper remains deliberately unreaped here. Never use
                # keeper.send_signal/poll/wait before this authority ends.
                os.killpg(self.keeper.pid, signum)
            except ProcessLookupError:
                result = "gone"
            except OSError as error:
                result = "error-%d" % error.errno
                self.facts["cleanup_status"] = 1
            self.facts["group_" + fact + "_result"] = result

    def close_control(self):
        self.close_fd("control_fd")

    def begin_term(self, now):
        if self.term_started is None:
            self.term_started = now
            self.facts["cleanup_scope"] = "private-group-and-direct-native"
            self.signal_group(signal.SIGTERM, "term")
            self.signal_native(signal.SIGTERM, "term")

    def final_cleanup(self, now, ordinary=False):
        if not self.release_started:
            if ordinary:
                self.facts["keeper_release"] = "control-eof"
                self.close_control()
            elif self.group_authority:
                self.signal_group(signal.SIGKILL, "kill")
                self.signal_native(signal.SIGKILL, "kill")
                self.facts["keeper_release"] = "final-group-signal"
                self.close_control()
            else:
                self.facts["keeper_release"] = "startup-owned-only"
                if self.keeper is not None:
                    self.facts["keeper_kill_attempted"] = 1
                    # No group authority was acquired; this unreaped direct
                    # child identity is still pinned and is the only target.
                    try:
                        os.kill(self.keeper.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    except OSError:
                        self.facts["cleanup_status"] = 1
                self.close_control()
            self.group_authority = False
            self.facts["group_authority_released"] = 1
            self.release_started = True
            self.release_deadline = min(now + KILL_GRACE_NS, self.capture_deadline)

    def reap_keeper(self, now):
        if self.release_started and self.keeper is not None and not self.facts["keeper_reaped"]:
            # Group authority is already released. A returncode ends direct
            # authority too; no later signal may use this child's numeric PID.
            status = self.keeper.poll()
            if status is None and now >= self.release_deadline and not self.facts["keeper_kill_attempted"]:
                self.facts["keeper_kill_attempted"] = 1
                try:
                    self.keeper.kill()
                    status = self.keeper.returncode
                except OSError:
                    self.facts["cleanup_status"] = 1
            if status is not None:
                self.facts["keeper_reaped"] = 1
                self.facts["keeper_status"] = 128 - status if status < 0 else status
                self.facts["keeper_kind"] = "signal" if status < 0 else "exit"
                self.read_keeper_receipts()
                if self.facts["keeper_release"] == "control-eof" and (status != 0 or not self.facts["keeper_control_eof_ack"]):
                    self.facts["cleanup_status"] = 1
                if self.facts["keeper_release"] == "final-group-signal" and status != -signal.SIGKILL:
                    # Genuine EOF after cleanup does not excuse a keeper that
                    # failed before the final dispatch. Preserve its actual
                    # status instead of treating every reap as protocol success.
                    self.facts["cleanup_status"] = 1

    def read_keeper_receipts(self):
        if self.ready_fd is not None:
            try:
                chunk = os.read(self.ready_fd, 128)
            except BlockingIOError:
                chunk = b""
            self.keeper_receipts += chunk
            if len(self.keeper_receipts) > 128:
                self.facts["cleanup_status"] = 1
            if self.keeper_receipts == b"released\n":
                self.facts["keeper_control_eof_ack"] = 1

    def drain(self, now):
        consumed = 0
        while self.output_fd is not None and consumed < READ_QUANTUM:
            if time.monotonic_ns() >= self.capture_deadline:
                break
            try:
                chunk = os.read(self.output_fd, min(4096, READ_QUANTUM - consumed))
            except BlockingIOError:
                break
            if not chunk:
                observed = time.monotonic_ns()
                if observed < self.capture_deadline:
                    self.facts["capture_eof"] = 1
                    self.facts["capture_status"] = 0
                else:
                    self.facts["capture_eof_late"] = 1
                self.facts["capture_elapsed"] = (observed - self.started) // 1_000_000_000
                self.close_fd("output_fd")
                break
            kept = chunk[:OUTPUT_LIMIT - self.retained_bytes]
            self.total_bytes += len(chunk)
            if kept:
                written = self.output.write(kept)
                self.retained_bytes += written or 0
                if written != len(kept):
                    raise OSError("short capture write")
            consumed += len(chunk)

    def spawn(self):
        self.output_fd, self.output_write_fd = os.pipe()
        os.set_blocking(self.output_fd, False)
        self.ready_fd, ready_write = os.pipe()
        control_read, self.control_fd = os.pipe()
        blocked = signal.pthread_sigmask(signal.SIG_BLOCK, CANCEL_SIGNALS)
        try:
            self.keeper = subprocess.Popen(
                [sys.executable, "-S", str(Path(__file__).resolve()), "--keeper", str(ready_write), str(control_read)],
                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                close_fds=True, pass_fds=(ready_write, control_read))
        finally:
            os.close(ready_write)
            os.close(control_read)
            signal.pthread_sigmask(signal.SIG_SETMASK, blocked)
        ready = b""
        os.set_blocking(self.ready_fd, False)
        while b"\n" not in ready and time.monotonic_ns() < self.command_deadline and not self.cancellation:
            self.pump_bridge()
            if self.bridge is not None and self.bridge.caller_lost:
                break
            if select.select([self.ready_fd], [], [], 0.02)[0]:
                chunk = os.read(self.ready_fd, 128)
                if not chunk:
                    break
                ready += chunk
                if len(ready) > 128:
                    break
        expected = ("%d %d\n" % (self.keeper.pid, self.keeper.pid)).encode("ascii")
        if ready != expected or os.getpgid(self.keeper.pid) != self.keeper.pid:
            raise RuntimeError("keeper-ready-failure")
        self.group_authority = True
        self.facts["keeper_pid"] = self.keeper.pid
        self.facts["keeper_pgid"] = self.keeper.pid
        self.pump_bridge()
        if (self.cancellation or self.facts["deadline_reached"] or time.monotonic_ns() >= self.command_deadline
                or self.bridge is not None and (self.bridge.caller_lost or "control" in self.bridge.eof)):
            self.command_terminal = True
        else:
            blocked = signal.pthread_sigmask(signal.SIG_BLOCK, CANCEL_SIGNALS)
            def native_setup():
                signal.signal(signal.SIGINT, signal.SIG_DFL)
                signal.signal(signal.SIGTERM, signal.SIG_DFL)
                os.setpgid(0, self.keeper.pid)
                signal.pthread_sigmask(signal.SIG_SETMASK, blocked - CANCEL_SIGNALS)
            try:
                self.native = subprocess.Popen(self.command, stdin=subprocess.DEVNULL,
                                               stdout=self.output_write_fd, stderr=self.output_write_fd,
                                               close_fds=True, preexec_fn=native_setup)
                self.facts["native_launch"] = 1
            except OSError as error:
                self.command_terminal = True
                self.facts["launch_error"] = errno.errorcode.get(error.errno, "other")
                if self.facts["launch_error"] not in {"ENOENT", "EACCES", "ENOEXEC"}:
                    self.facts["launch_error"] = "other"
                self.facts["command_status"] = 127 if error.errno == errno.ENOENT else 126 if error.errno in {errno.EACCES, errno.ENOEXEC} else 125
                self.facts["command_elapsed"] = (time.monotonic_ns() - self.started) // 1_000_000_000
            finally:
                signal.pthread_sigmask(signal.SIG_SETMASK, blocked)
                self.pump_bridge()
        self.close_fd("output_write_fd")
        self.native_done()

    def loop(self):
        while True:
            now = time.monotonic_ns()
            self.pump_bridge()
            # Completion first observed after the deadline is not evidence of
            # an earlier kernel exit. Latch the deadline before a fresh poll.
            if not self.command_terminal and now >= self.command_deadline:
                self.facts["deadline_reached"] = 1
                self.facts["command_status"] = 124
                self.begin_term(now)
            if self.cancellation:
                self.facts["cancellation_signal"] = self.cancellation
                self.facts["command_status"] = 128 + self.cancellation
                self.begin_term(now)
            if self.facts["helper_error"] != "none":
                self.begin_term(now)
            self.observe_native(now)
            if self.facts["deadline_reached"]:
                self.begin_term(time.monotonic_ns())
            if now < self.capture_deadline and not (self.bridge is not None and self.bridge.caller_lost):
                self.drain(now)
            now = time.monotonic_ns()
            if self.command_terminal and not self.facts["capture_eof"]:
                self.begin_term(now)
            if (self.command_terminal and self.facts["capture_eof"]
                    and (self.command_proof_positive() or self.term_started is not None)):
                self.final_cleanup(now, ordinary=self.term_started is None)
            elif self.term_started is not None and now >= self.term_started + KILL_GRACE_NS:
                self.final_cleanup(now)
            if now >= self.capture_deadline:
                if not self.facts["capture_eof"]:
                    self.facts["capture_status"] = 124
                    self.facts["capture_elapsed"] = (now - self.started) // 1_000_000_000
                self.final_cleanup(now)
                self.observe_native(now)
                self.reap_keeper(now)
                break
            self.reap_keeper(now)
            if self.bridge is not None and self.bridge.caller_lost:
                self.observe_native(now)
                self.reap_keeper(now)
                break
            if (self.command_terminal and self.facts["capture_eof"] and self.command_proof_final()
                    and (self.keeper is None or self.facts["keeper_reaped"])):
                break
            descriptors = [] if self.output_fd is None else [self.output_fd]
            select.select(descriptors, [], [], min(0.02, (self.capture_deadline - now) / 1_000_000_000))
        if self.native is not None and not self.command_terminal:
            self.facts["cleanup_status"] = 1
        if self.keeper is not None and not self.facts["keeper_reaped"]:
            self.facts["cleanup_status"] = 1

    def publish(self):
        if self.facts["capture_eof"]:
            atomic_text(self.path(".log.capture-status.log"),
                        "BUSTER_IOS_CAPTURE total_bytes=%d retained_bytes=%d truncated=%d\n" %
                        (self.total_bytes, self.retained_bytes, self.total_bytes > OUTPUT_LIMIT))
        if self.facts["native_status"] != "unavailable":
            atomic_text(self.path(".native-status.log"), "%s\n" % self.facts["native_status"])
        for fact, suffix in (("command_elapsed", ".command-elapsed.log"), ("capture_elapsed", ".capture-elapsed.log")):
            atomic_text(self.path(suffix), "%s\n" % self.facts[fact])
        keys = ("command_status", "native_status", "capture_status", "command_elapsed", "capture_elapsed",
                "deadline_reached", "cleanup_status", "native_kind", "launch_error")
        atomic_text(self.path(".supervisor-status.log"), "BUSTER_IOS_SUPERVISOR " +
                    " ".join("%s=%s" % item for item in self.facts.items()) + "\n")
        atomic_text(self.path(".supervisor-fields.log"), " ".join(str(self.facts[key]) for key in keys) + "\n")

    def run(self):
        self.started = time.monotonic_ns()
        self.command_deadline = self.started + self.command_seconds * 1_000_000_000
        self.capture_deadline = self.started + self.capture_seconds * 1_000_000_000
        old_handlers = {signum: signal.getsignal(signum) for signum in CANCEL_SIGNALS | {signal.SIGCHLD}}
        old_mask = signal.pthread_sigmask(signal.SIG_UNBLOCK, CANCEL_SIGNALS)
        for signum in CANCEL_SIGNALS:
            signal.signal(signum, self.note_signal)
        signal.signal(signal.SIGCHLD, signal.SIG_DFL)
        try:
            self.prefix.parent.mkdir(parents=True, exist_ok=True)
            for suffix in (".native-status.log", ".log.capture-status.log", ".command-elapsed.log", ".capture-elapsed.log", ".supervisor-fields.log", ".supervisor-status.log"):
                self.path(suffix).unlink(missing_ok=True)
            self.output = self.path(".log").open("wb", buffering=0)
            try:
                if self.bridge is not None:
                    self.bridge.prepare(self)
                    while (not self.bridge.go and not self.cancellation and not self.facts["deadline_reached"]
                           and not self.bridge.caller_lost and time.monotonic_ns() < self.command_deadline):
                        self.pump_bridge()
                        if not self.bridge.go:
                            select.select([fd for fd in self.bridge.readers.values() if fd is not None], [], [], 0.02)
                    self.pump_bridge()
                if (self.bridge is None or self.bridge.go) and not self.cancellation and not self.facts["deadline_reached"] \
                        and not (self.bridge is not None and (self.bridge.caller_lost or "control" in self.bridge.eof)):
                    self.spawn()
                else:
                    self.command_terminal = True
                    if not self.cancellation and not (self.bridge is not None and self.bridge.caller_lost):
                        self.facts["deadline_reached"] = 1
                    self.facts["command_status"] = 124 if self.facts["deadline_reached"] else 125
                    self.native_done()
                    self.final_cleanup(time.monotonic_ns())
            except Exception as error:
                self.facts["helper_error"] = type(error).__name__
                if self.native is None:
                    self.command_terminal = True
                else:
                    # Registration is ownership, not completion. A later spawn
                    # bookkeeping failure must retain direct authority even if
                    # argv has already left its private process group.
                    self.facts["cleanup_status"] = 1
                    self.facts["command_status"] = 125
                    self.begin_term(time.monotonic_ns())
                if self.cancellation:
                    self.facts["command_status"] = 128 + self.cancellation
                elif time.monotonic_ns() >= self.command_deadline:
                    self.facts["deadline_reached"] = 1
                    self.facts["command_status"] = 124
                self.close_fd("output_write_fd")
                self.final_cleanup(time.monotonic_ns())
                self.native_done()
            try:
                self.loop()
            except Exception as error:
                self.facts["helper_error"] = type(error).__name__
                self.facts["cleanup_status"] = 1
                if not self.facts["deadline_reached"] and not self.cancellation:
                    self.facts["command_status"] = 125
                if not self.facts["capture_eof"]:
                    self.facts["capture_status"] = 1
                    self.facts["capture_elapsed"] = (time.monotonic_ns() - self.started) // 1_000_000_000
                self.begin_term(time.monotonic_ns())
                self.final_cleanup(time.monotonic_ns())
                # Only bounded nonblocking reap remains after failed capture.
                while time.monotonic_ns() < self.capture_deadline and ((self.keeper is not None and not self.facts["keeper_reaped"]) or (self.native is not None and not self.command_terminal)):
                    self.observe_native(time.monotonic_ns())
                    self.reap_keeper(time.monotonic_ns())
                    time.sleep(0.02)
            self.close_output()
            self.reconcile_cancellation()
            self.facts_dirty = False
            self.publish()
        finally:
            for attribute in ("output_fd", "output_write_fd", "ready_fd", "control_fd"):
                self.close_fd(attribute)
            self.close_output()
            for signum, handler in old_handlers.items():
                signal.signal(signum, handler)
            signal.pthread_sigmask(signal.SIG_SETMASK, old_mask)
        # An intent can arrive during poll, cleanup or terminal publication,
        # after the loop's initial check. Reconcile after handlers are restored
        # too; updating receipts never revives released signal authority.
        if self.reconcile_cancellation() or self.facts_dirty:
            self.publish()
        return self.result()

    def result(self):
        result = self.facts["command_status"]
        if result == 0 and (self.facts["capture_status"] != 0 or self.facts["cleanup_status"] != 0):
            result = 1
        return result


def main(argv=None):
    arguments = sys.argv[1:] if argv is None else argv
    if arguments and arguments[0] == "--keeper":
        result = keeper_main(int(arguments[1]), int(arguments[2]))
    else:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument("--prefix", required=True)
        parser.add_argument("--command-seconds", required=True, type=seconds_value)
        parser.add_argument("--capture-seconds", required=True, type=seconds_value)
        parser.add_argument("--bridge-generation", help=argparse.SUPPRESS)
        for channel in ("control", "ready", "completion", "lifetime", "adjudication"):
            parser.add_argument("--bridge-%s-fd" % channel, type=int, help=argparse.SUPPRESS)
        parser.add_argument("command", nargs=argparse.REMAINDER)
        options = parser.parse_args(arguments)
        if options.capture_seconds <= options.command_seconds + 10:
            parser.error("capture bound must exceed command plus ten-second grace")
        command = options.command[1:] if options.command[:1] == ["--"] else options.command
        if not command:
            parser.error("command is required")
        descriptors = [getattr(options, "bridge_%s_fd" % channel) for channel in
                       ("control", "ready", "completion", "lifetime", "adjudication")]
        bridge = None
        if options.bridge_generation is not None or any(fd is not None for fd in descriptors):
            if (options.bridge_generation is None or re.fullmatch(r"[A-Za-z0-9]{8,64}", options.bridge_generation) is None
                    or any(fd is None or fd < 3 for fd in descriptors) or len(set(descriptors)) != 5):
                parser.error("complete private bridge descriptors and generation are required")
            bridge = CallerBridge(options.bridge_generation, *descriptors)
        owner = Supervisor(options.prefix, options.command_seconds, options.capture_seconds, command, bridge)
        try:
            result = owner.run()
            if bridge is not None:
                try:
                    bridge.pump(owner)
                except Exception as error:
                    owner.helper_failure(error)
                if owner.reconcile_cancellation() or owner.facts_dirty or bridge.caller_lost:
                    owner.publish()
                result = owner.result()
                bridge.write(bridge.completion, "COMPLETE %s %d\n" % (bridge.generation, result))
        finally:
            if bridge is not None:
                bridge.close(owner)
                if owner.reconcile_cancellation() or owner.facts_dirty:
                    owner.publish()
                    result = owner.result()
    return result


if __name__ == "__main__":
    sys.exit(main())
