#!/usr/bin/env python3
"""Own one lifecycle command and its bounded output capture (#2428).

Supervisor.run owns a same-session private group anchored by an unreaped keeper.
Native completion, real pipe EOF and owned cleanup are separate observations.
keeper_main handles the private ready/control protocol; it never owns payload
output. Only the first OUTPUT_LIMIT bytes are retained, while the pipe is drained.
"""
import argparse
import errno
import os
from pathlib import Path
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
    return 0


class Supervisor:
    def __init__(self, prefix, command_seconds, capture_seconds, command):
        self.prefix = Path(prefix)
        self.command_seconds = command_seconds
        self.capture_seconds = capture_seconds
        self.command = command
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
                [sys.executable, str(Path(__file__).resolve()), "--keeper", str(ready_write), str(control_read)],
                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                close_fds=True, pass_fds=(ready_write, control_read))
        finally:
            os.close(ready_write)
            os.close(control_read)
            signal.pthread_sigmask(signal.SIG_SETMASK, blocked)
        ready = b""
        os.set_blocking(self.ready_fd, False)
        while b"\n" not in ready and time.monotonic_ns() < self.command_deadline and not self.cancellation:
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
        if self.cancellation or time.monotonic_ns() >= self.command_deadline:
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
        self.close_fd("output_write_fd")

    def loop(self):
        while True:
            now = time.monotonic_ns()
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
            if now < self.capture_deadline:
                self.drain(now)
            now = time.monotonic_ns()
            if self.command_terminal and not self.facts["capture_eof"]:
                self.begin_term(now)
            if self.command_terminal and self.facts["capture_eof"]:
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
            if self.command_terminal and self.facts["capture_eof"] and (self.keeper is None or self.facts["keeper_reaped"]):
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
                self.spawn()
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
        parser.add_argument("command", nargs=argparse.REMAINDER)
        options = parser.parse_args(arguments)
        if options.capture_seconds <= options.command_seconds + 10:
            parser.error("capture bound must exceed command plus ten-second grace")
        command = options.command[1:] if options.command[:1] == ["--"] else options.command
        if not command:
            parser.error("command is required")
        result = Supervisor(options.prefix, options.command_seconds, options.capture_seconds, command).run()
    return result


if __name__ == "__main__":
    sys.exit(main())
