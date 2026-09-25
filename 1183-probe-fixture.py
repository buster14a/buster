"""Disposable hosted namespace probe; never run against production accounts."""
import fcntl
import os
from pathlib import Path
import socket
import struct
import subprocess as sp
import sys
import tempfile
import threading

repo = Path(sys.argv[1]).resolve()
tmp = Path(sys.argv[2]).resolve()
assert os.getuid() == 0 and os.getpid() == 1
sp.run(['mount', '--make-rprivate', '/'], check=True)
base = Path(tempfile.mkdtemp(prefix='1183-probe-', dir=tmp))
base.chmod(0o755)
passwd = Path('/etc/passwd').read_text()
group = Path('/etc/group').read_text()
names = ['buster-bench', 'buster-bench-candidate', 'buster-github-runner']
for index, name in enumerate(names):
    assert not any(line.startswith(name + ':') for line in passwd.splitlines())
    number = str(65000 + index)
    assert not any(line.split(':')[2] == number for line in passwd.splitlines() if len(line.split(':')) > 2)
    assert not any(line.split(':')[2] == number for line in group.splitlines() if len(line.split(':')) > 2)
passwd += ''.join(f'{name}:x:{65000+i}:{65000+i}:isolated fixture:/nonexistent:/usr/sbin/nologin\n' for i, name in enumerate(names))
pw, gr = base / 'passwd', base / 'group'
pw.write_text(passwd)

def groups(service_members='', candidate_members='buster-bench'):
    gr.write_text(group + f'buster-bench:x:65000:{service_members}\nbuster-bench-candidate:x:65001:{candidate_members}\nbuster-github-runner:x:65002:\n')

groups()
for source, destination in ((pw, '/etc/passwd'), (gr, '/etc/group')):
    source.chmod(0o644)
    sp.run(['mount', '--bind', str(source), destination], check=True)
var = base / 'var-lib'
var.mkdir(mode=0o755)
sp.run(['mount', '--bind', str(var), '/var/lib'], check=True)
sp.run(['mount', '-t', 'tmpfs', '-o', 'mode=755', 'tmpfs', '/run'], check=True)
Path('/run/.containerenv').write_text('isolated 1183 probe fixture\n')

def mkdir(path, uid, gid, mode):
    target = Path(path)
    target.mkdir()
    os.chown(target, uid, gid)
    target.chmod(mode)

def file(path, uid, gid, mode):
    target = Path(path)
    target.write_text('controlled fixture\n')
    os.chown(target, uid, gid)
    target.chmod(mode)

root = '/var/lib/buster-bench'
mkdir(root, 65000, 65001, 0o710)
mkdir(root + '/queue', 65000, 65000, 0o710)
file(root + '/queue/worker-1', 65000, 65000, 0o440)
file(root + '/queue/worker-instance-1', 65000, 65000, 0o440)
mkdir(root + '/lease', 65000, 65000, 0o710)
file(root + '/lease/host.lock', 65000, 65000, 0o640)
mkdir(root + '/workspaces', 65000, 65001, 0o2710)
mkdir(root + '/workspaces/results', 65000, 65000, 0o710)
result = root + '/workspaces/results/job-1-attempt-2'
mkdir(result, 65000, 65000, 0o700)
payload = Path(result + '/payload')
file(payload, 65000, 65000, 0o400)
lease = open(root + '/lease/host.lock', 'r+')
fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
env = dict(os.environ, BUSTER_BROKER_LIVE_TEST='1')
tools = repo / 'build/bench-service-tools'
probe = [tools / 'systemd-broker-live-test', '--isolation-only', '1', '2']
wrapper = tmp / '1183-socket-fixture'
cases = 0

def run(label, args, expected=0, *required):
    global cases
    done = sp.run([str(arg) for arg in args], env=env, text=True, stdout=sp.PIPE, stderr=sp.STDOUT, timeout=20)
    print('PROBE_FIXTURE_CASE', label, 'exit', done.returncode, flush=True)
    print(done.stdout, end='', flush=True)
    assert done.returncode == expected, (label, done.returncode, expected)
    for marker in required:
        assert marker in done.stdout, (label, marker)
    cases += 1
    return done.stdout

run('existing-payload-positive', probe, 0, 'BROKER_PRIVATE_PAYLOAD_ACCESS identity=buster-bench-candidate read/write/rw_denied=yes',
    'BROKER_PRIVATE_PAYLOAD_ACCESS identity=buster-github-runner read/write/rw_denied=yes')
payload.unlink()
run('missing-existing-payload-rejected', probe, 1, 'BROKER_PRIVATE_PAYLOAD path=')
file(payload, 65000, 65000, 0o444)
run('group-readable-payload-rejected', probe, 1)
payload.chmod(0o400)
groups('buster-bench-candidate')
run('candidate-service-membership-rejected', probe, 1)
groups('buster-github-runner')
run('runner-service-membership-rejected', probe, 1)
groups()

socket_directory = '/run/buster-bench-systemd-broker'
mkdir(socket_directory, 0, 65000, 0o710)
socket_path = socket_directory + '/control.sock'
listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
listener.bind(socket_path)
os.chown(socket_path, 65000, 65000)
os.chmod(socket_path, 0o600)
listener.listen(4)
run('candidate-actual-connect-denied', [wrapper, 'candidate'], 0,
    'BROKER_SOCKET_DAC path=' + socket_path, 'authorization_denied=yes')
run('runner-actual-connect-denied', [wrapper, 'runner'], 0,
    'BROKER_SOCKET_DAC path=' + socket_path, 'authorization_denied=yes')

def response(frame):
    errors = []
    def server():
        try:
            connection, _ = listener.accept()
            with connection:
                request = connection.recv(256)
                assert len(request) == 176
                magic, version, operation, stage, signal, reserved, job, attempt = struct.unpack_from('=IIIIIIQQ', request)
                assert (magic, version, operation, stage, signal, reserved, job, attempt) == (0x42515344, 1, 2, 0, 3, 0, 1, 2)
                assert request[40:170] == bytes(130)
                if frame is not None:
                    connection.send(frame)
        except Exception as exc:
            errors.append(exc)
    worker = threading.Thread(target=server, daemon=True)
    worker.start()
    return worker, errors

worker, errors = response(struct.pack('=IIi', 3, 4, 126))
run('root-correlated-valid-status', [wrapper, 'root'], 0,
    'BROKER_ROOT_PEER_REJECTION received=12 kind=3 length=4 status=126 verified=yes')
worker.join(timeout=10)
assert not worker.is_alive() and not errors, errors

worker, errors = response(struct.pack('=IIi', 2, 4, 126))
run('wrong-frame-kind-rejected', [wrapper, 'root'], 1)
worker.join(timeout=10)
assert not worker.is_alive() and not errors, errors

worker, errors = response(None)
run('broken-transport-rejected', [wrapper, 'root'], 1)
worker.join(timeout=10)
assert not worker.is_alive() and not errors, errors

listener.close()
Path(socket_path).unlink()
run('missing-socket-inode-rejected', [wrapper, 'candidate'], 1,
    'BROKER_SOCKET_FIXTURE path=' + socket_path + ' verified=no')
print(f'ISSUE_1183_PROBE_FIXTURE cases={cases} result=pass', flush=True)
