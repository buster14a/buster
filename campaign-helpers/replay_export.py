#!/usr/bin/env python3
"""Replay a physical #880-style export published by LOCAL.

usage: replay_export.py RELEASE_TAG ASSET_BASENAME SERVICE_BINARY [JOB TOKEN BASE CANDIDATE BOOT]

Downloads ASSET_BASENAME.export and ASSET_BASENAME.export-receipt.txt from the
buster14a/buster release RELEASE_TAG into a fresh directory, prints their
SHA-256 (compare with LOCAL's receipt), runs `unpack-export`, then checks:
- every top-level record's SHA-256 (compare with the receipt);
- BQ-BUNDLE-V1 entries/bytes against the declared totals, each entry's size and
  hash, no unlisted file, and the manifest's bundle-sha256;
- throughput/complete.txt hashes and summary.json `valid`, when present;
- with JOB..BOOT given, both frozen-tree inventories parse under
  .github/scripts/issue1162_frozen_tree_evidence.parse_receipt, bound to the
  manifest's binary digests.
Run from a checkout whose tools/bench_service matches the installed service.
"""
import hashlib, json, os, re, subprocess, sys, tempfile, urllib.request

tag, base, service = sys.argv[1:4]
extra = sys.argv[4:]
work = tempfile.mkdtemp(prefix="replay-")
for suffix in (".export", ".export-receipt.txt"):
    url = f"https://github.com/buster14a/buster/releases/download/{tag}/{base}{suffix}"
    with urllib.request.urlopen(url) as r, open(os.path.join(work, base + suffix), "wb") as f:
        f.write(r.read())
    b = open(os.path.join(work, base + suffix), "rb").read()
    print(f"{hashlib.sha256(b).hexdigest()}  {len(b)} B  {base}{suffix}")
receipt = re.search(r"export-receipt-sha256=([0-9a-f]{64})", open(os.path.join(work, base + ".export-receipt.txt")).read()).group(1)
os.mkdir(os.path.join(work, "un"), 0o700)
out = os.path.join(work, "un", "out")
rc = subprocess.run([service, "unpack-export", os.path.join(work, base + ".export"), out, receipt]).returncode
print("unpack_exit", rc)
if rc:
    sys.exit(rc)
os.chdir(out)
for name in sorted(os.listdir(".")):
    if os.path.isfile(name):
        print(hashlib.sha256(open(name, "rb").read()).hexdigest()[:8], name)
manifest = open("validate-buster-v1.manifest").read()
print("\n".join(l for l in manifest.splitlines() if re.match(r"(status|stage|process-result|job-id|attempt-token|bundle-sha256|.*binary-sha256)=", l)))
lines = open("validate-buster-v1.bundle").read().splitlines()
entries, declared_n, declared_b = lines[3:], int(lines[1].split("=")[1]), int(lines[2].split("=")[1])
names, total, bad = set(), 0, 0
for e in entries:
    h, size, path = e.split(" ", 2)
    data = open(path, "rb").read()
    total += int(size); names.add(path)
    bad += len(data) != int(size) or hashlib.sha256(data).hexdigest() != h
files = {os.path.relpath(os.path.join(r, f), ".") for r, _, fs in os.walk(".") for f in fs}
control = {"validate-buster-v1.bundle", "validate-buster-v1.manifest", "validate-buster-v1.outcome"}
print("bundle entries", len(entries), "declared", declared_n, "bytes", total, "declared", declared_b,
      "mismatches", bad, "unlisted", sorted(files - names - control))
print("manifest-bundle-digest-match", hashlib.sha256(open("validate-buster-v1.bundle", "rb").read()).hexdigest() in manifest)
if os.path.exists("throughput/complete.txt"):
    ok = sum(hashlib.sha256(open("throughput/" + p[1], "rb").read()).hexdigest() == p[0]
             for p in (l.split() for l in open("throughput/complete.txt")) if len(p) == 2 and len(p[0]) == 64)
    print("throughput complete verified", ok, "valid", json.load(open("throughput/summary.json")).get("valid"))
if len(extra) == 5:
    job, token, b_rev, c_rev, boot = int(extra[0]), int(extra[1]), extra[2], extra[3], extra[4]
    sys.path.insert(0, os.path.join(os.environ.get("REPO", os.getcwd()), ".github/scripts"))
    import issue1162_frozen_tree_evidence as ft
    workspace = f"/var/lib/buster-bench/workspaces/job-{job}-attempt-{token}"
    for stage, key in (("base-build", "base-binary-sha256"), ("candidate-build", "candidate-binary-sha256")):
        path = f"validate-buster-v1.{stage}.inventory"
        m = re.search(rf"^{key}=([0-9a-f]{{64}})$", manifest, re.M)
        if not os.path.exists(path) or not m:
            continue
        root = workspace + ("/base/build" if stage == "base-build" else "/candidate/build")
        r = ft.parse_receipt(open(path, "rb").read(), stage, job, token, b_rev, c_rev, boot, root, m.group(1))
        print(stage, "PARSE_OK nodes", r["node_count"])
print("scratch:", work)
