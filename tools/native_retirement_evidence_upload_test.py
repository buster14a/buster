#!/usr/bin/env python3
"""Scoped census upload policy and authenticated hidden-input replay.

The ZIP fixture models upload-artifact's hidden-file switch; extraction and
byte verification use the existing archive implementation. This is a transport
regression, not a compiler census or a live Actions upload qualification.
"""
import csv
from pathlib import Path
import re
import tempfile
import unittest
import zipfile

import native_retirement_archive as archive


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github/workflows/native-retirement-evidence.yml"
UPLOAD_PATHS = (
    "candidate/evidence/",
    "candidate/build/CMakeCache.txt",
    "candidate/build/compile_commands.json",
    "reference/build/CMakeCache.txt",
    "reference/build/compile_commands.json",
)


def upload_policy():
    text = WORKFLOW.read_text(encoding="utf-8")
    block = text.split("      - name: Retain raw evidence and build recipes\n", 1)[1]
    block = block.split("\n  strict_differential:", 1)[0]
    paths = re.search(r"(?m)^          path: \|\n((?:            [^\n]+\n)+)", block)
    if paths is None:
        raise AssertionError("missing scoped census upload paths")
    include_hidden = re.findall(r"(?m)^          include-hidden-files: (\S+)\s*$", block)
    if len(include_hidden) > 1 or (include_hidden and include_hidden[0] not in ("true", "false")):
        raise AssertionError("ambiguous census hidden-file policy")
    result = (tuple(line.strip() for line in paths.group(1).splitlines()), include_hidden == ["true"])
    return result


class CensusUploadTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        with (ROOT / "docs/native-retirement-support-v1.tsv").open(encoding="utf-8", newline="") as stream:
            matches = [row for row in csv.DictReader(stream, delimiter="\t") if row["path"] == "tests/.gitignore"]
        self.assertEqual(len(matches), 1)
        self.row = matches[0]
        self.assertEqual((self.row["role"], self.row["compile_obligation"]), ("support-file", "dependency-only"))
        self.size = int(self.row["bytes"])
        self.sha256 = self.row["sha256"]
        original = archive.checked(ROOT / self.row["path"], self.size, self.sha256).read_bytes()
        for index in range(4):
            path = self.root / f"candidate/evidence/census-integrated-{index}/inputs" / self.row["path"]
            path.parent.mkdir(parents=True)
            path.write_bytes(original)
            (path.parent / "visible-control.txt").write_bytes(b"visible evidence\n")
        for checkout in ("candidate", "reference"):
            private = self.root / checkout / ".git/objects/private-object"
            private.parent.mkdir(parents=True)
            private.write_bytes(b"checkout metadata must not be uploaded\n")
        for relative in UPLOAD_PATHS[1:]:
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"retained build recipe\n")

    def replay(self, include_hidden):
        paths, _ = upload_policy()
        self.assertEqual(paths, UPLOAD_PATHS)
        packed = self.root / "upload.zip"
        with zipfile.ZipFile(packed, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
            for relative in paths:
                source = self.root / relative
                files = sorted(source.rglob("*")) if source.is_dir() else [source]
                for path in files:
                    name = path.relative_to(self.root)
                    if path.is_file() and (include_hidden or not any(part.startswith(".") for part in name.parts)):
                        bundle.write(path, name.as_posix())
        output = self.root / "downloaded"
        archive.extract_zip(packed, output)
        self.assertFalse(any(".git" in path.relative_to(output).parts for path in output.rglob("*")))
        for relative in UPLOAD_PATHS[1:]:
            self.assertEqual((output / relative).read_bytes(), b"retained build recipe\n")
        return output

    def test_upload_enables_hidden_files_only_in_scoped_evidence(self):
        paths, include_hidden = upload_policy()
        self.assertEqual(paths, UPLOAD_PATHS)
        self.assertTrue(include_hidden, "census upload drops the authenticated tests/.gitignore input")

    def test_current_upload_policy_replays_all_four_hidden_inputs(self):
        _, include_hidden = upload_policy()
        output = self.replay(include_hidden)
        for index in range(4):
            path = output / f"candidate/evidence/census-integrated-{index}/inputs" / self.row["path"]
            self.assertEqual(archive.checked(path, self.size, self.sha256).read_bytes(), (ROOT / self.row["path"]).read_bytes())

    def test_default_hidden_filter_reproduces_all_four_historical_omissions(self):
        output = self.replay(False)
        for index in range(4):
            with self.subTest(shard=index), self.assertRaisesRegex(ValueError, "missing archive asset"):
                archive.checked(output / f"candidate/evidence/census-integrated-{index}/inputs" / self.row["path"], self.size, self.sha256)

    def test_replayed_hidden_input_corruption_still_fails_closed(self):
        output = self.replay(True)
        path = output / "candidate/evidence/census-integrated-0/inputs" / self.row["path"]
        path.write_bytes(b"x" * self.size)
        with self.assertRaisesRegex(ValueError, "digest mismatch"):
            archive.checked(path, self.size, self.sha256)


if __name__ == "__main__":
    unittest.main()
