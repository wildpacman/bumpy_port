import json
import os
import tempfile
import unittest
from pathlib import Path

from tools.re.mz import load_image, parse_mz
from tools.re.unpack_bumpy import unpack
from tools.re.validate_unpack import (
    PRIMARY_COMMIT,
    VALIDATOR_COMMIT,
    unpack_with_unlzexe,
    validate,
)


class UnpackBumpyTest(unittest.TestCase):
    def test_primary_unpack_is_deterministic(self):
        with tempfile.TemporaryDirectory() as directory:
            first = Path(directory) / "first.exe"
            second = Path(directory) / "second.exe"

            unpack(Path("BUMPY.EXE"), first)
            unpack(Path("BUMPY.EXE"), second)

            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertGreater(parse_mz(first.read_bytes()).file_size, 46019)
            self.assertNotEqual(first.read_bytes()[28:32], b"LZ91")

    def test_primary_unpack_rejects_output_hardlinked_to_source(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "linked.exe"
            try:
                os.link("BUMPY.EXE", output)
            except OSError as error:
                self.skipTest(f"hardlinks unavailable: {error}")

            before = Path("BUMPY.EXE").read_bytes()
            with self.assertRaisesRegex(ValueError, "same file"):
                unpack(Path("BUMPY.EXE"), output)
            self.assertEqual(Path("BUMPY.EXE").read_bytes(), before)

    def test_unlzexe_unpack_is_deterministic_and_preserves_source(self):
        with tempfile.TemporaryDirectory() as directory:
            first = Path(directory) / "first.exe"
            second = Path(directory) / "second.exe"
            before = Path("BUMPY.EXE").read_bytes()

            unpack_with_unlzexe(Path("BUMPY.EXE"), first)
            unpack_with_unlzexe(Path("BUMPY.EXE"), second)

            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(Path("BUMPY.EXE").read_bytes(), before)
            self.assertNotEqual(first.read_bytes()[28:32], b"LZ91")

    def test_validation_writes_stable_report_with_both_hash_kinds(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            primary = root / "primary.exe"
            validator = root / "validator.exe"
            report = root / "report.json"
            unpack(Path("BUMPY.EXE"), primary)
            unpack_with_unlzexe(Path("BUMPY.EXE"), validator)

            validate(primary, validator, report)
            first_report = report.read_bytes()
            validate(primary, validator, report)

            self.assertEqual(report.read_bytes(), first_report)
            data = json.loads(first_report)
            self.assertTrue(data["equal"])
            self.assertEqual(data["load_image_size"], len(load_image(primary.read_bytes())))
            self.assertEqual(data["primary"]["commit"], PRIMARY_COMMIT)
            self.assertEqual(data["validator"]["commit"], VALIDATOR_COMMIT)
            for identity in ("primary", "validator"):
                self.assertEqual(len(data[identity]["file_sha256"]), 64)
                self.assertEqual(len(data[identity]["load_image_sha256"]), 64)

    def test_validation_rejects_report_that_is_an_input(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            primary = root / "primary.exe"
            validator = root / "validator.exe"
            unpack(Path("BUMPY.EXE"), primary)
            unpack_with_unlzexe(Path("BUMPY.EXE"), validator)
            before = primary.read_bytes()

            with self.assertRaisesRegex(ValueError, "same file"):
                validate(primary, validator, primary)

            self.assertEqual(primary.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
