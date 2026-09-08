import base64, gzip, hashlib, json, os, pathlib, subprocess

def git(*args, **kwargs):
    return subprocess.check_output(['git', *args], **kwargs).decode().strip()

parts = ''.join(pathlib.Path(f'.issue267/series{i}.b64').read_text().strip() for i in range(5))
manifest = json.loads(gzip.decompress(base64.b64decode(parts, validate=True)))
assert manifest['base'] == 'cfe58e298fcfc13781b7b3f2ae50d2e1e2556bc2'
expected = ['fix/267-forwarding-authority', 'fix/267-got-authority', 'fix/267-padding-authority']
assert [c['branch'] for c in manifest['commits']] == expected
git('checkout', '--detach', manifest['base'])
git('config', 'user.name', 'github-actions[bot]')
git('config', 'user.email', '41898282+github-actions[bot]@users.noreply.github.com')
results = []
for c in manifest['commits']:
    patch = c['patch'].encode()
    assert hashlib.sha256(patch).hexdigest() == c['sha256']
    subprocess.run(['git', 'apply', '--unidiff-zero', '--index', '-'], input=patch, check=True)
    paths = git('diff', '--cached', '--name-only').splitlines()
    assert not any(p.startswith(('.github/', '.issue267/')) for p in paths)
    assert git('write-tree') == c['tree']
    git('commit', '-m', c['message'])
    sha = git('rev-parse', 'HEAD')
    results.append(dict(branch=c['branch'], sha=sha, tree=c['tree']))
# All three trees have been checked before any branch is published.
auth = os.environ.copy()
auth['GIT_CONFIG_COUNT'] = '1'
auth['GIT_CONFIG_KEY_0'] = 'http.https://github.com/.extraheader'
auth['GIT_CONFIG_VALUE_0'] = 'AUTHORIZATION: basic ' + base64.b64encode(('x-access-token:' + os.environ['GH_TOKEN']).encode()).decode()
for r in results:
    assert not git('ls-remote', '--heads', 'origin', 'refs/heads/' + r['branch'], env=auth)
subprocess.run(['git', 'push', '--atomic', 'origin', *[r['sha'] + ':refs/heads/' + r['branch'] for r in results]], env=auth, check=True)
pathlib.Path(os.environ['RUNNER_TEMP'], 'published.json').write_text(json.dumps(results, indent=2) + '\n')
print(json.dumps(results, indent=2))
