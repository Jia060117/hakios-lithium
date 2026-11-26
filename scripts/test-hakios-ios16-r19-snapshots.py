"""Host-only snapshot contract tests; all evidence is disposable synthetic data.

Run: python -B scripts/test-hakios-ios16-r19-snapshots.py -v
"""

import hashlib
import os
from pathlib import Path
import sys
import tempfile
import unittest

# Importing the helper must not create shared scripts/__pycache__ files.
sys.dont_write_bytecode = True
import hakios_r19 as helper


# Independent fixtures: no helper allowlists, hashes, or text normalization.
TEXT_FILES = {
    "scripts/nested/example.py": b"# synthetic evidence\r\nprint('fixture')\n# no final newline",
    "reports/nested/example.md": "Evidence: \u6d4b\u8bd5 \u2713\r\n\tkeep spaces  \n".encode("utf-8"),
    "ports/synthetic/include/example.h": b"#define FIXTURE 19\r\n\r\n",
    "config/nested/empty.txt": b"",
}
REJECTIONS = (RuntimeError, ValueError)


class SnapshotEvidenceTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="hakios-r19-snapshots-")
        self.addCleanup(temporary.cleanup)
        self.sandbox = Path(temporary.name).resolve()
        self.root = self.sandbox / "root"
        self.root.mkdir()
        self.output = self.root / "artifacts" / "r19"
        self.assertTrue(self.output.is_relative_to(self.root))

    def write_source(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return hashlib.sha256(data).hexdigest()

    def populate(self):
        return {name: self.write_source(name, data) for name, data in TEXT_FILES.items()}

    def tree(self):
        """Record directories as well as bytes, without following symlinks."""
        state = {}
        for directory, directories, files in os.walk(self.sandbox, followlinks=False):
            for name in directories + files:
                path = Path(directory) / name
                key = path.relative_to(self.sandbox).as_posix()
                if path.is_symlink():
                    state[key] = ("symlink", os.readlink(path))
                elif path.is_dir():
                    state[key] = ("directory",)
                else:
                    state[key] = ("file", path.read_bytes())
        return state

    def assert_rejected_without_writes(self, evidence, output=None):
        before = self.tree()
        try:
            with self.assertRaises(REJECTIONS):
                helper.snapshot_evidence(
                    self.root, self.output if output is None else output, evidence
                )
        finally:
            self.assertEqual(self.tree(), before, "Rejected input changed files or directories")

    def make_symlink(self, link, destination, *, directory=False):
        link.parent.mkdir(parents=True, exist_ok=True)
        try:
            link.symlink_to(destination, target_is_directory=directory)
        except (OSError, NotImplementedError) as error:
            self.skipTest("Symlinks unavailable on this host: " + str(error))

    def test_preserves_exact_bytes_and_returns_root_relative_mapping(self):
        evidence = self.populate()
        original_evidence = dict(evidence)
        saved = helper.snapshot_evidence(self.root, self.output, evidence)
        self.assertEqual(set(saved), set(TEXT_FILES))
        destinations = set()
        for name, data in TEXT_FILES.items():
            with self.subTest(source=name):
                entry = saved[name]
                self.assertEqual(set(entry), {"path", "sha256"})
                self.assertIsInstance(entry["path"], str)
                relative = Path(entry["path"])
                self.assertFalse(relative.is_absolute())
                self.assertNotIn("..", relative.parts)
                target = (self.root / relative).resolve()
                self.assertTrue(target.is_relative_to(self.output.resolve()))
                self.assertNotIn(target, destinations)
                destinations.add(target)
                self.assertEqual(target.read_bytes(), data)
                self.assertEqual(entry["sha256"], hashlib.sha256(data).hexdigest())
                self.assertEqual(hashlib.sha256(target.read_bytes()).hexdigest(), evidence[name])
                self.assertEqual((self.root / name).read_bytes(), data)
        self.assertEqual(evidence, original_evidence)

    def test_identical_repeat_is_allowed(self):
        evidence = self.populate()
        first = helper.snapshot_evidence(self.root, self.output, evidence)
        before = self.tree()
        second = helper.snapshot_evidence(self.root, self.output, evidence)
        self.assertEqual(second, first)
        self.assertEqual(self.tree(), before)

    def test_changed_source_cannot_reuse_its_frozen_digest(self):
        name, original = "reports/changed.txt", b"original evidence\r\n"
        evidence = {name: self.write_source(name, original)}
        saved = helper.snapshot_evidence(self.root, self.output, evidence)
        self.write_source(name, b"changed evidence\r\n")
        self.assert_rejected_without_writes(evidence)
        self.assertEqual((self.root / saved[name]["path"]).read_bytes(), original)

    def test_wrong_initial_digest_creates_no_snapshot_directories(self):
        name = "scripts/wrong-digest.py"
        actual = self.write_source(name, b"# synthetic evidence\n")
        wrong = hashlib.sha256(b"different evidence\n").hexdigest()
        self.assertNotEqual(actual, wrong)
        self.assert_rejected_without_writes({name: wrong})
        self.assertFalse(self.output.exists())

    def test_existing_conflicting_target_is_preserved(self):
        name, data = "reports/conflict.txt", b"current source\n"
        evidence = {name: self.write_source(name, data)}
        saved = helper.snapshot_evidence(self.root, self.output, evidence)
        target = self.root / saved[name]["path"]
        target.write_bytes(b"existing conflicting snapshot\r\n")
        self.assert_rejected_without_writes(evidence)
        self.assertEqual(target.read_bytes(), b"existing conflicting snapshot\r\n")

    def test_rejects_parent_components_in_source_paths(self):
        data = b"traversal fixture\n"
        (self.sandbox / "outside.txt").write_bytes(data)
        digest = self.write_source("config/inside.txt", data)
        (self.root / "scripts").mkdir()
        for name in ("../outside.txt", "scripts/../../outside.txt",
                     "scripts/../config/inside.txt"):
            with self.subTest(source=name):
                self.assert_rejected_without_writes({name: digest})

    def test_rejects_absolute_source_even_inside_root(self):
        name = "reports/absolute.txt"
        digest = self.write_source(name, b"absolute fixture\n")
        absolute = str((self.root / name).resolve())
        self.assertTrue(Path(absolute).is_absolute())
        self.assert_rejected_without_writes({absolute: digest})

    def test_rejects_source_outside_the_four_evidence_directories(self):
        for name in ("private/example.txt", "scripts-other/example.txt", "root.txt"):
            with self.subTest(source=name):
                digest = self.write_source(name, b"unapproved source\n")
                self.assert_rejected_without_writes({name: digest})

    def test_outside_output_is_rejected_before_creating_anything(self):
        evidence = self.populate()
        # Prefix similarity must not be mistaken for containment.
        outside = self.sandbox / "root-lookalike" / "r19"
        self.assertFalse(outside.is_relative_to(self.root))
        self.assert_rejected_without_writes(evidence, output=outside)
        self.assertFalse(outside.parent.exists())

    def test_outside_output_is_rejected_even_with_empty_evidence(self):
        self.assert_rejected_without_writes({}, output=self.sandbox / "outside-empty")

    def test_output_parent_traversal_is_rejected_before_writes(self):
        evidence = self.populate()
        outside = self.root / ".." / "escaped-output"
        self.assertFalse(outside.resolve().is_relative_to(self.root))
        self.assert_rejected_without_writes(evidence, output=outside)

    def test_rejects_nul_bytes_with_an_otherwise_correct_digest(self):
        for index, data in enumerate((b"\x00", b"text\x00tail\n", b"MZ\x90\x00\x03\x00")):
            with self.subTest(data=data):
                name = "reports/nul-{}.txt".format(index)
                digest = self.write_source(name, data)
                self.assert_rejected_without_writes({name: digest})

    def test_rejects_binary_without_nul_even_with_correct_digest(self):
        data = b"\x89PNG\r\n\x1a\n\xff\xfe"
        self.assertNotIn(b"\x00", data)
        name = "reports/binary.txt"
        digest = self.write_source(name, data)
        self.assert_rejected_without_writes({name: digest})

    def test_rejects_source_file_symlink_escaping_root(self):
        data = b"external file evidence\n"
        outside = self.sandbox / "external.txt"
        outside.write_bytes(data)
        name = "scripts/linked.txt"
        self.make_symlink(self.root / name, outside)
        self.assert_rejected_without_writes({name: hashlib.sha256(data).hexdigest()})

    def test_rejects_source_directory_symlink_escaping_root(self):
        data = b"external directory evidence\n"
        outside = self.sandbox / "external-source"
        outside.mkdir()
        (outside / "evidence.txt").write_bytes(data)
        self.make_symlink(self.root / "scripts" / "linked", outside, directory=True)
        self.assert_rejected_without_writes(
            {"scripts/linked/evidence.txt": hashlib.sha256(data).hexdigest()}
        )

    def test_rejects_target_directory_symlink_escaping_output(self):
        evidence = self.populate()
        saved = helper.snapshot_evidence(self.root, self.output, evidence)
        name = "reports/nested/example.md"
        target = self.root / saved[name]["path"]
        target.unlink()
        target.parent.rmdir()
        outside = self.sandbox / "external-target"
        outside.mkdir()
        self.make_symlink(target.parent, outside, directory=True)
        self.assert_rejected_without_writes({name: evidence[name]})

    def test_rejects_output_symlink_escaping_root_before_writes(self):
        evidence = self.populate()
        outside = self.sandbox / "external-output"
        outside.mkdir()
        link = self.root / "linked-output"
        self.make_symlink(link, outside, directory=True)
        self.assert_rejected_without_writes(evidence, output=link)


if __name__ == "__main__":
    unittest.main()
