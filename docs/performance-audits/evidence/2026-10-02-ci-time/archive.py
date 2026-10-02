"""One-off #709 evidence preservation. No compiler, test or benchmark execution."""
from pathlib import Path
import ast
import hashlib
import json
import os
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent
REPO = 'buster14a/buster'
BRANCH = 'codex/709-ci-time-evidence'
SOURCE = 'e424b387fcb51b00c1d19e87e2d369ae8a212792'
ORIGINAL_BUNDLE_SHA256 = '422a4cebdd04c3d27d80cb7a23c144b3ade681017caafe7023ba74c334a9d5b0'
LIMIT = 4 * 1024 * 1024

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None

def get_bytes(url, headers=None):
    with urllib.request.urlopen(urllib.request.Request(url, headers=headers or {}), timeout=60) as response:
        data = response.read(LIMIT + 1)
    if len(data) > LIMIT:
        raise ValueError('Download exceeds the bounded archive size')
    return data

def download(rows):
    if os.environ.get('GITHUB_REPOSITORY') != REPO or os.environ.get('GITHUB_REF') != 'refs/heads/' + BRANCH:
        raise ValueError('Publication is restricted to its designated repository/branch')
    token = os.environ['GH_TOKEN']
    headers = {'Authorization': 'Bearer ' + token, 'Accept': 'application/vnd.github+json',
               'X-GitHub-Api-Version': '2022-11-28', 'User-Agent': 'buster-ci-evidence-archive'}
    (ROOT / 'raw').mkdir(exist_ok=True)
    metadata = []
    for aid, filename, expected in rows:
        endpoint = f'https://api.github.com/repos/{REPO}/actions/artifacts/{aid}'
        info = json.loads(get_bytes(endpoint, headers))
        if info['id'] != aid or info['name'] + '.zip' != filename or info['expired']:
            raise ValueError(f'Artifact identity/expiry mismatch: {aid}')
        if info['digest'] != 'sha256:' + expected or info['workflow_run']['head_sha'] != SOURCE:
            raise ValueError(f'Artifact source/digest mismatch: {aid}')
        request = urllib.request.Request(endpoint + '/zip', headers=headers)
        try:
            urllib.request.build_opener(NoRedirect).open(request, timeout=60)
        except urllib.error.HTTPError as error:
            if error.code != 302:
                raise
            location = error.headers['Location']
            parsed = urllib.parse.urlsplit(location)
            if parsed.scheme != 'https' or not (parsed.hostname or '').endswith(('.blob.core.windows.net', '.actions.githubusercontent.com')):
                raise ValueError('Unexpected artifact storage origin')
        else:
            raise ValueError('Expected a signed artifact redirect')
        data = get_bytes(location)  # Never forward GitHub credentials to storage.
        if len(data) != info['size_in_bytes'] or hashlib.sha256(data).hexdigest() != expected:
            raise ValueError(f'Original ZIP verification failed: {aid}')
        (ROOT / 'raw' / filename).write_bytes(data)
        metadata.append(info)
        print(f'Verified artifact {aid}: {filename}', flush=True)
    (ROOT / 'publication-artifact-metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')

def main():
    constants = {}
    for node in ast.parse((ROOT / 'verify_artifacts.py').read_text()).body:
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name) and node.targets[0].id in ('A3', 'C4'):
            constants[node.targets[0].id] = ast.literal_eval(node.value)
    original = json.loads((ROOT / 'expected-original.json').read_text())
    rows = []
    for run, variant in [('36996168725', 'C4'), ('36991068435', 'A3')]:
        for line in constants[variant].splitlines():
            lane, aid, digest = line.split()
            rows.append((int(aid), f'desktop-{lane}-{run}-1.zip', digest))
    rows.append((11222445904, 'clang-analyzer-36996168725-1.zip',
                 'a19e677994802c67eeb66f9c4b7a12a7d21d49ce3c40ad79831fd4175144601b'))
    if len(rows) != 27 or {f'raw/{n}': d for _, n, d in rows} != {n: d for n, d in original.items() if n.startswith('raw/')}:
        raise ValueError('Frozen artifact inventory mismatch')
    if sys.argv[1:] != ['--local']:
        if sys.argv[1:]:
            raise ValueError('Only optional --local is supported')
        download(rows)
    subprocess.run([sys.executable, '-B', str(ROOT / 'replay.py')], check=True, timeout=180)
    names = list(original) + ['replay.py', 'expected-original.json', 'PUBLICATION.md']
    bundle = ROOT / 'evidence.zip'
    with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in names:
            entry = zipfile.ZipInfo(name, (2026, 10, 2, 12, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, (ROOT / name).read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    with zipfile.ZipFile(bundle) as archive:
        if set(archive.namelist()) != set(names):
            raise ValueError('Published bundle inventory mismatch')
        for name, expected in original.items():
            if hashlib.sha256(archive.read(name)).hexdigest() != expected:
                raise ValueError('Published original member mismatch: ' + name)
    receipt = dict(schema='buster-ci-time-publication-v1', source_revision=SOURCE,
                   original_bundle_sha256=ORIGINAL_BUNDLE_SHA256, original_members_verified=len(original),
                   original_raw_artifacts=27, published_members=len(names),
                   published_bundle_sha256=hashlib.sha256(bundle.read_bytes()).hexdigest(),
                   published_bundle_bytes=bundle.stat().st_size,
                   archive_commit_parent=os.environ.get('GITHUB_SHA', 'local-validation'),
                   publication_run_id=os.environ.get('GITHUB_RUN_ID', 'local-validation'),
                   compiler_tests_or_benchmarks_run=False,
                   qualification_accepted=False)
    (ROOT / 'publication.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, indent=2))

if __name__ == '__main__':
    main()
