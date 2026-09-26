#!/usr/bin/env python3
"""Exercise SDK archive binding and publication with an offline archive."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
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

    def test_darwin_availability_overlay_preprocessor_contract(self):
        # This is a hosted Clang preprocessing fixture for the shipped overlay,
        # not an oracle for Buster's feature query. The false query below is a
        # test-only compiler macro override to exercise the fallback branch;
        # the frozen SDK census must still verify Buster's real behavior.
        clang = shutil.which('clang')
        self.assertIsNotNone(clang, 'the hosted Darwin overlay test requires Clang')
        adapter = Path(__file__).resolve().with_name('native-retirement-darwin')
        self.assertTrue((adapter / 'Availability.h').is_file())

        # Only model the pinned SDK's version constants, native-attribute
        # spelling, and use-site token paste. The compatibility fallback under
        # test exists only in the actual overlay file above.
        vendor = self.root / 'darwin-vendor-fixture'
        vendor.mkdir()
        (vendor / 'Availability.h').write_text(
            '#ifndef BUSTER_TEST_VENDOR_AVAILABILITY_H\n'
            '#define BUSTER_TEST_VENDOR_AVAILABILITY_H\n'
            '#define __IPHONE_2_0 20000\n'
            '#ifndef __IPHONE_OS_VERSION_MIN_REQUIRED\n'
            '#define __IPHONE_OS_VERSION_MIN_REQUIRED 20000\n'
            '#endif\n'
            '#ifdef BUSTER_TEST_VENDOR_SENTINEL\n'
            '#define __AVAILABILITY_INTERNAL__IPHONE_2_0 BUSTER_TEST_VENDOR_SENTINEL\n'
            '#elif __has_attribute(availability)\n'
            '#define __AVAILABILITY_INTERNAL__IPHONE_2_0 __attribute__((availability(ios,introduced=2.0)))\n'
            '#endif\n'
            '#define __OSX_AVAILABLE_STARTING(_osx, _ios) __AVAILABILITY_INTERNAL##_ios\n'
            '#endif\n'
        )

        source = (
            '#include <Availability.h>\n'
            '#if __has_attribute(availability) != BUSTER_TEST_EXPECT_QUERY\n'
            '#error availability query did not match this fixture\n'
            '#endif\n'
            '#if BUSTER_TEST_EXPECT_IPHONE_2\n'
            '#ifndef __AVAILABILITY_INTERNAL__IPHONE_2_0\n'
            '#error iOS 2.0 annotation missing\n'
            '#endif\n'
            '#else\n'
            '#ifdef __AVAILABILITY_INTERNAL__IPHONE_2_0\n'
            '#error unsupported configuration acquired an iOS 2.0 annotation\n'
            '#endif\n'
            '#endif\n'
            '#ifdef __AVAILABILITY_INTERNAL__IPHONE_3_0\n'
            '#error later iOS annotation must remain undefined\n'
            '#endif\n'
            '#ifdef BUSTER_TEST_EXPECT_VENDOR_VALUE\n'
            '#if __AVAILABILITY_INTERNAL__IPHONE_2_0 != BUSTER_TEST_EXPECT_VENDOR_VALUE\n'
            '#error existing vendor annotation was overwritten\n'
            '#endif\n'
            '#endif\n'
            '__OSX_AVAILABLE_STARTING(__MAC_10_4, __IPHONE_2_0) int ios_available_api(void);\n'
        )

        def preprocess(target, definitions, include_vendor=True):
            command = [clang, '-E', '-P', '-x', 'c', '-nostdinc', '-target', target,
                       '-I', str(adapter)]
            if include_vendor:
                command.extend(['-I', str(vendor)])
            command.extend(['-Wno-builtin-macro-redefined', *definitions, '-'])
            return subprocess.run(command, input=source, text=True, capture_output=True,
                                   timeout=30, check=False)

        missing_vendor = preprocess('x86_64-apple-ios', [], include_vendor=False)
        self.assertNotEqual(missing_vendor.returncode, 0, missing_vendor.stdout)
        self.assertIn('Availability.h', missing_vendor.stderr)

        unsupported_query = '-D__has_attribute(x)=0'
        fallback_cases = [
            ('x86_64-ios', 'x86_64-apple-ios'),
            ('aarch64-ios', 'aarch64-apple-ios'),
        ]
        for name, target in fallback_cases:
            with self.subTest(case=name):
                output = preprocess(target, [
                    '-D__BUSTER__=1', unsupported_query,
                    '-DBUSTER_TEST_EXPECT_QUERY=0', '-DBUSTER_TEST_EXPECT_IPHONE_2=1',
                    '-D__IPHONE_OS_VERSION_MIN_REQUIRED=20000',
                ])
                self.assertEqual(output.returncode, 0, output.stderr)
                self.assertIn('int ios_available_api(void);', output.stdout)
                self.assertNotIn('__AVAILABILITY_INTERNAL__IPHONE_2_0', output.stdout)

        controls = [
            ('non-ios', 'x86_64-unknown-linux-gnu', ['-D__BUSTER__=1', unsupported_query,
             '-DBUSTER_TEST_EXPECT_QUERY=0', '-DBUSTER_TEST_EXPECT_IPHONE_2=0',
             '-D__IPHONE_OS_VERSION_MIN_REQUIRED=20000']),
            ('minimum-below-2.0', 'x86_64-apple-ios', ['-D__BUSTER__=1', unsupported_query,
             '-DBUSTER_TEST_EXPECT_QUERY=0', '-DBUSTER_TEST_EXPECT_IPHONE_2=0',
             '-D__IPHONE_OS_VERSION_MIN_REQUIRED=10000']),
            ('no-buster', 'x86_64-apple-ios', [unsupported_query,
             '-DBUSTER_TEST_EXPECT_QUERY=0', '-DBUSTER_TEST_EXPECT_IPHONE_2=0',
             '-D__IPHONE_OS_VERSION_MIN_REQUIRED=20000']),
        ]
        for name, target, definitions in controls:
            with self.subTest(case=name):
                output = preprocess(target, definitions)
                self.assertEqual(output.returncode, 0, output.stderr)
                self.assertIn('__AVAILABILITY_INTERNAL__IPHONE_2_0 int ios_available_api(void);', output.stdout)

        native = preprocess('x86_64-apple-ios', [
            '-D__BUSTER__=1', '-DBUSTER_TEST_EXPECT_QUERY=1', '-DBUSTER_TEST_EXPECT_IPHONE_2=1',
            '-D__IPHONE_OS_VERSION_MIN_REQUIRED=20000',
        ])
        self.assertEqual(native.returncode, 0, native.stderr)
        self.assertIn('__attribute__((availability(ios,introduced=2.0)))', native.stdout)

        preserved = preprocess('x86_64-apple-ios', [
            '-D__BUSTER__=1', unsupported_query, '-DBUSTER_TEST_EXPECT_QUERY=0',
            '-DBUSTER_TEST_EXPECT_IPHONE_2=1', '-DBUSTER_TEST_VENDOR_SENTINEL=73',
            '-DBUSTER_TEST_EXPECT_VENDOR_VALUE=73', '-D__IPHONE_OS_VERSION_MIN_REQUIRED=20000',
        ])
        self.assertEqual(preserved.returncode, 0, preserved.stderr)
        self.assertIn('73 int ios_available_api(void);', preserved.stdout)


if __name__ == '__main__':
    unittest.main()
