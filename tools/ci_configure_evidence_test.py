#!/usr/bin/env python3
"""Isolated regression tests for configure-evidence retention (no toolchain)."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import ci_configure_evidence as evidence


class ConfigureEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.build.mkdir()
        self.output = self.root / "artifacts" / "configure"
        self.tree = "build-ci_on-cc_zig-sanitize_off-fuzz_available_off-configs_Debug"

    def write(self, relative, data=b"diagnostic\n"):
        path = self.build / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def collect(self, **kwargs):
        return evidence.collect(self.build, self.output, environment={"GITHUB_RUN_ID": "17", "SECRET": "not-copied"}, **kwargs)

    def link(self, target, destination):
        try:
            destination.symlink_to(target, target_is_directory=target.is_dir())
        except OSError as error:
            self.skipTest(f"host does not permit symlinks: {error}")

    def test_raw_bytes_hashes_and_allowlisted_identity(self):
        data = b"\xff\xfeC\x00a\x00c\x00h\x00e\x00\r\x00\n\x00"
        self.write(f"{self.tree}/CMakeCache.txt", data)
        self.write(f"{self.tree}/CMakeFiles/CMakeConfigureLog.yaml")
        self.write(f"{self.tree}/cmake-profile.json", b"[]")
        result = self.collect(profile_requested=True)
        captured = [item for item in result["files"] if item["status"] == "captured"]
        cache = next(item for item in captured if item["path"].endswith("CMakeCache.txt"))
        self.assertEqual(cache["sha256"], hashlib.sha256(data).hexdigest())
        self.assertEqual(cache["retained_path"], cache["path"])
        self.assertEqual((self.output / cache["retained_path"]).read_bytes(), data)
        self.assertEqual(result["identity"], {"GITHUB_RUN_ID": "17"})
        self.assertEqual(result["configure_logs_captured"], 1)
        self.assertEqual(result["profiles_captured"], 1)
        self.assertTrue(result["profile_requested"])
        self.assertEqual(json.loads((self.output / "manifest.json").read_text()), result)

    def test_captures_ninja_log_and_writes_tree_summary(self):
        data = b"# ninja log v5\n0\t10\t0\tobj.o\thash\n"
        self.write(f"{self.tree}/.ninja_log", data)
        result = self.collect()
        record = next(item for item in result["files"] if item["path"].endswith("/.ninja_log"))
        self.assertEqual(record["status"], "captured")
        self.assertEqual(record["path"], f"{self.tree}/.ninja_log")
        self.assertEqual(record["retained_path"], f"{self.tree}/ninja_log.txt")
        self.assertEqual(record["sha256"], hashlib.sha256(data).hexdigest())
        self.assertEqual((self.output / record["retained_path"]).read_bytes(), data)
        self.assertFalse((self.output / self.tree / ".ninja_log").exists())
        summary = {"tree": self.tree, "version": 5, "edges": 1, "malformed_lines": 0,
                   "edge_duration_sum_ms": 10,
                   "top": [{"output": "obj.o", "outputs": ["obj.o"], "start_ms": 0, "end_ms": 10,
                            "duration_ms": 10}],
                   "all_edges": [{"output": "obj.o", "outputs": ["obj.o"], "start_ms": 0, "end_ms": 10,
                                  "duration_ms": 10}]}
        self.assertEqual(result["ninja_logs_captured"], 1)
        self.assertEqual(result["ninja_log_summaries"], [summary])
        self.assertEqual(json.loads((self.output / self.tree / "ninja-log-summary.json").read_text()),
                         {key: value for key, value in summary.items() if key != "tree"})
        self.assertEqual(json.loads((self.output / "manifest.json").read_text()), result)

    def test_artifact_paths_are_nonhidden_and_hash_match_manifest(self):
        for name in evidence.FILES:
            data = b"# ninja log v7\n" if name == ".ninja_log" else b"diagnostic\n"
            self.write(f"{self.tree}/{name}", data)
        result = self.collect()
        self.assertEqual(result["errors"], [])
        captured = [item for item in result["files"] if item["status"] == "captured"]
        self.assertEqual(len(captured), len(evidence.FILES))
        for path in self.output.rglob("*"):
            relative = path.relative_to(self.output)
            self.assertFalse(any(part.startswith(".") for part in relative.parts), str(relative))
        for record in captured:
            retained = self.output / record["retained_path"]
            self.assertTrue(retained.is_file(), record["retained_path"])
            self.assertEqual(hashlib.sha256(retained.read_bytes()).hexdigest(), record["sha256"])

    def test_summarize_ninja_v7_across_invocations_uses_per_edge_durations(self):
        data = (
            b"# ninja log v7\n"
            b"0\t40\t1700000000\ta.c.obj\thash-a\n"
            b"0\t55\t1700000001\tb.c.obj\thash-b\n"
            b"55\t70\t1700000002\tide.exe\thash-link\n"
            b"0\t202\t1700000003\tCMakeFiles/test_all\thash-test-all\n"
        )
        result = evidence.summarize_ninja_log(data)
        self.assertEqual(result["version"], 7)
        self.assertEqual(result["edges"], 4)
        self.assertEqual(result["malformed_lines"], 0)
        self.assertEqual(result["edge_duration_sum_ms"], 40 + 55 + 15 + 202)
        self.assertNotIn("wall_ms", result)
        self.assertNotIn("tail", result)
        self.assertEqual([edge["output"] for edge in result["top"]],
                         ["CMakeFiles/test_all", "b.c.obj", "a.c.obj", "ide.exe"])
        self.assertEqual([edge["output"] for edge in result["all_edges"]],
                         ["CMakeFiles/test_all", "b.c.obj", "a.c.obj", "ide.exe"])
        self.assertEqual(len({edge["output"] for edge in result["all_edges"]}), 4)
        link = next(edge for edge in result["all_edges"] if edge["output"] == "ide.exe")
        self.assertEqual(link["duration_ms"], 15)

    def test_only_build_directory_aliases_are_merged(self):
        build_directory = "/x/build/t"
        lines = ["# ninja log v7"]
        for index in range(1, 4):
            relative = f"CMakeFiles/edge{index}"
            absolute = f"{build_directory}/{relative}"
            lines.extend((
                f"1\t16\t8134371059249091\t{relative}\t35ac50d1984a72a1",
                f"1\t16\t8134371059249091\t{absolute}\t35ac50d1984a72a1",
            ))
        data = ("\n".join(lines) + "\n").encode()

        result = evidence.summarize_ninja_log(data, build_directory)
        self.assertEqual(result["edges"], 3)
        self.assertEqual(result["edge_duration_sum_ms"], 3 * 15)
        for index, edge in enumerate(result["all_edges"], 1):
            relative = f"CMakeFiles/edge{index}"
            absolute = f"{build_directory}/{relative}"
            self.assertEqual(edge, {
                "output": relative,
                "outputs": sorted([relative, absolute]),
                "start_ms": 1,
                "end_ms": 16,
                "duration_ms": 15,
            })

        without_build_directory = evidence.summarize_ninja_log(data)
        self.assertEqual(without_build_directory["edges"], 6)
        self.assertEqual(without_build_directory["edge_duration_sum_ms"], 6 * 15)
        self.assertTrue(all(len(edge["outputs"]) == 1
                            for edge in without_build_directory["all_edges"]))

    def test_absolute_output_under_another_directory_is_not_merged(self):
        build_directory = "/x/build/t"
        data = (
            b"# ninja log v7\n"
            b"1\t16\t8134371059249091\tCMakeFiles/edge1\t35ac50d1984a72a1\n"
            b"1\t16\t8134371059249091\t/other/CMakeFiles/edge1\t35ac50d1984a72a1\n"
        )
        result = evidence.summarize_ninja_log(data, build_directory)
        self.assertEqual(result["edges"], 2)
        self.assertEqual(result["edge_duration_sum_ms"], 2 * 15)
        self.assertTrue(all(len(edge["outputs"]) == 1 for edge in result["all_edges"]))

    @unittest.skipUnless(os.name == "nt", "Windows path normalization")
    def test_windows_style_absolute_alias_is_merged(self):
        build_directory = r"D:\a\b\build\t"
        relative = "CMakeFiles/test_all-Release"
        absolute = "D:/A/b/build/t/CMakeFiles/test_all-Release"
        data = (
            b"# ninja log v7\n"
            + f"1\t16\t8134371059249091\t{relative}\t35ac50d1984a72a1\n".encode()
            + f"1\t16\t8134371059249091\t{absolute}\t35ac50d1984a72a1\n".encode()
        )
        result = evidence.summarize_ninja_log(data, build_directory)
        self.assertEqual(result["edges"], 1)
        self.assertEqual(result["all_edges"], [{
            "output": relative,
            "outputs": sorted([relative, absolute]),
            "start_ms": 1,
            "end_ms": 16,
            "duration_ms": 15,
        }])

    @unittest.skipUnless(os.name == "posix", "POSIX path normalization")
    def test_posix_absolute_alias_uses_exact_build_directory(self):
        build_directory = "/x/build/t"
        relative = "CMakeFiles/test_all-Release"
        absolute = f"{build_directory}/{relative}"
        data = (
            b"# ninja log v7\n"
            + f"1\t16\t8134371059249091\t{relative}\t35ac50d1984a72a1\n".encode()
            + f"1\t16\t8134371059249091\t{absolute}\t35ac50d1984a72a1\n".encode()
        )
        result = evidence.summarize_ninja_log(data, build_directory)
        self.assertEqual(result["edges"], 1)
        self.assertEqual(result["all_edges"], [{
            "output": relative,
            "outputs": sorted([relative, absolute]),
            "start_ms": 1,
            "end_ms": 16,
            "duration_ms": 15,
        }])

    def test_hashless_ninja_lines_remain_per_output(self):
        data = (
            b"# ninja log v7\n"
            b"0\t10\t8134371059249091\ta.txt\n"
            b"0\t10\t8134371059249091\tb.txt\n"
        )
        result = evidence.summarize_ninja_log(data)
        self.assertEqual(result["edges"], 2)
        self.assertEqual(result["edge_duration_sum_ms"], 20)
        self.assertEqual(result["all_edges"], [
            {"output": "a.txt", "outputs": ["a.txt"], "start_ms": 0, "end_ms": 10, "duration_ms": 10},
            {"output": "b.txt", "outputs": ["b.txt"], "start_ms": 0, "end_ms": 10, "duration_ms": 10},
        ])

    def test_duplicate_outputs_keep_last_invocation_edge(self):
        data = (
            b"# ninja log v7\n"
            b"0\t40\t1700000000\ta.c.obj\thash-first\n"
            b"0\t7\t1700000001\ta.c.obj\thash-last\n"
            b"not a ninja edge\n"
        )
        result = evidence.summarize_ninja_log(data)
        self.assertEqual(result["edges"], 1)
        self.assertEqual(result["malformed_lines"], 1)
        self.assertEqual(result["edge_duration_sum_ms"], 7)
        self.assertEqual(result["all_edges"],
                         [{"output": "a.c.obj", "outputs": ["a.c.obj"],
                           "start_ms": 0, "end_ms": 7, "duration_ms": 7}])

    def test_summarize_empty_and_headers_only_ninja_logs(self):
        for data in (b"", b"# ninja log v6\n\n"):
            with self.subTest(data=data):
                result = evidence.summarize_ninja_log(data)
                self.assertEqual(result["edges"], 0)
                self.assertEqual(result["edge_duration_sum_ms"], 0)
                self.assertEqual(result["top"], [])
                self.assertEqual(result["all_edges"], [])
                self.assertNotIn("wall_ms", result)
                self.assertNotIn("tail", result)
        self.assertIsNone(evidence.summarize_ninja_log(b"")["version"])
        self.assertEqual(evidence.summarize_ninja_log(b"# ninja log v6\n")["version"], 6)

    def test_missing_ninja_log_is_not_an_error_or_summary(self):
        self.write(f"{self.tree}/CMakeCache.txt")
        result = self.collect()
        record = next(item for item in result["files"] if item["path"].endswith("/.ninja_log"))
        self.assertEqual(record["status"], "missing")
        self.assertNotIn("retained_path", record)
        self.assertEqual(result["errors"], [])
        self.assertEqual(result["ninja_logs_captured"], 0)
        self.assertEqual(result["ninja_log_summaries"], [])

    def test_cli_prints_ninja_log_breakdown(self):
        build_directory = (self.build / self.tree).resolve().as_posix()
        absolute_output = f"{build_directory}/compile.obj"
        self.write(f"{self.tree}/.ninja_log",
                   b"# ninja log v5\n"
                   b"0\t1500\t0\tcompile.obj\thash\n"
                   + f"0\t1500\t0\t{absolute_output}\thash\n".encode()
                   + b"0\t500\t0\tlink.exe\tlink-hash\n")
        relative_build_root = os.path.relpath(self.build, self.root)
        run = subprocess.run([sys.executable, evidence.__file__, "--build-root", relative_build_root,
                              "--output", str(self.output)],
                             capture_output=True, text=True, check=False, cwd=self.root)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertTrue(run.stdout.startswith("CMake evidence: 1 trees, 0 configure logs, "))
        self.assertIn(f"NINJA_LOG_SUMMARY tree={self.tree} edges=2 "
                      "edge_duration_sum_seconds=2.000 malformed=0", run.stdout)
        self.assertIn(f"NINJA_LOG_EDGE tree={self.tree} rank=1 "
                      "seconds=1.500 output=compile.obj outputs=2", run.stdout)
        self.assertIn(f"NINJA_LOG_EDGE tree={self.tree} rank=2 "
                      "seconds=0.500 output=link.exe", run.stdout)
        self.assertNotIn("outputs=1", run.stdout)
        self.assertNotIn("NINJA_LOG_TAIL", run.stdout)
        self.assertNotIn("wall_seconds=", run.stdout)
        self.assertNotIn("start_seconds=", run.stdout)
        self.assertNotIn("end_seconds=", run.stdout)

    def test_does_not_capture_objects_unrelated_trees_or_environment(self):
        self.write(f"{self.tree}/subject.o")
        self.write("unrelated/CMakeCache.txt")
        result = self.collect()
        self.assertEqual(result["captured_bytes"], 0)
        self.assertEqual(result["tree_count"], 1)
        self.assertFalse(result["profile_requested"])
        self.assertFalse(any(path.name == "subject.o" for path in self.output.rglob("*")))

    def test_sharded_tree_names_and_superbuild_exclusion(self):
        release = "build-release-ci_on-cc_clang-sanitize_off-fuzz_available_off-configs_Release"
        checks = "build-checks-ci_on-cc_zig-sanitize_off-fuzz_available_off-configs_Debug"
        for tree in (release, checks):
            self.write(f"{tree}/CMakeCache.txt", tree.encode("ascii"))
            self.write(f"{tree}/CMakeFiles/CMakeConfigureLog.yaml")
        self.write("build-checks-superbuild-ci_on/CMakeCache.txt", b"not a compiler tree")
        result = self.collect()
        paths = [item["path"] for item in result["files"]]
        self.assertEqual(result["errors"], [])
        self.assertEqual(result["tree_count"], 2)
        self.assertEqual(result["configure_logs_captured"], 2)
        self.assertTrue(any(path.startswith(f"{release}/") for path in paths))
        self.assertTrue(any(path.startswith(f"{checks}/") for path in paths))
        self.assertFalse(any("superbuild" in path for path in paths))

    def test_failed_configure_missing_files_remain_missing(self):
        self.write(f"{self.tree}/CMakeFiles/CMakeConfigureLog.yaml", b"failed probe")
        result = self.collect(profile_requested=True)
        self.assertEqual(result["errors"], [])
        self.assertEqual(result["profiles_captured"], 0)
        self.assertEqual(next(item for item in result["files"] if item["path"].endswith("cmake-profile.json"))["status"], "missing")
        self.assertNotIn("passed", json.dumps(result))

    def test_each_split_shard_cli_retains_its_configure_evidence(self):
        trees = (
            "build-sanitized-debug-ci_on-cc_clang-sanitize_on-fuzz_available_on-configs_Debug",
            "build-sanitized-release-ci_on-cc_clang-sanitize_on-fuzz_available_on-configs_Release",
            "build-portability-ci_on-cc_gcc-sanitize_off-fuzz_available_off-configs_Debug",
            "build-portability-ci_on-cc_zig-sanitize_off-fuzz_available_off-configs_Debug",
            "build-portability-ci_on-cc_msvc-sanitize_off-fuzz_available_off-configs_Debug",
        )
        for tree in trees:
            with self.subTest(tree=tree), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                build = root / "build"
                output = root / "configure"
                cache = build / tree / "CMakeCache.txt"
                cache.parent.mkdir(parents=True)
                data = (tree + "\r\n").encode("ascii")
                cache.write_bytes(data)
                log = cache.parent / "CMakeFiles" / "CMakeConfigureLog.yaml"
                log.parent.mkdir()
                log.write_bytes(b"configure diagnostic\n")
                owner = tree.split("-ci_on-cc_", 1)[0]
                superbuild = build / f"{owner}-superbuild-ci_on"
                superbuild.mkdir()
                (superbuild / "CMakeCache.txt").write_bytes(b"not a compiler tree")
                run = subprocess.run([sys.executable, evidence.__file__, "--build-root", str(build), "--output", str(output)],
                                     capture_output=True, text=True, check=False)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                result = json.loads((output / "manifest.json").read_text())
                self.assertEqual(result["errors"], [])
                self.assertEqual(result["tree_count"], 1)
                self.assertEqual(result["configure_logs_captured"], 1)
                self.assertEqual(result["profiles_captured"], 0)
                record = next(item for item in result["files"] if item["path"].endswith("CMakeCache.txt"))
                self.assertEqual(record["sha256"], hashlib.sha256(data).hexdigest())
                self.assertEqual(record["retained_path"], record["path"])
                self.assertEqual((output / record["retained_path"]).read_bytes(), data)
                self.assertFalse(any("superbuild" in item["path"] for item in result["files"]))

    def test_split_superbuilds_alone_are_not_empty_success(self):
        for owner in ("sanitized-debug", "sanitized-release", "portability"):
            self.write(f"build-{owner}-superbuild-ci_on/CMakeCache.txt")
        result = self.collect()
        self.assertEqual(result["tree_count"], 0)
        self.assertEqual(result["errors"], ["no matrix configure trees found"])

    def test_missing_build_root_is_not_empty_success(self):
        self.build.rmdir()
        result = self.collect()
        self.assertTrue(result["errors"])
        self.assertEqual(result["tree_count"], 0)

    def test_empty_build_root_is_not_empty_success(self):
        result = self.collect()
        self.assertTrue(result["errors"])
        self.assertEqual(result["tree_count"], 0)

    def test_linked_build_root_refused(self):
        actual = self.root / "actual-build"
        actual.mkdir()
        self.build.rmdir()
        self.link(actual, self.build)
        with self.assertRaisesRegex(ValueError, "linked"):
            self.collect()

    def test_windows_root_reparse_point_refused(self):
        with mock.patch.object(Path, "lstat", return_value=mock.Mock(st_mode=0o40755, st_file_attributes=0x400)):
            with self.assertRaisesRegex(ValueError, "linked"):
                self.collect()

    def test_rejects_existing_output(self):
        self.output.mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.collect()

    def test_rejects_output_within_build_tree(self):
        with self.assertRaisesRegex(ValueError, "outside"):
            evidence.collect(self.build, self.build / "evidence")

    def test_file_limit_is_bounded_and_reported(self):
        self.write(f"{self.tree}/CMakeCache.txt", b"12345")
        result = self.collect(file_limit=4)
        self.assertTrue(result["errors"])
        self.assertEqual(result["captured_bytes"], 0)
        record = next(item for item in result["files"] if item["path"].endswith("CMakeCache.txt"))
        self.assertEqual(record["status"], "error")
        self.assertNotIn("retained_path", record)

    def test_total_limit_and_exact_boundary(self):
        self.write(f"{self.tree}/CMakeCache.txt", b"1234")
        self.write(f"{self.tree}/CMakeFiles/CMakeConfigureLog.yaml", b"1234")
        result = self.collect(file_limit=4, total_limit=4)
        self.assertEqual(result["captured_bytes"], 4)
        self.assertTrue(result["errors"])

    def test_file_symlink_refused(self):
        private = self.root / "private"
        private.write_text("must not be copied")
        path = self.write(f"{self.tree}/CMakeCache.txt")
        path.unlink()
        self.link(private, path)
        result = self.collect()
        self.assertTrue(result["errors"])
        self.assertEqual(result["captured_bytes"], 0)

    def test_directory_symlink_refused(self):
        private = self.root / "private"
        private.mkdir()
        (private / "CMakeConfigureLog.yaml").write_text("must not be copied")
        tree = self.build / self.tree
        tree.mkdir()
        self.link(private, tree / "CMakeFiles")
        result = self.collect()
        self.assertTrue(result["errors"])
        self.assertEqual(result["captured_bytes"], 0)

    def test_tree_symlink_refused(self):
        private = self.root / "private"
        private.mkdir()
        (private / "CMakeCache.txt").write_text("must not be copied")
        self.link(private, self.build / self.tree)
        result = self.collect()
        self.assertTrue(result["errors"])
        self.assertEqual(result["captured_bytes"], 0)

    def test_windows_reparse_point_refused(self):
        with mock.patch.object(Path, "lstat", return_value=mock.Mock(st_mode=0o100644, st_file_attributes=0x400)):
            with self.assertRaisesRegex(ValueError, "reparse"):
                evidence.regular_path(self.build, "CMakeCache.txt")

    def test_nonregular_file_and_traversal_refused(self):
        (self.build / "directory").mkdir()
        for relative in ("directory", "../private", str(self.root / "private")):
            with self.assertRaises(ValueError):
                evidence.regular_path(self.build, relative)

    def test_root_entry_bound(self):
        self.write("a")
        self.write("b")
        with mock.patch.object(evidence, "MAX_ROOT_ENTRIES", 1):
            result = self.collect()
        self.assertTrue(result["errors"])

    def test_tree_bound(self):
        self.write(f"{self.tree}/CMakeCache.txt")
        self.write("build-ci_on-cc_clang-other/CMakeCache.txt")
        with mock.patch.object(evidence, "MAX_TREES", 1):
            result = self.collect()
        self.assertTrue(result["errors"])
        self.assertEqual(result["captured_bytes"], 0)

    def test_cli_propagates_collection_failure(self):
        self.build.rmdir()
        run = subprocess.run([sys.executable, evidence.__file__, "--build-root", str(self.build), "--output", str(self.output)],
                             capture_output=True, text=True, check=False)
        self.assertEqual(run.returncode, 1)
        self.assertTrue((self.output / "manifest.json").is_file())


if __name__ == "__main__":
    unittest.main()
