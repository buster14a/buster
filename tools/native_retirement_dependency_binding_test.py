#!/usr/bin/env python3
"""Unit tests for native-retirement policy/snapshot/binding ownership."""
from __future__ import annotations
import copy, hashlib, json
from pathlib import Path
import tempfile, unittest
import native_retirement_dependency_binding as binding


def digest(data: bytes) -> str: return hashlib.sha256(data).hexdigest()
def canonical(value) -> bytes: return (json.dumps(value, ensure_ascii=True, indent=2) + "\n").encode()


class DependencyBindingTest(unittest.TestCase):
    def setUp(self):
        self.files = {
            "src/alpha.h": b"#define LEFT 1\n#define MIDDLE 1\n#define RIGHT 1\n",
            "src/beta.h": b"#define BETA 1\n",
            "external/vendor.h": b"#define EXTERNAL 1\n",
            "sdk-headers/test/sdk.h": b"#define SDK 1\n",
        }
        def row(source, provenance, destination):
            result = {"source": source, "provenance": provenance, "destination": destination}
            if not provenance.startswith("repo:"):
                result.update(bytes=len(self.files[source]), sha256=digest(self.files[source]))
            return result
        self.policy = {
            "schema": binding.POLICY_SCHEMA, "version": 1,
            "legacy_descriptor": {"path": binding.LEGACY_DESCRIPTOR_PATH, "sha256": "a"*64},
            "source_root": "..",
            "projects": [
                row("src/alpha.h", "repo:src/alpha.h", "dependencies/project-include/alpha.h"),
                row("src/beta.h", "repo:src/beta.h", "dependencies/project-include/beta.h"),
                row("external/vendor.h", "github/example/vendor/" + "1"*40 + "/vendor.h", "dependencies/project-include/vendor.h"),
                row("sdk-headers/test/sdk.h", "sdk/" + "2"*64 + "/include/sdk.h", "dependencies/project-include/sdk.h"),
            ], "resources": [],
        }
        self.policy_raw = canonical(self.policy)
        binding.parse_policy(self.policy_raw)

    def identity(self, source):
        data = self.files[source]; return len(data), digest(data)

    def snapshot(self): return binding.render_snapshot(self.policy_raw, self.policy, self.identity)

    def values(self, snapshot_raw):
        return {"schema": binding.BINDING_SCHEMA, "version": 1,
                "policy_path": binding.POLICY_PATH, "policy_sha256": digest(self.policy_raw),
                "snapshot_path": binding.SNAPSHOT_PATH, "snapshot_sha256": digest(snapshot_raw),
                "receipt_sha256": "3"*64, "project_sha256": "4"*64, "ledger_sha256": "5"*64}

    def test_policy_migration_removes_only_repo_identities(self):
        legacy = copy.deepcopy(self.policy)
        legacy["schema"] = binding.LEGACY_SCHEMA
        legacy.pop("legacy_descriptor")
        for record in legacy["projects"]:
            if record["provenance"].startswith("repo:"):
                data = self.files[record["source"]]
                record["bytes"], record["sha256"] = len(data), digest(data)
        legacy_raw = canonical(legacy)
        migrated_raw, migrated = binding.policy_from_legacy(legacy_raw)
        expected = copy.deepcopy(self.policy)
        expected["legacy_descriptor"]["sha256"] = digest(legacy_raw)
        self.assertEqual(migrated, expected)
        for record in migrated["projects"]:
            self.assertEqual("bytes" in record, not record["provenance"].startswith("repo:"))
        snapshot_raw, snapshot = binding.render_snapshot(migrated_raw, migrated, self.identity)
        resolved = binding.resolved_manifest(migrated, binding.parse_snapshot(snapshot_raw, migrated_raw, migrated))
        self.assertEqual(resolved, legacy)

    def test_snapshot_only_contains_repository_identities(self):
        raw, records = self.snapshot()
        parsed = binding.parse_snapshot(raw, self.policy_raw, self.policy)
        self.assertEqual(parsed, records)
        self.assertEqual([r["source"] for r in parsed], ["src/alpha.h", "src/beta.h"])
        self.assertNotIn(b"external/vendor.h", raw); self.assertNotIn(b"sdk-headers/test/sdk.h", raw)
        resolved = binding.resolved_manifest(self.policy, parsed)
        for record in resolved["projects"]:
            if record["provenance"].startswith("repo:"):
                self.assertEqual(record["sha256"], digest(self.files[record["source"]]))

    def test_two_different_sources_combine_deterministically(self):
        self.files["src/alpha.h"] = self.files["src/alpha.h"].replace(b"LEFT 1", b"LEFT 2")
        self.files["src/beta.h"] = b"#define BETA 2\n"
        first, _ = self.snapshot(); second, _ = self.snapshot(); self.assertEqual(first, second)
        rows = {r["source"]: r for r in binding.parse_snapshot(first, self.policy_raw, self.policy)}
        self.assertEqual(rows["src/alpha.h"]["sha256"], digest(self.files["src/alpha.h"]))
        self.assertEqual(rows["src/beta.h"]["sha256"], digest(self.files["src/beta.h"]))

    def test_two_regions_one_source_do_not_change_policy(self):
        before = self.policy_raw
        self.files["src/alpha.h"] = self.files["src/alpha.h"].replace(b"LEFT 1", b"LEFT 30").replace(b"RIGHT 1", b"RIGHT 40")
        raw, _ = self.snapshot(); rows = binding.parse_snapshot(raw, self.policy_raw, self.policy)
        alpha = next(r for r in rows if r["source"] == "src/alpha.h")
        self.assertEqual(alpha["sha256"], digest(self.files["src/alpha.h"])); self.assertEqual(self.policy_raw, before)

    def test_missing_duplicate_unsorted_unexpected_snapshot_fail(self):
        raw, _ = self.snapshot(); value = json.loads(raw)
        cases = []
        x=copy.deepcopy(value); x["records"].pop(); cases.append((x,"exactly cover"))
        x=copy.deepcopy(value); x["records"].append(copy.deepcopy(x["records"][0])); cases.append((x,"duplicate"))
        x=copy.deepcopy(value); x["records"].reverse(); cases.append((x,"sorted"))
        x=copy.deepcopy(value); x["records"][0]["destination"]="bad"; cases.append((x,"unexpected"))
        for malformed, message in cases:
            with self.subTest(message=message), self.assertRaisesRegex(binding.BindingError, message):
                binding.parse_snapshot(canonical(malformed), self.policy_raw, self.policy)

    def test_policy_rejects_generated_repo_fields_and_snapshot_rejects_external_sdk(self):
        bad = copy.deepcopy(self.policy); bad["projects"][0]["bytes"] = 1; bad["projects"][0]["sha256"]="0"*64
        with self.assertRaisesRegex(binding.BindingError, "generated identity"):
            binding.parse_policy(canonical(bad))
        raw,_=self.snapshot(); value=json.loads(raw)
        for source in ("external/vendor.h","sdk-headers/test/sdk.h"):
            injected=copy.deepcopy(value); injected["records"].append({"source":source,"bytes":len(self.files[source]),"sha256":digest(self.files[source])}); injected["records"].sort(key=lambda x:x["source"])
            with self.subTest(source=source), self.assertRaisesRegex(binding.BindingError,"exactly cover"):
                binding.parse_snapshot(canonical(injected),self.policy_raw,self.policy)

    def test_duplicate_json_and_policy_hash_mismatch_fail(self):
        with self.assertRaisesRegex(binding.BindingError,"duplicate JSON"):
            binding.strict_json(b'{"a":1,"a":2}',"test")
        raw,_=self.snapshot()
        with self.assertRaisesRegex(binding.BindingError,"policy binding mismatch"):
            binding.parse_snapshot(raw,self.policy_raw+b"\n",self.policy)

    def test_binding_round_trip_and_malformed_forms(self):
        snapshot_raw,_=self.snapshot(); raw=binding.render_binding(self.values(snapshot_raw))
        self.assertEqual(binding.render_binding(binding.parse_binding(raw)),raw)
        cases=[(raw[:-20],"missing or unexpected|canonical"),
               (raw.replace(b"#define BUSTER_NATIVE_RETIREMENT_LEDGER_SHA256",b"#define BUSTER_NATIVE_RETIREMENT_LEDGER_SHA256_BAD"),"missing or unexpected"),
               (raw.replace(b"#define BUSTER_NATIVE_RETIREMENT_BINDING_VERSION 1\n",b"#define BUSTER_NATIVE_RETIREMENT_BINDING_VERSION 1\n#define BUSTER_NATIVE_RETIREMENT_BINDING_VERSION 1\n"),"duplicate")]
        for malformed,message in cases:
            with self.subTest(message=message), self.assertRaisesRegex(binding.BindingError,message): binding.parse_binding(malformed)

    def test_valid_stale_quartet_fails_independent_check(self):
        snapshot_raw,_=self.snapshot(); values=self.values(snapshot_raw)
        stale=binding.parse_binding(binding.render_binding(dict(values,receipt_sha256="6"*64)))
        with self.assertRaisesRegex(binding.BindingError,"receipt_sha256.*independent"):
            binding.verify_materialized_identities(stale,{k:values[k] for k in ("policy_sha256","receipt_sha256","project_sha256","ledger_sha256")})

    def test_legacy_raw_is_reused_only_for_exact_resolved_descriptor(self):
        snapshot_raw, rows=self.snapshot(); resolved=binding.resolved_manifest(self.policy,rows); legacy_raw=canonical(resolved)
        policy = copy.deepcopy(self.policy); policy["legacy_descriptor"]["sha256"] = digest(legacy_raw)
        self.assertEqual(binding.render_resolved_descriptor(policy,rows,legacy_raw),legacy_raw)
        changed=copy.deepcopy(rows); changed=list(changed); changed[0]=dict(changed[0],bytes=changed[0]["bytes"]+1)
        self.assertNotEqual(binding.render_resolved_descriptor(policy,tuple(changed),legacy_raw),legacy_raw)

    def test_load_authority_cross_binds_all_inputs(self):
        snapshot_raw, rows=self.snapshot(); values=self.values(snapshot_raw)
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for relative,data in ((binding.POLICY_PATH,self.policy_raw),(binding.SNAPSHOT_PATH,snapshot_raw),(binding.BINDING_PATH,binding.render_binding(values))):
                path=root/relative; path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(data)
            actual,resolved,parsed=binding.load_authority(root)
            self.assertEqual(actual,values); self.assertEqual(parsed,rows); self.assertEqual(resolved["projects"][0]["sha256"],digest(self.files["src/alpha.h"]))
            (root/binding.SNAPSHOT_PATH).write_bytes(snapshot_raw+b"\n")
            with self.assertRaisesRegex(binding.BindingError,"snapshot identity mismatch|cannot decode"): binding.load_authority(root)

if __name__ == "__main__": unittest.main()
