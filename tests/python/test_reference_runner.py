from __future__ import annotations

import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class ReferenceRunnerTest(unittest.TestCase):
    def test_reference_configuration_is_fixed(self) -> None:
        config = (ROOT / "reference/dosbox-x.conf").read_text(encoding="ascii")

        for setting in (
            "machine=vgaonly",
            "memsize=16",
            "core=normal",
            "cputype=386",
            "cycles=fixed 3000",
            "aspect=false",
            "scaler=none",
            "rate=44100",
            "blocksize=1024",
            "prebuffer=25",
            "mididevice=none",
            "mount c .",
        ):
            self.assertIn(setting, config)

    def test_automated_menu_probe_finishes_and_preserves_originals(self) -> None:
        result = subprocess.run(
            [
                "pwsh",
                "-NoProfile",
                "-File",
                str(ROOT / "tools/reference/run_reference.ps1"),
                "-VerifyMenu",
                "-StartupTimeoutSeconds",
                "20",
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=45,
            check=False,
        )

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("DOSBox-X version verified: 2026.06.02", result.stdout)
        self.assertIn("Original asset verification passed before launch", result.stdout)
        self.assertIn("Original asset verification passed after launch", result.stdout)
        self.assertIn("Reference menu probe passed:", result.stdout)


if __name__ == "__main__":
    unittest.main()
