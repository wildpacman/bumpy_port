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


def _unpacker_available() -> bool:
    """The live LZEXE unpacker needs the pinned vendored checkout, which a fresh
    clone does not have. Tests that drive it skip when it is absent."""
    try:
        from tools.re.unpack_bumpy import _primary_source

        _primary_source(Path(__file__).resolve().parents[2])
        return True
    except Exception:
        return False


UNPACKER_AVAILABLE = _unpacker_available()
NEEDS_UNPACKER = unittest.skipUnless(
    UNPACKER_AVAILABLE, "vendored unpacklzexe is not set up (run tools/re/bootstrap_tools.ps1)"
)


class UnpackBumpyTest(unittest.TestCase):
    @NEEDS_UNPACKER
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

    @NEEDS_UNPACKER
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

    @NEEDS_UNPACKER
    def test_parallel_unpack_invocations_are_isolated(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            outputs = [root / f"primary-{index}.exe" for index in range(4)]
            with ThreadPoolExecutor(max_workers=4) as executor:
                futures = [
                    executor.submit(unpack, Path("BUMPY.EXE"), output)
                    for output in outputs
                ]
                for future in futures:
                    future.result()

            self.assertEqual(len({path.read_bytes() for path in outputs}), 1)

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
