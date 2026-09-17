#!/usr/bin/env python3
"""Exercise SDK archive binding and publication with an offline archive."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

import native_retirement_sdks as sdk


class SdkTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.cache = self.root / 'cache'
        self.cache.mkdir()
        archive = self.root / 'archive.zip'
        with zipfile.ZipFile(archive, 'w') as package:
            package.writestr('include/header.h', b'header bytes\n')
        data = archive.read_bytes()
        sha = hashlib.sha256(data).hexdigest()
        archive.rename(self.cache / sha)
        record = {'archive': 'sdk', 'member': 'include/header.h', 'source': 'sdk-headers/test/header.h',
                  'bytes': 13, 'sha256': hashlib.sha256(b'header bytes\n').hexdigest()}
        self.manifest = {'version': 1, 'archives': [{'name': 'sdk', 'url': 'https://invalid.example/unused',
                                                  'sha256': sha, 'bytes': len(data)}], 'files': [record]}
        self.dependency = {'projects': [dict(record, provenance=f'sdk/{sha}/include/header.h')]}

    def tearDown(self):
        self.temporary.cleanup()

    def prepare(self):
        manifest = self.root / 'sdk.json'
        manifest.write_text(json.dumps(self.manifest))
        dependency = self.root / 'dependencies.json'
        dependency.write_text(json.dumps(self.dependency))
        return sdk.prepare(manifest, dependency, self.root, self.cache)

    def test_verified_cache_and_existing_headers_are_rechecked(self):
        self.assertEqual(self.prepare(), 1)
        output = self.root / 'sdk-headers/test/header.h'
        self.assertEqual(output.read_bytes(), b'header bytes\n')
        output.write_bytes(b'altered bytes')
        with self.assertRaisesRegex(ValueError, 'identity mismatch'):
            self.prepare()

    def test_archive_tamper_is_rejected_before_publication(self):
        archive = next(self.cache.iterdir())
        data = bytearray(archive.read_bytes())
        data[0] ^= 1
        archive.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            self.prepare()
        self.assertFalse((self.root / 'sdk-headers').exists())

    def test_descriptor_binding_and_paths_fail_closed(self):
        self.dependency['projects'][0]['sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'binding mismatch'):
            self.prepare()
        self.manifest['files'][0]['source'] = '../escape'
        with self.assertRaisesRegex(ValueError, 'noncanonical SDK path'):
            self.prepare()


if __name__ == '__main__':
    unittest.main()
