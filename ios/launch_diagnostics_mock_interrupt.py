#!/usr/bin/env python3
"""Interrupt the mock launcher only after its producer-owner handshake.

The caller asks for interruption through a private FIFO after validating the
producer registration. This helper owns the lifecycle-capture bridge as a
subprocess handle and requests TERM through the bridge's cancellation protocol;
it never signals a saved Bash PID or process group.
"""

import os
from pathlib import Path
import select
import signal
import stat
import subprocess
import sys
import time


INTERRUPT_DELAY_NS = 2_000_000_000
BRIDGE_COMMAND_SECONDS = "20"
BRIDGE_CAPTURE_SECONDS = "20"
BRIDGE_TERM_WAIT_SECONDS = 12
BRIDGE_KILL_WAIT_SECONDS = 3
POLL_SECONDS = 0.05
pending_signal = 0


def note_signal(signum, _frame):
    global pending_signal
    if not pending_signal:
        pending_signal = signum


def signal_bridge(bridge, signum):
    """Signal only the still-owned direct child represented by Popen."""
    if bridge.poll() is not None:
        return False
    try:
        bridge.send_signal(signum)
    except ProcessLookupError:
        return False
    return True


def wait_bridge(bridge):
    try:
        return bridge.wait(timeout=BRIDGE_TERM_WAIT_SECONDS)
    except subprocess.TimeoutExpired:
        signal_bridge(bridge, signal.SIGKILL)
        return bridge.wait(timeout=BRIDGE_KILL_WAIT_SECONDS)


def read_gate(descriptor, bridge, seconds):
    deadline = time.monotonic() + seconds
    frame = bytearray()
    while True:
        if pending_signal:
            return None
        status = bridge.poll()
        if status is not None:
            return ("bridge-exited", status)
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return None
        readable, _, _ = select.select([descriptor], [], [], min(POLL_SECONDS, remaining))
        if not readable:
            continue
        chunk = os.read(descriptor, 256)
        if not chunk:
            continue
        frame.extend(chunk)
        if len(frame) > 64:
            raise ValueError("interruption gate frame is oversized")
        if b"\n" not in frame:
            continue
        line, trailing = bytes(frame).split(b"\n", 1)
        if trailing:
            raise ValueError("interruption gate has trailing bytes")
        if line not in (b"interrupt", b"abort"):
            raise ValueError("interruption gate frame is invalid")
        return line.decode("ascii")


def write_result(path, gate, sent, elapsed_ns, status):
    target = Path(path)
    if target.exists() or target.is_symlink():
        raise OSError("interruption result already exists")
    staging = target.with_name(target.name + ".tmp")
    staging.write_text(
        "gate=%s signal=%s delay_ns=%d bridge_status=%d\n"
        % (gate, "TERM" if sent else "not-sent", elapsed_ns, status),
        encoding="ascii",
    )
    os.replace(staging, target)


def replay_capture(prefix):
    path = Path(prefix + ".log")
    if path.is_file() and not path.is_symlink():
        sys.stdout.buffer.write(path.read_bytes())
        sys.stdout.buffer.flush()


def main(arguments):
    if "--" not in arguments:
        print("mock interrupter requires a command separator", file=sys.stderr)
        return 2
    separator = arguments.index("--")
    options = arguments[:separator]
    command = arguments[separator + 1 :]
    if len(options) != 6 or not command:
        print("mock interrupter received incorrect arguments", file=sys.stderr)
        return 2
    timeout_bin, bridge_script, prefix, gate_path, gate_seconds, result_path = options
    if not gate_seconds.isascii() or not gate_seconds.isdecimal() or int(gate_seconds) < 1:
        print("mock interrupter gate bound must be positive seconds", file=sys.stderr)
        return 2
    gate_seconds = int(gate_seconds)
    if not stat.S_ISFIFO(os.lstat(gate_path).st_mode):
        print("mock interrupter gate is not a FIFO", file=sys.stderr)
        return 2

    global pending_signal
    signal.signal(signal.SIGINT, note_signal)
    signal.signal(signal.SIGTERM, note_signal)

    gate_fd = os.open(gate_path, os.O_RDONLY | os.O_NONBLOCK)
    bridge = None
    outcome = 1
    try:
        bridge_command = [
            "/bin/bash",
            bridge_script,
            timeout_bin,
            prefix,
            BRIDGE_COMMAND_SECONDS,
            BRIDGE_CAPTURE_SECONDS,
            "--",
            *command,
        ]
        bridge = subprocess.Popen(bridge_command)
        gate = read_gate(gate_fd, bridge, gate_seconds)
        if gate is None:
            print("producer registration handshake did not open the bounded interrupt gate", file=sys.stderr)
            sent = signal_bridge(bridge, signal.SIGTERM)
            status = wait_bridge(bridge)
            write_result(result_path, "timeout", sent, 0, status)
            outcome = 1
        elif isinstance(gate, tuple):
            print("lifecycle-capture bridge exited before producer registration: status=%d" % gate[1], file=sys.stderr)
            write_result(result_path, "early-exit", False, 0, gate[1])
            outcome = 1
        elif gate == "abort":
            sent = signal_bridge(bridge, signal.SIGTERM)
            status = wait_bridge(bridge)
            write_result(result_path, "abort", sent, 0, status)
            outcome = 1
        else:
            gate_started = time.monotonic_ns()
            deadline = gate_started + INTERRUPT_DELAY_NS
            result_written = False
            while True:
                if pending_signal:
                    sent = signal_bridge(bridge, pending_signal)
                    status = wait_bridge(bridge)
                    write_result(result_path, "signal", sent, time.monotonic_ns() - gate_started, status)
                    result_written = True
                    outcome = 128 + pending_signal
                    break
                status = bridge.poll()
                if status is not None:
                    elapsed_ns = time.monotonic_ns() - gate_started
                    write_result(result_path, "interrupt", False, elapsed_ns, status)
                    print("lifecycle-capture bridge exited before the post-registration interrupt", file=sys.stderr)
                    result_written = True
                    outcome = 1
                    break
                remaining_ns = deadline - time.monotonic_ns()
                if remaining_ns <= 0:
                    break
                time.sleep(min(POLL_SECONDS, remaining_ns / 1_000_000_000))

            if not result_written:
                elapsed_ns = time.monotonic_ns() - gate_started
                sent = signal_bridge(bridge, signal.SIGTERM)
                status = wait_bridge(bridge)
                write_result(result_path, "interrupt", sent, elapsed_ns, status)
                outcome = status if sent and status == 143 else 1

        replay_capture(prefix)
        return outcome
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        print("mock interrupter failed: %s" % error, file=sys.stderr)
        if bridge is not None and bridge.poll() is None:
            signal_bridge(bridge, signal.SIGTERM)
            try:
                wait_bridge(bridge)
            except subprocess.TimeoutExpired:
                signal_bridge(bridge, signal.SIGKILL)
        return 1
    finally:
        os.close(gate_fd)
        if bridge is not None and bridge.poll() is None:
            signal_bridge(bridge, signal.SIGTERM)
            try:
                wait_bridge(bridge)
            except subprocess.TimeoutExpired:
                signal_bridge(bridge, signal.SIGKILL)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
