"""Temporary PR-686 diagnostic recipe; never updates repository refs."""
import base64
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import urllib.request

root = Path.cwd()
work = root / 'integration-base'
entries = json.loads((root / 'tools/pr686-expected-blobs.json').read_text())
pr_paths = ['.github/workflows/ci.yml', '.github/workflows/ios-monitor-tests.yml',
            'android/run_tests.sh', 'android/run_tests_test.sh',
            'android/test_ci.sh', 'docs/agents/testing.md']

if sys.argv[1] == 'prepare':
    for path in pr_paths:
        result = subprocess.run(['git', 'merge-file', '-p', str(work / path),
                                 str(root / 'original-base' / path),
                                 str(root / 'candidate' / path)],
                                capture_output=True, check=True)
        (work / path).write_bytes(result.stdout)
    subprocess.run(['git', 'apply', str(root / 'tools/pr686-repair.patch')],
                   cwd=work, check=True)
    subprocess.run(['git', 'diff', '--check'], cwd=work, check=True)
elif sys.argv[1] != 'publish':
    raise SystemExit('expected prepare or publish')

for entry in entries:
    content = (work / entry['path']).read_bytes()
    digest = hashlib.sha1(b'blob ' + str(len(content)).encode() + b'\0' + content).hexdigest()
    if digest != entry['sha']:
        raise SystemExit('unexpected repair bytes: ' + entry['path'])
    print('VERIFIED', entry['path'], digest, flush=True)
    if sys.argv[1] == 'publish':
        body = json.dumps({'encoding': 'base64',
                           'content': base64.b64encode(content).decode()}).encode()
        request = urllib.request.Request(
            'https://api.github.com/repos/buster14a/buster/git/blobs', data=body,
            method='POST', headers={'Authorization': 'Bearer ' + os.environ['GH_TOKEN'],
                                    'Accept': 'application/vnd.github+json',
                                    'Content-Type': 'application/json',
                                    'X-GitHub-Api-Version': '2022-11-28'})
        with urllib.request.urlopen(request, timeout=30) as response:
            published = json.load(response)
        if published['sha'] != digest:
            raise SystemExit('published blob differs: ' + entry['path'])
        print('PUBLISHED', entry['path'], digest, flush=True)
if sys.argv[1] == 'publish':
    destination = Path(os.environ['RUNNER_TEMP']) / 'pr686-uploaded-blobs.json'
    destination.write_text(json.dumps(entries, indent=2) + '\n')
