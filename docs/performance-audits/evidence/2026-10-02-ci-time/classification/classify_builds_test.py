"""Offline controls for build-time classification; no captured command runs."""
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import classify_builds as audit

BUNDLE = Path(os.environ.get('BUSTER_CI_CLASSIFICATION_BUNDLE',
              str(Path(__file__).resolve().parent.parent / 'evidence.zip')))


class ClassificationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.records, cls.sources = audit.collect(BUNDLE)

    def test_complete_sample_totals(self):
        self.assertEqual(len(self.sources), 27)
        self.assertEqual(len(self.records), 44)
        for run, total in audit.TOTALS.items():
            rows = [r for r in self.records if r['run_id'] == run]
            self.assertEqual(len(rows), 22)
            self.assertEqual(sum(r['build_elapsed_us'] for r in rows), total)

    def test_shared_configuration_is_not_duplicated(self):
        rows = [r for r in self.records if r['configuration'] == 'Debug+Release (shared)']
        self.assertEqual(len(rows), 2)
        self.assertEqual({r['os'] for r in rows}, {'macos'})
        self.assertEqual(sum(len(r['row_ids']) for r in self.records), 46)

    def test_every_dimension_is_an_exhaustive_partition(self):
        for dimension in audit.DIMENSIONS:
            audit.aggregate(self.records, [dimension])
        audit.aggregate(self.records, audit.JOINT)

    def test_compiler_family_and_vendor_are_distinct_views(self):
        apple = [r for r in self.records if r['compiler'] == 'Apple Clang']
        self.assertEqual(len(apple), 4)
        self.assertEqual({r['compiler_family'] for r in apple}, {'Clang'})

    def test_fuzz_support_does_not_imply_sanitizer(self):
        rows = [r for r in self.records if r['fuzz_support'] == 'on' and r['sanitizer'] == 'off']
        self.assertEqual(len(rows), 6)
        self.assertEqual({r['configuration'] for r in rows}, {'Release'})

    def test_unknown_and_duplicate_configurations_rejected(self):
        for value in ('', 'RelWithDebInfo', 'Debug;Debug'):
            with self.assertRaises(ValueError):
                audit.configuration_bucket(value)

    def test_missing_tree_changes_total(self):
        with self.assertRaisesRegex(ValueError, 'exhaustive partition'):
            audit.aggregate(self.records[:-1], ['configuration'])

    def test_wrong_outer_hash_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'invalid.zip'
            path.write_bytes(b'not an evidence archive')
            with self.assertRaisesRegex(ValueError, 'unrecognized evidence ZIP'):
                audit.collect(path)

    def check_mutation(self, member, transform, message):
        # Re-bind ONLY the synthetic fixture's package/container hashes so the
        # tested inner invariant, rather than the outer digest guard, is reached.
        raw_name = 'desktop-linux-x86_64-checks-36991068435-1.zip'
        with zipfile.ZipFile(BUNDLE) as package:
            files = {n: package.read(n) for n in package.namelist()}
        with zipfile.ZipFile(io.BytesIO(files['raw/' + raw_name])) as archive:
            inner = {n: archive.read(n) for n in archive.namelist()}
        inner[member] = transform(inner[member])
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, 'w', zipfile.ZIP_DEFLATED) as out:
            for name, value in inner.items():
                out.writestr(name, value)
        changed = buffer.getvalue()
        files['raw/' + raw_name] = changed
        inventory = json.loads(files['derived/verified_artifacts.json'])
        for entry in inventory:
            if entry['filename'] == raw_name:
                entry['published_sha256'] = hashlib.sha256(changed).hexdigest()
                entry['bytes'] = len(changed)
        files['derived/verified_artifacts.json'] = json.dumps(inventory).encode()
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'synthetic.zip'
            with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as out:
                for name, value in files.items():
                    out.writestr(name, value)
            fixture_hash = hashlib.sha256(path.read_bytes()).hexdigest()
            with patch.object(audit, 'BUNDLES', {fixture_hash}):
                with self.assertRaisesRegex(ValueError, message):
                    audit.collect(path)

    def test_incomplete_summary_rejected(self):
        def change(raw):
            value = json.loads(raw)
            value['complete'] = False
            return json.dumps(value).encode()
        self.check_mutation('matrix-phases/summary.json', change, 'incomplete phase summary')

    def test_source_mismatch_rejected(self):
        def change(raw):
            value = json.loads(raw)
            value['identity']['source_revision'] = '0' * 40
            return json.dumps(value).encode()
        self.check_mutation('matrix-phases/summary.json', change, 'source mismatch')

    def test_negative_build_interval_rejected(self):
        def change(raw):
            value = json.loads(raw)
            value['trees'][0]['elapsed_us']['build'] = -1
            return json.dumps(value).encode()
        self.check_mutation('matrix-phases/summary.json', change, 'invalid build interval')

    def test_cache_policy_disagreement_rejected(self):
        path = ('configure/build-checks-ci_on-cc_clang-sanitize_on-fuzz_available_on-configs_Debug/'
                'CMakeCache.txt')
        self.check_mutation(path,
            lambda raw: raw.replace(b'BUSTER_SANITIZE:BOOL=ON', b'BUSTER_SANITIZE:BOOL=OFF'),
            'cache policy mismatch')


if __name__ == '__main__':
    unittest.main(verbosity=2)
