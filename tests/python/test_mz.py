import dataclasses
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.re.mz import MzHeader, load_image, parse_mz


def mz_file(
    *,
    signature=0x5A4D,
    bytes_in_last_page=64,
    page_count=1,
    header_paragraphs=2,
    payload=b"A" * 32,
):
    header = struct.pack(
        "<14H",
        signature,
        bytes_in_last_page,
        page_count,
        0,
        header_paragraphs,
        0,
        0xFFFF,
        0,
        0,
        0,
        0,
        0,
        28,
        0,
    )
    return header + b"\x00" * 4 + payload


class MzHeaderTest(unittest.TestCase):
    def test_parses_packed_bumpy_header(self):
        header = parse_mz(Path("BUMPY.EXE").read_bytes())

        self.assertEqual(header.signature, 0x5A4D)
        self.assertEqual(header.file_size, 46019)
        self.assertEqual(header.header_size, 32)
        self.assertEqual(header.initial_cs, 2783)
        self.assertEqual(header.initial_ip, 14)
        self.assertEqual(header.relocation_count, 0)

    def test_header_is_immutable_and_serializes_computed_sizes(self):
        header = parse_mz(mz_file())

        with self.assertRaises(dataclasses.FrozenInstanceError):
            header.signature = 0
        self.assertIsInstance(header, MzHeader)
        self.assertEqual(header.file_size, 64)
        self.assertEqual(header.header_size, 32)
        self.assertEqual(
            header.to_dict(),
            {
                "signature": 0x5A4D,
                "bytes_in_last_page": 64,
                "page_count": 1,
                "relocation_count": 0,
                "header_paragraphs": 2,
                "min_extra_paragraphs": 0,
                "max_extra_paragraphs": 0xFFFF,
                "initial_ss": 0,
                "initial_sp": 0,
                "checksum": 0,
                "initial_ip": 0,
                "initial_cs": 0,
                "relocation_table_offset": 28,
                "overlay": 0,
                "file_size": 64,
                "header_size": 32,
            },
        )

    def test_zero_last_page_byte_count_means_full_page(self):
        data = mz_file(bytes_in_last_page=0, page_count=1) + b"\x00" * 448

        self.assertEqual(parse_mz(data).file_size, 512)

    def test_rejects_short_header(self):
        with self.assertRaisesRegex(ValueError, "shorter than 28 bytes"):
            parse_mz(b"MZ")

    def test_rejects_wrong_signature(self):
        with self.assertRaisesRegex(ValueError, "missing MZ signature"):
            parse_mz(mz_file(signature=0x1234))

    def test_rejects_declared_size_beyond_actual_data(self):
        with self.assertRaisesRegex(ValueError, "declared MZ size exceeds file size"):
            parse_mz(mz_file(bytes_in_last_page=65))

    def test_rejects_header_size_beyond_declared_file(self):
        with self.assertRaisesRegex(ValueError, "MZ header exceeds declared file size"):
            parse_mz(mz_file(header_paragraphs=5))

    def test_rejects_zero_page_count(self):
        with self.assertRaisesRegex(ValueError, "page count must be positive"):
            parse_mz(mz_file(page_count=0))

    def test_rejects_impossible_last_page_byte_count(self):
        with self.assertRaisesRegex(ValueError, "last-page byte count exceeds 511"):
            parse_mz(mz_file(bytes_in_last_page=512))

    def test_rejects_header_size_shorter_than_fixed_header(self):
        with self.assertRaisesRegex(ValueError, "MZ header size is shorter than 28 bytes"):
            parse_mz(mz_file(header_paragraphs=1))

    def test_load_image_excludes_header_and_trailing_bytes(self):
        data = mz_file(payload=b"B" * 32) + b"TRAILING"

        self.assertEqual(load_image(data), b"B" * 32)

    def test_writes_stable_sorted_ascii_report_with_file_name(self):
        from tools.re.report_mz import write_report

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "SAMPLE.EXE"
            output = root / "report.json"
            source.write_bytes(mz_file())

            write_report(source, output)
            first = output.read_bytes()
            write_report(source, output)

            expected = {
                "file": "SAMPLE.EXE",
                **parse_mz(source.read_bytes()).to_dict(),
            }
            self.assertEqual(
                first,
                (json.dumps(expected, indent=2, sort_keys=True) + "\n").encode("ascii"),
            )
            self.assertEqual(output.read_bytes(), first)

    def test_report_script_runs_directly_from_repository_root(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "report.json"

            result = subprocess.run(
                [
                    sys.executable,
                    "tools/re/report_mz.py",
                    "BUMPY.EXE",
                    str(output),
                ],
                cwd=Path.cwd(),
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                json.loads(output.read_text(encoding="ascii"))["file"],
                "BUMPY.EXE",
            )


if __name__ == "__main__":
    unittest.main()
