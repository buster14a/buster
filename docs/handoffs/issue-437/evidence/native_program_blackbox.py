#!/usr/bin/env python3
"""Local native-program integration checks; held host lease forbids execution."""
import argparse
import fcntl
import hashlib
import json
import os
import re
import shutil
import socket
import stat
import struct
import subprocess
import tempfile
import time
from pathlib import Path

CHECKS = []
RUNS = []
MAX_PROGRAM = 4 * 1024 * 1024
CAP = 128
NATIVE_BEGIN, NATIVE_WRITE, NATIVE_FINISH = 14, 15, 16
BQ_OK, BQ_BAD_REQUEST, BQ_CONFLICT, BQ_FULL, BQ_IO, BQ_CORRUPT, BQ_SOURCE_MISMATCH = 0, 1, 2, 3, 5, 6, 12
CORRELATION = 0


def record(name, passed, detail=None):
    CHECKS.append({'name': name, 'passed': bool(passed), 'detail': detail})


def manifest(data):
    digest = hashlib.sha256(data).hexdigest()
    body = ('BQ-NATIVE-V1\noperation=execute-once\nplatform=linux-x86-64-static-elf\narguments=none\nprogram-sha256=' + digest + '\nprogram-size=' + str(len(data)) + '\n').encode()
    return body, hashlib.sha256(body).hexdigest(), digest


def upload_body(data):
    return hashlib.sha256(data).hexdigest().encode() + struct.pack('<Q', len(data))


def cli(name, *args, expect=0):
    proc = subprocess.run([BINARY, *map(str, args)], capture_output=True, timeout=5)
    record(name + '/exit', proc.returncode == expect, {'exit': proc.returncode, 'stderr': proc.stderr.decode('utf-8', 'replace')})
    RUNS.append({'name': name, 'command': [BINARY, *map(str, args)], 'exit': proc.returncode, 'stdout': proc.stdout.decode('utf-8', 'replace'), 'stderr': proc.stderr.decode('utf-8', 'replace')})
    return proc


def raw(name, socket_path, operation, body=b'', *, expected=BQ_OK, schema=2, advertised_length=None, magic=b'BQP1', wire=None):
    global CORRELATION
    CORRELATION += 1
    correlation = CORRELATION
    payload = struct.pack('<4sIIIQ', magic, schema, operation, len(body) if advertised_length is None else advertised_length, correlation) + body if wire is None else wire
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as sock:
        sock.settimeout(5)
        sock.connect(str(socket_path))
        sock.sendall(payload)
        reply = sock.recv(65536)
    valid = len(reply) >= 28
    header = struct.unpack_from('<4sIIIQ', reply) if valid else None
    code = struct.unpack_from('<I', reply, 24)[0] if valid else None
    record(name + '/framing', valid and header[0] == b'BQP1' and header[3] == len(reply) - 24 and (wire is not None or magic != b'BQP1' or len(payload) > 536 or (header[2] == operation | 0x80000000 and header[4] == correlation)), {'reply_bytes': len(reply), 'header': repr(header)})
    record(name + '/error', code == expected, {'expected': expected, 'actual': code})
    parsed = {'error': code, 'wire_bytes': len(reply)}
    if code == BQ_OK and operation in (14, 15, 16):
        record(name + '/upload_reply_shape', len(reply) == 100, len(reply))
        if len(reply) >= 100:
            parsed.update(cursor=struct.unpack_from('<Q', reply, 28)[0], identity=reply[36:100].decode('ascii'))
    RUNS.append({'name': name, 'input_sha256': hashlib.sha256(payload).hexdigest(), 'input_bytes': len(payload), 'reply': parsed})
    return parsed


class Service:
    def __init__(self, root):
        self.root = Path(root)
        self.queue = self.root / 'queue'
        self.socket = self.root / 'control.sock'
        self.installed = self.root / 'installed'
        self.workspaces = self.root / 'workspaces'
        self.lease = self.root / 'host.lock'
        for directory in (self.queue, self.installed, self.workspaces):
            directory.mkdir(mode=0o700)
        self.lock = os.open(self.lease, os.O_CREAT | os.O_RDWR | os.O_CLOEXEC, 0o600)
        fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        self.child = None
        self.start()

    def start(self):
        assert len(str(self.socket).encode()) < 108
        cpu = min(os.sched_getaffinity(0))
        self.child = subprocess.Popen([BINARY, 'serve', str(self.queue), str(self.socket), str(self.installed), str(self.workspaces), str(self.lease), str(cpu)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline and self.child.poll() is None and not self.socket.exists():
            time.sleep(0.01)
        if not self.socket.exists():
            out, err = self.child.communicate(timeout=1)
            raise RuntimeError('Disposable service did not start: ' + err.decode('utf-8', 'replace'))
        record('service/short_private_socket', len(str(self.socket).encode()) < 108 and stat.S_ISSOCK(self.socket.stat().st_mode))
        record('service/socket_private', stat.S_IMODE(self.socket.stat().st_mode) & 0o077 == 0)

    def stop(self):
        if self.child is not None:
            self.child.terminate()
            out, err = self.child.communicate(timeout=4)
            record('service/clean_stop', self.child.returncode == 0 and not out, {'exit': self.child.returncode, 'stderr': err.decode('utf-8', 'replace')})
            self.child = None

    def restart(self):
        self.stop()
        self.start()

    def close(self):
        try:
            self.stop()
        finally:
            os.close(self.lock)


def identity_from_cli(name, proc, data):
    match = re.search(r'program-manifest-sha256=([0-9a-f]{64}) program-bytes=(\d+)', proc.stdout.decode())
    canonical, expected, _ = manifest(data)
    record(name + '/manifest_receipt', bool(match) and match.group(1) == expected and int(match.group(2)) == len(data), proc.stdout.decode())
    return expected


def raw_full_upload(service, name, data, *, expect_finish=BQ_OK):
    body = upload_body(data)
    canonical, identity, _ = manifest(data)
    first = raw(name + '/begin', service.socket, NATIVE_BEGIN, body)
    record(name + '/begin_identity', first.get('identity') == identity and first.get('cursor') == 0, first)
    offset = 0
    for offset in range(0, len(data), 432):
        chunk = data[offset:offset + 432]
        response = raw(name + '/chunk_' + str(offset), service.socket, NATIVE_WRITE, body + struct.pack('<Q', offset) + chunk)
        record(name + '/cursor_' + str(offset), response.get('cursor') == offset + len(chunk), response)
    end = raw(name + '/finish', service.socket, NATIVE_FINISH, body, expected=expect_finish)
    if expect_finish == BQ_OK:
        record(name + '/final_identity', end.get('identity') == identity and end.get('cursor') == len(data), end)
    return identity


def test_native(service, fixtures):
    good_path = fixtures / 'transcript.elf'
    data = good_path.read_bytes()
    proc = cli('cli/upload_valid', 'client', service.socket, 'upload-program', good_path)
    identity = identity_from_cli('cli/upload_valid', proc, data)
    canonical, _, digest = manifest(data)
    committed = service.queue / 'native-blobs' / identity
    record('store/manifest_exact', (committed / 'manifest').read_bytes() == canonical)
    record('store/program_exact', (committed / 'program').read_bytes() == data)
    record('store/publication_modes', stat.S_IMODE(committed.stat().st_mode) == 0o500 and stat.S_IMODE((committed / 'manifest').stat().st_mode) == 0o400 and stat.S_IMODE((committed / 'program').stat().st_mode) == 0o400)
    again = cli('cli/upload_idempotent', 'client', service.socket, 'upload-program', good_path)
    identity_from_cli('cli/upload_idempotent', again, data)
    raw('raw/already_committed_begin', service.socket, NATIVE_BEGIN, upload_body(data))
    raw('raw/already_committed_finish', service.socket, NATIVE_FINISH, upload_body(data))
    # Appending inert bytes preserves this benign ELF's executable segments while
    # producing another immutable identity for independent resumable-upload tests.
    resumable = data + b'\0blackbox-resume'
    body = upload_body(resumable)
    canonical2, identity2, digest2 = manifest(resumable)
    first = raw('resume/begin', service.socket, NATIVE_BEGIN, body)
    record('resume/cursor_zero', first.get('cursor') == 0)
    first_write = raw('resume/write_first', service.socket, NATIVE_WRITE, body + struct.pack('<Q', 0) + resumable[:432])
    record('resume/cursor_first', first_write.get('cursor') == 432)
    duplicate = raw('resume/identical_retry', service.socket, NATIVE_WRITE, body + struct.pack('<Q', 0) + resumable[:432])
    record('resume/identical_retry_cursor', duplicate.get('cursor') == 432)
    changed = bytes([resumable[0] ^ 1]) + resumable[1:432]
    raw('resume/conflicting_prefix', service.socket, NATIVE_WRITE, body + struct.pack('<Q', 0) + changed, expected=BQ_CONFLICT)
    raw('resume/gap', service.socket, NATIVE_WRITE, body + struct.pack('<Q', 433) + b'x', expected=BQ_CONFLICT)
    raw('resume/partial_overlap', service.socket, NATIVE_WRITE, body + struct.pack('<Q', 425) + b'x' * 14, expected=BQ_CONFLICT)
    raw('resume/size_conflict', service.socket, NATIVE_BEGIN, body[:64] + struct.pack('<Q', len(resumable) + 1), expected=BQ_CONFLICT)
    raw('resume/incomplete_finish', service.socket, NATIVE_FINISH, body, expected=BQ_SOURCE_MISMATCH)
    service.restart()
    reopened = raw('resume/reopened_begin', service.socket, NATIVE_BEGIN, body)
    record('resume/durable_prefix', reopened.get('cursor') == 432 and reopened.get('identity') == identity2, reopened)
    for offset in range(432, len(resumable), 432):
        chunk = resumable[offset:offset + 432]
        reply = raw('resume/remainder_' + str(offset), service.socket, NATIVE_WRITE, body + struct.pack('<Q', offset) + chunk)
        record('resume/durable_cursor_' + str(offset), reply.get('cursor') == offset + len(chunk), reply)
    raw('resume/finish', service.socket, NATIVE_FINISH, body)
    record('resume/exact_complete_program', (service.queue / 'native-blobs' / identity2 / 'program').read_bytes() == resumable)
    raw('resume/finish_retry', service.socket, NATIVE_FINISH, body)
    # Real CLI rejection is checked before any local filename can cross RPC.
    for path in sorted(fixtures.glob('*.elf')):
        if path.name == 'transcript.elf':
            continue
        cli('cli/reject_' + path.name, 'client', service.socket, 'upload-program', path, expect=1)
    # Direct upload admission still rejects malformed static/dynamic ELF bytes.
    for label in ('bad_magic.elf', 'unsupported_et_dyn.elf', 'unsupported_rwx.elf', 'dynamic-linked.elf'):
        raw_full_upload(service, 'raw/reject_' + label, (fixtures / label).read_bytes(), expect_finish=BQ_SOURCE_MISMATCH)
    for label, bad_body in [('sha_nonhex', b'Z' * 64 + struct.pack('<Q', 64)), ('size_small', b'a' * 64 + struct.pack('<Q', 63)), ('size_large', b'a' * 64 + struct.pack('<Q', MAX_PROGRAM + 1)), ('short_body', b'a' * 71), ('long_body', b'a' * 64 + struct.pack('<Q', 64) + b'x')]:
        raw('raw/begin_reject_' + label, service.socket, NATIVE_BEGIN, bad_body, expected=BQ_BAD_REQUEST)
    raw('raw/write_empty', service.socket, NATIVE_WRITE, body + struct.pack('<Q', 0), expected=BQ_BAD_REQUEST)
    raw('raw/write_over_cap', service.socket, NATIVE_WRITE, body + struct.pack('<Q', 0) + b'x' * 433, expected=BQ_BAD_REQUEST)
    raw('raw/write_offset_overflow', service.socket, NATIVE_WRITE, body + struct.pack('<Q', 2**64 - 1) + b'x', expected=BQ_BAD_REQUEST)
    raw('raw/unknown_operation', service.socket, 999, expected=BQ_BAD_REQUEST)
    raw('raw/malformed_magic', service.socket, NATIVE_BEGIN, body, magic=b'BAD!', expected=BQ_BAD_REQUEST)
    raw('raw/wrong_schema', service.socket, NATIVE_BEGIN, body, schema=1, expected=BQ_BAD_REQUEST)
    raw('raw/mismatched_length', service.socket, NATIVE_BEGIN, body, advertised_length=71, expected=BQ_BAD_REQUEST)
    # Fixed principal/key idempotency and replay remain queue behavior, not process
    # execution. The external host lease stays held until after serve stops.
    submit = cli('cli/submit', 'client', service.socket, 'submit-program', 'native-blackbox-idempotent', identity)
    match = re.search(r'job=(\d+) token=(\d+).*phase=queued', submit.stdout.decode())
    record('queue/submission_queued_unreserved', bool(match) and match.group(2) == '0', submit.stdout.decode())
    job_id = int(match.group(1)) if match else 1
    repeated = cli('cli/submit_identical', 'client', service.socket, 'submit-program', 'native-blackbox-idempotent', identity)
    record('queue/same_key_same_job', re.search(r'job=' + str(job_id) + r'\b', repeated.stdout.decode()) is not None)
    cli('cli/submit_conflicting_key', 'client', service.socket, 'submit-program', 'native-blackbox-idempotent', identity2, expect=1)
    service.restart()
    status = cli('cli/status_after_restart', 'client', service.socket, 'status', job_id)
    record('queue/replay_queued_unreserved', 'phase=queued' in status.stdout.decode() and 'token=0' in status.stdout.decode(), status.stdout.decode())
    cli('cli/cancel_queued', 'client', service.socket, 'cancel', job_id)
    cancelled = cli('cli/status_cancelled', 'client', service.socket, 'status', job_id)
    record('queue/cancel_queued_terminal', 'phase=finished' in cancelled.stdout.decode() and 'outcome=cancelled' in cancelled.stdout.decode(), cancelled.stdout.decode())
    record('queue/no_execution_workspaces', not list(service.workspaces.iterdir()))
    record('queue/no_worker_attempt_records', not any(p.name.startswith(('attempt-', 'worker-', 'instance-')) for p in service.queue.iterdir()))
    # This public command must enforce the installed candidate account. A local
    # user is not authorized to bypass the contained production stage.
    cli('helper/local_account_rejected', 'native-payload', committed, identity, expect=1)
    # Committed manifest tampering must be rejected rather than reported as a
    # successful retry. Restore the exact immutable bytes before teardown.
    target = committed / 'manifest'
    original = target.read_bytes()
    os.chmod(target, 0o600)
    target.write_bytes(bytes([original[0] ^ 1]) + original[1:])
    os.chmod(target, 0o400)
    raw('store/tampered_manifest_rejected', service.socket, NATIVE_BEGIN, upload_body(data), expected=BQ_CORRUPT)
    os.chmod(target, 0o600)
    target.write_bytes(original)
    os.chmod(target, 0o400)
    # Capacity is lifetime storage, including unfinished blobs. No payload is
    # executed or materialized by these protocol-only begin calls.
    store = service.queue / 'native-blobs'
    initial_entries = len(list(store.iterdir()))
    for i in range(CAP - initial_entries):
        unique = hashlib.sha256(('blackbox-quota-' + str(i)).encode()).hexdigest().encode()
        raw('store/quota_entry_' + str(i), service.socket, NATIVE_BEGIN, unique + struct.pack('<Q', 64))
    record('store/exact_entry_cap', len(list(store.iterdir())) == CAP, len(list(store.iterdir())))
    raw('store/quota_exhausted', service.socket, NATIVE_BEGIN, b'f' * 64 + struct.pack('<Q', 64), expected=BQ_FULL)
    cli('cli/upload_idempotent_at_capacity', 'client', service.socket, 'upload-program', good_path)


def test_materialization(service, fixtures):
    data = (fixtures / 'transcript.elf').read_bytes()
    canonical, identity, digest = manifest(data)
    queued = cli('materialize/submit_valid', 'client', service.socket, 'submit-program', 'native-blackbox-materialize', identity)
    job = re.search(r'job=(\d+)', queued.stdout.decode())
    record('materialize/job_receipt', bool(job), queued.stdout.decode())
    job_id = int(job.group(1)) if job else 1
    # Operator materialization CLI mutates this isolated queue only after the
    # authenticated server has stopped, preserving one writer at a time. It
    # copies bytes and never starts a manager unit or uploaded program.
    service.stop()
    recipes = service.installed / 'recipes'
    recipes.mkdir(mode=0o700)
    profile = recipes / 'native-execute-v1.recipe'
    profile.write_text('schema=1\nrecipe=native-execute-v1\nsource-manifest=BQ-NATIVE-V1\noperation=execute-once\nplatform=linux-x86-64-static-elf\narguments=none\ncompilation=unavailable\nbenchmark-metrics=unavailable\n')
    os.chmod(profile, 0o400)
    os.chmod(recipes, 0o500)
    os.chmod(service.installed, 0o500)
    os.chmod(service.workspaces, 0o2700)
    materialized = cli('materialize/valid', 'materialize', service.queue, service.installed, service.workspaces)
    match = re.search(r'job=(\d+) token=(\d+).*phase=preparing', materialized.stdout.decode())
    record('materialize/exact_reserved_job', bool(match) and int(match.group(1)) == job_id and int(match.group(2)) > 0, materialized.stdout.decode())
    token = int(match.group(2)) if match else 0
    attempt = service.workspaces / ('job-' + str(job_id) + '-attempt-' + str(token))
    for side in ('base', 'candidate'):
        source = attempt / side / 'source'
        record('materialize/' + side + '/exact_program', (source / 'program').read_bytes() == data)
        record('materialize/' + side + '/exact_manifest', (source / '.native-manifest').read_bytes() == canonical)
        record('materialize/' + side + '/modes', stat.S_IMODE(source.stat().st_mode) == 0o550 and stat.S_IMODE((source / 'program').stat().st_mode) == 0o550 and stat.S_IMODE((source / '.native-manifest').stat().st_mode) == 0o440)
    cli('materialize/reconcile_without_execution', 'workspace-reconcile', service.queue, service.workspaces, job_id, token)
    record('materialize/reconcile_removes_workspace', not attempt.exists())
    # A stored executable whose bytes no longer match its canonical identity
    # must become a source failure before preparing an executable workspace.
    cli('tamper/submit', 'submit', service.queue, 'github-actions', 'native-blackbox-tampered-program', 'native-execute-v1', identity, identity)
    program = service.queue / 'native-blobs' / identity / 'program'
    original = program.read_bytes()
    os.chmod(program, 0o600)
    program.write_bytes(original[:-1] + bytes([original[-1] ^ 1]))
    os.chmod(program, 0o400)
    failed = cli('tamper/materialize_rejected', 'materialize', service.queue, service.installed, service.workspaces, expect=1)
    record('tamper/source_mismatch_diagnostic', 'source-mismatch' in failed.stderr.decode(), failed.stderr.decode())
    record('tamper/no_workspace_left', not list(service.workspaces.iterdir()))
    os.chmod(program, 0o600)
    program.write_bytes(original)
    os.chmod(program, 0o400)


def restore_cleanup_permissions(root):
    for directory, subdirs, files in os.walk(root):
        os.chmod(directory, 0o700)


def main():
    global BINARY
    p = argparse.ArgumentParser()
    p.add_argument('binary')
    p.add_argument('--fixtures', default='work/native-blackbox-fixtures')
    p.add_argument('--output', default='work/native-program-blackbox-results.json')
    args = p.parse_args()
    BINARY = str(Path(args.binary).resolve())
    fixtures = Path(args.fixtures).resolve()
    with tempfile.TemporaryDirectory(prefix='bq-native-', dir='/tmp') as temporary:
        service = None
        try:
            service = Service(temporary)
            test_native(service, fixtures)
            test_materialization(service, fixtures)
        except Exception as error:
            record('harness/exception', False, type(error).__name__ + ': ' + str(error))
        finally:
            if service is not None:
                try:
                    service.close()
                except Exception as error:
                    record('harness/teardown', False, type(error).__name__ + ': ' + str(error))
            restore_cleanup_permissions(temporary)
        summary = {'checks': len(CHECKS), 'passed': sum(x['passed'] for x in CHECKS), 'failed': sum(not x['passed'] for x in CHECKS)}
        result = {'binary': BINARY, 'binary_sha256': hashlib.sha256(Path(BINARY).read_bytes()).hexdigest(), 'fixture_directory': str(fixtures), 'queue_root': temporary, 'execution_boundary': 'configured host lease held externally; authenticated queue tests plus stopped-service byte materialization fixtures; no manager/native payload execution', 'checks': CHECKS, 'runs': RUNS, 'summary': summary}
        Path(args.output).write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(summary))
        for check in CHECKS:
            if not check['passed']:
                print(json.dumps(check))
        return 1 if summary['failed'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
