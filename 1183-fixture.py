"""Temporary hosted-only validation machinery; excluded from the fixing PR."""
import fcntl
import os
from pathlib import Path
import re
import subprocess as sp
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
tmp = Path(sys.argv[2]).resolve()
assert os.getuid() == 0 and os.getpid() == 1, 'requires disposable root PID/mount namespace'
sp.run(['mount', '--make-rprivate', '/'], check=True)
base = Path(tempfile.mkdtemp(prefix='1183-ns-', dir=tmp))
base.chmod(0o755)
passwd = Path('/etc/passwd').read_text()
group = Path('/etc/group').read_text()
names = ['buster-bench', 'buster-bench-candidate', 'buster-github-runner']
for name in names:
    assert not any(line.startswith(name + ':') for line in passwd.splitlines())
for number in (65000, 65001, 65002):
    assert not any(line.split(':')[2] == str(number) for line in passwd.splitlines() if len(line.split(':')) > 2)
    assert not any(line.split(':')[2] == str(number) for line in group.splitlines() if len(line.split(':')) > 2)
passwd += ''.join(f'{name}:x:{65000+i}:{65000+i}:isolated fixture:/nonexistent:/usr/sbin/nologin\n' for i, name in enumerate(names))
pw = base / 'passwd'
gr = base / 'group'
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
Path('/run/.containerenv').write_text('disposable issue-1183 identity fixture; no systemd\n')

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
mkdir(root + '/workspaces/results/job-1-attempt-2', 65000, 65000, 0o700)
file(root + '/workspaces/results/job-1-attempt-2/payload', 65000, 65000, 0o400)
lease = open(root + '/lease/host.lock', 'r+')
fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
env = dict(os.environ, BUSTER_BROKER_LIVE_TEST='1')
checks = 0

def run(label, args, expected=0):
    global checks
    result = sp.run([str(a) for a in args], env=env, text=True, stdout=sp.PIPE, stderr=sp.STDOUT, timeout=90)
    print('FIXTURE_CASE', label, 'exit', result.returncode, flush=True)
    print(result.stdout, end='', flush=True)
    assert result.returncode == expected, (label, result.returncode, expected)
    checks += 1
    return result.stdout

tools = repo / 'build/bench-service-tools'
run('patched-cleanup', [tools / 'service-tests', '--cleanup-identity-only'])
red = run('original-cleanup', [tmp / '1183-red-service-tests', '--cleanup-identity-only'], 1)
assert 'failures=4' in red
for index in (3, 4):
    assert re.search(rf'CLEANUP_IDENTITY case={index} .*expected=refused actual=removed', red)
assert len(re.findall('expected=refused actual=removed', red)) == 2
probe = [tools / 'systemd-broker-live-test', '--isolation-only', '1', '2']
run('clean-provisioned-identities', probe)
groups('buster-bench-candidate')
run('candidate-service-group-rejected', probe, 1)
old = run('original-erases-membership', [tmp / '1183-old-groups'])
new = run('patched-preserves-membership', [tmp / '1183-new-groups'])
old_ids = [line for line in old.splitlines() if re.fullmatch(r'[0-9 ]+', line)]
new_ids = [line for line in new.splitlines() if re.fullmatch(r'[0-9 ]+', line)]
assert len(old_ids) == len(new_ids) == 1
assert set(old_ids[0].split()) == {'65001'}
assert set(new_ids[0].split()) == {'65000', '65001'}
run('contaminated-direct-private-read-detected', [tmp / '1183-new-private'], 1)
groups('buster-github-runner')
run('runner-service-group-rejected', probe, 1)
groups('', 'buster-bench,buster-github-runner')
run('runner-candidate-group-rejected', probe, 1)
groups()
worker = Path(root + '/queue/worker-1')
worker.unlink()
run('missing-private-fixture-not-a-pass', probe, 1)
file(worker, 65000, 65000, 0o440)
run('restored-positive', probe)
print(f'ISSUE_1183_NAMESPACE_REGRESSION cases={checks} result=pass', flush=True)
