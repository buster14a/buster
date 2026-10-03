#!/usr/bin/env python3
"""POSIX bootstrap absolute-resource and relative-path regression controls."""
import hashlib
import importlib.util
import os
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("bootstrap_absolute_fixture", ROOT / "tests/bootstrap_wrapper_test.py")
bootstrap = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bootstrap)


@unittest.skipIf(os.name == "nt", "POSIX bootstrap dependency paths")
class AbsoluteResourcePathsTests(unittest.TestCase):
    def setUp(self):
        self.wrapper = bootstrap.BootstrapWrapperTests("test_cold_then_warm_reuse_and_argument_forwarding")
        self.addCleanup(self.wrapper.doCleanups)
        self.wrapper.setUp()
        # Extend the copied fake compiler, preserving the frozen support file.
        self.wrapper.fake_tcc.write_text(bootstrap.FAKE_TCC + r"""
if "-MF" in arguments:
    extra_dependency = os.environ.get("BUSTER_FAKE_TCC_DEPENDENCY", "")
    extra_dependency = extra_dependency.replace("\\", "\\\\").replace(" ", "\\ ")
    if extra_dependency:
        dependency.write_text(dependency.read_text().rstrip("\n") + " " + extra_dependency + "\n")
""")

    def test_absolute_parent_dependency_cold_warm_and_invalidation(self):
        wrapper = self.wrapper
        toolchain_build = wrapper.root.parent / "tinycc build"
        toolchain_build.mkdir()
        resource = wrapper.root.parent / "tcc install/include/stdalign.h"
        resource.parent.mkdir(parents=True)
        resource.write_text("#define BUSTER_EXTERNAL 1\n")
        # Retain the lexical /../ component TinyCC puts in its dependencies.
        dependency = str(toolchain_build / "../tcc install/include/stdalign.h")
        wrapper.environment["BUSTER_FAKE_TCC_DEPENDENCY"] = dependency
        arguments = wrapper.success_arguments("external-header-marker")
        wrapper.assert_driver_ran(wrapper.run_wrapper(*arguments), "external-header-marker")
        self.assertEqual(wrapper.launch_count(), 2)
        wrapper.assert_driver_ran(wrapper.run_wrapper(*arguments), "external-header-marker")
        self.assertEqual(wrapper.launch_count(), 2)
        resource.write_text("#define BUSTER_EXTERNAL 2\n")
        wrapper.assert_driver_ran(wrapper.run_wrapper(*arguments), "external-header-marker")
        self.assertEqual(wrapper.launch_count(), 4)
        wrapper.assert_driver_ran(wrapper.run_wrapper(*arguments), "external-header-marker")
        self.assertEqual(wrapper.launch_count(), 4)
        resource.unlink()
        result = wrapper.run_wrapper(*arguments)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing bootstrap dependency", result.stderr)
        self.assertNotIn("external-header-marker", result.stdout)

    def test_relative_parent_dependencies_remain_refused(self):
        wrapper = self.wrapper
        external = wrapper.root.parent / "external header.h"
        external.write_text("#define BUSTER_EXTERNAL 1\n")
        dependencies = ("../external header.h", "src/../../external header.h",
                        "src/buster/lib/../lib/base.h")
        for index, dependency in enumerate(dependencies):
            with self.subTest(dependency=dependency):
                result = wrapper.run_wrapper(*wrapper.success_arguments("unsafe-path-marker"),
                                              environment={"BUSTER_FAKE_TCC_DEPENDENCY": dependency})
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("unsafe bootstrap dependency path: " + dependency, result.stderr)
                self.assertNotIn("unsafe-path-marker", result.stdout)
                self.assertEqual(wrapper.launch_count(), index + 1)
                self.assertEqual(list(wrapper.root.glob(".cache/bootstrap-driver/*/*/*.complete")), [])

    def test_relative_parent_manifest_dependency_is_not_reused(self):
        wrapper = self.wrapper
        arguments = wrapper.success_arguments("unsafe-manifest-marker")
        wrapper.assert_driver_ran(wrapper.run_wrapper(*arguments), "unsafe-manifest-marker")
        external = wrapper.root.parent / "external header.h"
        external.write_text("#define BUSTER_EXTERNAL 1\n")
        digest = hashlib.sha256(external.read_bytes()).hexdigest()
        marker = next(wrapper.root.glob(".cache/bootstrap-driver/*/*/*.complete"))
        marker.write_text(marker.read_text().replace("\nEND\n",
                          "\ndependency\t../external header.h\t" + digest + "\nEND\n"))
        result = wrapper.run_wrapper(*arguments, environment={"BUSTER_FAKE_TCC_FAIL": "23"})
        self.assertEqual(result.returncode, 23, result.stderr)
        self.assertNotIn("unsafe-manifest-marker", result.stdout)
        self.assertEqual(wrapper.launch_count(), 4)


if __name__ == "__main__":
    unittest.main()
