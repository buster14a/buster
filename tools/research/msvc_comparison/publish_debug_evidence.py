"""Archive one pinned diagnostic artifact on one reserved evidence branch."""
import base64
import hashlib
import json
import os
from pathlib import Path
import urllib.error
import urllib.parse
import urllib.request

REPO = os.environ['GITHUB_REPOSITORY']
TOKEN = os.environ['GH_TOKEN']
BRANCH = 'codex/2277-msvc-debug-evidence'
EXPECTED_HEAD = os.environ['EVIDENCE_EXPECTED_HEAD']
ARTIFACT = 11230635693
RUN = 37016724916
WORKFLOW_SHA = '5dafc4dcf94a730007b097254ca700a5cf86acf9'
ZIP_SHA256 = '34c1a7bf0d1659b06277e49a24190e2f6a4c4652e8646173f1a353fb7dcace35'
DESTINATION = 'docs/audits/2026-10-02-msvc-debug-2277/full-debug-evidence.zip'

def api(path, method='GET', data=None):
    raw = None if data is None else json.dumps(data).encode()
    request = urllib.request.Request('https://api.github.com/repos/' + REPO + '/' + path, data=raw, method=method,
                                     headers={'Authorization': 'Bearer ' + TOKEN, 'Accept': 'application/vnd.github+json',
                                              'X-GitHub-Api-Version': '2022-11-28', 'Content-Type': 'application/json'})
    with urllib.request.urlopen(request, timeout=120) as response:
        return json.load(response)

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, file_pointer, code, message, headers, new_url):
        return None

ref_path = 'git/ref/heads/' + BRANCH
assert api(ref_path)['object']['sha'] == EXPECTED_HEAD
artifact = api('actions/artifacts/' + str(ARTIFACT))
assert artifact['workflow_run']['id'] == RUN
assert artifact['workflow_run']['head_sha'] == WORKFLOW_SHA
assert not artifact['expired']
archive_url = 'https://api.github.com/repos/' + REPO + '/actions/artifacts/' + str(ARTIFACT) + '/zip'
request = urllib.request.Request(archive_url, headers={'Authorization': 'Bearer ' + TOKEN})
try:
    urllib.request.build_opener(NoRedirect).open(request, timeout=120)
except urllib.error.HTTPError as error:
    assert error.code in (301, 302, 303, 307, 308)
    location = error.headers['Location']
    assert urllib.parse.urlsplit(location).scheme == 'https'
else:
    raise RuntimeError('Expected an artifact download redirect')
# Authorization is intentionally absent from the cross-host signed download.
with urllib.request.urlopen(location, timeout=120) as response:
    archive = response.read()
assert hashlib.sha256(archive).hexdigest() == ZIP_SHA256
blob = api('git/blobs', 'POST', {'encoding': 'base64', 'content': base64.b64encode(archive).decode()})
expected_blob = hashlib.sha1(b'blob ' + str(len(archive)).encode() + b'\0' + archive).hexdigest()
assert blob['sha'] == expected_blob
commit = api('git/commits/' + EXPECTED_HEAD)
tree = api('git/trees', 'POST', {'base_tree': commit['tree']['sha'], 'tree': [
    {'path': DESTINATION, 'mode': '100644', 'type': 'blob', 'sha': blob['sha']}]})
new_commit = api('git/commits', 'POST', {'message': 'docs: retain complete matched MSVC debug artifact (#2277)',
                                      'tree': tree['sha'], 'parents': [EXPECTED_HEAD]})
assert api(ref_path)['object']['sha'] == EXPECTED_HEAD
api('git/refs/heads/' + BRANCH, 'PATCH', {'sha': new_commit['sha'], 'force': False})
assert api(ref_path)['object']['sha'] == new_commit['sha']
result = {'commit_sha': new_commit['sha'], 'branch': BRANCH, 'archive_path': DESTINATION,
          'zip_sha256': ZIP_SHA256, 'archive_git_blob_sha': blob['sha'], 'archive_bytes': len(archive)}
Path('publication-result.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result))
