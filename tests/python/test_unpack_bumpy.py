import json
import io
import os
import subprocess
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from contextlib import redirect_stderr
from pathlib import Path
from unittest.mock import patch

from tools.re.mz import parse_mz
from tools.re.unpack_bumpy import atomic_write, run_checked, unpack
from tools.re.validate_unpack import (
    PRIMARY_COMMIT,
    VALIDATOR_COMMIT,
    unpack_with_unlzexe,
    validate,
)


class UnpackBumpyTest(unittest.TestCase):
    def test_bootstrap_and_docs_pin_historical_unlzexe_validator(self):
        bootstrap = Path("tools/re/bootstrap_tools.ps1").read_text(encoding="utf-8")
        readme = Path("analysis/README.md").read_text(encoding="utf-8")

        self.assertIn(VALIDATOR_COMMIT, bootstrap)
        self.assertIn("mywave82/unlzexe", bootstrap)
        self.assertIn("status --porcelain", bootstrap)
        self.assertIn(VALIDATOR_COMMIT, readme)
        self.assertIn("historical", readme.lower())
        self.assertNotIn(
            "Official LZEXE 0.91 `UPACKEXE.EXE`: independent unpack validation",
            readme,
        )

    def test_primary_unpack_is_deterministic(self):
        with tempfile.TemporaryDirectory() as directory:
            first = Path(directory) / "first.exe"
            second = Path(directory) / "second.exe"

            unpack(Path("BUMPY.EXE"), first)
            unpack(Path("BUMPY.EXE"), second)

            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertGreater(parse_mz(first.read_bytes()).file_size, 46019)
            self.assertNotEqual(first.read_bytes()[28:32], b"LZ91")
            self.assertEqual(parse_mz(first.read_bytes()).min_extra_paragraphs, 211)

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
            self.assertTrue(data["semantic_match"])
            self.assertEqual(data["differences"], [])
            self.assertEqual(data["primary"]["commit"], PRIMARY_COMMIT)
            self.assertEqual(data["validator"]["commit"], VALIDATOR_COMMIT)
            for identity in ("primary", "validator"):
                self.assertEqual(len(data[identity]["file_sha256"]), 64)
                self.assertEqual(len(data[identity]["load_image_sha256"]), 64)
                self.assertEqual(data[identity]["min_extra_paragraphs"], 211)
                self.assertEqual(data[identity]["relocation_count"], 1050)

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

    def test_parallel_unpack_invocations_are_isolated(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            outputs = [root / f"primary-{index}.exe" for index in range(4)]
            validators = [root / f"validator-{index}.exe" for index in range(4)]
            with ThreadPoolExecutor(max_workers=8) as executor:
                futures = [
                    executor.submit(unpack, Path("BUMPY.EXE"), output)
                    for output in outputs
                ] + [
                    executor.submit(unpack_with_unlzexe, Path("BUMPY.EXE"), output)
                    for output in validators
                ]
                for future in futures:
                    future.result()

            self.assertEqual(len({path.read_bytes() for path in outputs}), 1)
            self.assertEqual(len({path.read_bytes() for path in validators}), 1)

    def test_run_checked_reports_timeout_concisely(self):
        with self.assertRaisesRegex(RuntimeError, "timed out"):
            run_checked(
                ["python", "-c", "import time; time.sleep(1)"],
                purpose="test helper",
                timeout=0.01,
            )

    def test_run_checked_reports_failed_process_concisely(self):
        with self.assertRaisesRegex(RuntimeError, "test helper failed"):
            run_checked(
                ["python", "-c", "raise SystemExit(7)"],
                purpose="test helper",
                timeout=5,
            )

    def test_unpack_cli_reports_error_without_traceback(self):
        from tools.re import unpack_bumpy

        stderr = io.StringIO()
        with patch.object(unpack_bumpy, "unpack", side_effect=RuntimeError("timed out")):
            with redirect_stderr(stderr):
                result = unpack_bumpy.main()

        self.assertEqual(result, 2)
        self.assertEqual(stderr.getvalue(), "error: timed out\n")

    def test_atomic_write_retries_transient_windows_replace_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "output.bin"
            real_replace = os.replace
            attempts = 0

            def flaky_replace(source, destination):
                nonlocal attempts
                attempts += 1
                if attempts == 1:
                    raise PermissionError("transient lock")
                real_replace(source, destination)

            with patch("tools.re.unpack_bumpy.os.replace", side_effect=flaky_replace):
                atomic_write(output, b"result")

            self.assertEqual(output.read_bytes(), b"result")
            self.assertEqual(attempts, 2)


if __name__ == "__main__":
    unittest.main()
