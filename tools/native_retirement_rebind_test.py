#!/usr/bin/env python3
"""Focused integration tests for policy/snapshot/aggregate rebinding."""
from __future__ import annotations
import copy, hashlib, json
from pathlib import Path
import tempfile, unittest
import native_retirement_dependency_binding as authority
import native_retirement_external as external
import native_retirement_rebind as rebind
from native_retirement_rebind_contract import RebindError


def sha(data): return hashlib.sha256(data).hexdigest()
def canonical(value): return authority.canonical_json(value)

class NativeRetirementRebindTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(); self.root=Path(self.temp.name).resolve()
        for directory in ("docs","tools","src"):(self.root/directory).mkdir(parents=True,exist_ok=True)
        self.alpha=b"#define LEFT 1\n#define MIDDLE 1\n#define RIGHT 1\n"; self.beta=b"#define BETA 1\n"
        (self.root/"src/alpha.h").write_bytes(self.alpha); (self.root/"src/beta.h").write_bytes(self.beta)
        def row(source,destination):
            data=(self.root/source).read_bytes()
            return {"source":source,"provenance":f"repo:{source}","destination":destination,"bytes":len(data),"sha256":sha(data)}
        legacy={"schema":authority.LEGACY_SCHEMA,"version":1,"source_root":"..","metadata":[],"external_checkouts":[],"external_generated":[],
                "projects":[row("src/alpha.h","dependencies/project-include/alpha.h"),row("src/beta.h","dependencies/project-include/beta.h")]}
        legacy_raw=canonical(legacy); policy_raw,self.policy=authority.policy_from_legacy(legacy_raw)
        (self.root/authority.LEGACY_DESCRIPTOR_PATH).write_bytes(legacy_raw); (self.root/authority.POLICY_PATH).write_bytes(policy_raw)
        (self.root/rebind.PYTHON_BINDING_PATH).write_text("FULL_EXTERNAL_CHECKOUTS = ()\nFULL_EXTERNAL_GENERATED = ()\n",encoding="utf-8")
        (self.root/rebind.SDK_MANIFEST_PATH).write_bytes(canonical({"version":1,"archives":[],"files":[]}))
    def tearDown(self): self.temp.cleanup()
    def generated(self): return {path:(self.root/path).read_bytes() for path in rebind.TARGET_PATHS if (self.root/path).exists()}

    def test_check_read_only_refresh_and_second_refresh_idempotent(self):
        before={path.relative_to(self.root).as_posix():path.read_bytes() for path in self.root.rglob("*") if path.is_file()}
        self.assertEqual(rebind.run("check",self.root)["status"],"stale")
        after={path.relative_to(self.root).as_posix():path.read_bytes() for path in self.root.rglob("*") if path.is_file()}; self.assertEqual(before,after)
        self.assertEqual(rebind.run("refresh",self.root)["status"],"refreshed"); generated=self.generated()
        self.assertEqual(rebind.run("check",self.root)["status"],"current"); self.assertEqual(rebind.run("refresh",self.root)["status"],"current"); self.assertEqual(generated,self.generated())

    def test_different_sources_and_two_regions_combine_without_policy_write(self):
        rebind.run("refresh",self.root); policy=(self.root/authority.POLICY_PATH).read_bytes()
        (self.root/"src/alpha.h").write_bytes(self.alpha.replace(b"LEFT 1",b"LEFT 30").replace(b"RIGHT 1",b"RIGHT 40")); (self.root/"src/beta.h").write_bytes(b"#define BETA 2\n")
        report=rebind.run("refresh",self.root); self.assertEqual([row["source"] for row in report["source_changes"]],["src/alpha.h","src/beta.h"]); self.assertEqual((self.root/authority.POLICY_PATH).read_bytes(),policy)

    def test_external_preparation_precedes_repository_refresh_without_writes(self):
        rebind.run("refresh", self.root)
        old_generated = self.generated()
        policy = (self.root / authority.POLICY_PATH).read_bytes()
        (self.root / "src/alpha.h").write_bytes(b"#define LEFT 2\n")
        prepared = external.prepare(self.root / authority.POLICY_PATH, self.root)
        self.assertEqual(prepared["records"], 2)
        self.assertEqual(self.generated(), old_generated)
        self.assertEqual((self.root / authority.POLICY_PATH).read_bytes(), policy)
        self.assertEqual(rebind.run("check", self.root)["status"], "stale")
        self.assertEqual(rebind.run("refresh", self.root)["status"], "refreshed")
        self.assertEqual(rebind.run("check", self.root)["status"], "current")

    def test_missing_malformed_duplicate_snapshot_and_truncated_binding_repair(self):
        rebind.run("refresh",self.root); (self.root/authority.SNAPSHOT_PATH).write_text('{"records":[],"records":[]}\n'); (self.root/authority.BINDING_PATH).write_text("truncated\n")
        self.assertEqual(rebind.run("check",self.root)["status"],"stale"); self.assertEqual(rebind.run("refresh",self.root)["status"],"refreshed"); self.assertEqual(rebind.run("check",self.root)["status"],"current")

    def test_external_policy_cannot_be_refreshed(self):
        rebind.run("refresh",self.root); policy=json.loads((self.root/authority.POLICY_PATH).read_bytes()); policy["external_checkouts"]=[{"name":"x","repository":"x/y","revision":"1"*40,"path":"external/x"}]; (self.root/authority.POLICY_PATH).write_bytes(canonical(policy))
        with self.assertRaisesRegex(RebindError,"external checkout"): rebind.run("refresh",self.root)

    def test_sdk_policy_cannot_be_refreshed(self):
        policy=json.loads((self.root/authority.POLICY_PATH).read_bytes()); data=b"sdk\n"; path=self.root/"sdk-headers/a.h"; path.parent.mkdir(); path.write_bytes(data)
        policy["projects"].append({"source":"sdk-headers/a.h","provenance":"sdk/"+"2"*64+"/include/a.h","destination":"dependencies/project-include/a.h","bytes":len(data),"sha256":sha(data)})
        (self.root/authority.POLICY_PATH).write_bytes(canonical(policy)); (self.root/rebind.SDK_MANIFEST_PATH).write_bytes(canonical({"version":1,"archives":[{"name":"sdk","url":"https://invalid","sha256":"2"*64,"bytes":1}],"files":[{"archive":"sdk","member":"include/a.h","source":"sdk-headers/a.h","bytes":len(data),"sha256":"0"*64}]}))
        with self.assertRaisesRegex(RebindError,"SDK dependency"): rebind.run("refresh",self.root)

    def test_source_toctou_aborts_without_generated_writes(self):
        rebind.run("refresh",self.root); (self.root/"src/alpha.h").write_bytes(b"first\n"); plan=rebind.prepare(self.root); before=self.generated(); (self.root/"src/alpha.h").write_bytes(b"second\n")
        with self.assertRaisesRegex(RebindError,"changed after validation"): rebind.apply(plan)
        self.assertEqual(before,self.generated())

if __name__=="__main__": unittest.main()
