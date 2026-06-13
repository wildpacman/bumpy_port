import dataclasses
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.re.mz import MzHeader, load_image, parse_mz, relocation_entries


def mz_file(
    *,
    signature=0x5A4D,
    bytes_in_last_page=64,
    page_count=1,
    header_paragraphs=2,
    relocation_count=0,
    relocation_table_offset=28,
    payload=b"A" * 32,
):
    header = struct.pack(
        "<14H",
        signature,
        bytes_in_last_page,
        page_count,
        relocation_count,
        header_paragraphs,
        0,
        0xFFFF,
        0,
        0,
        0,
        0,
        0,
        relocation_table_offset,
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

    def test_accepts_relocation_table_ending_at_header_boundary(self):
        header = parse_mz(
            mz_file(relocation_count=1, relocation_table_offset=28),
        )

        self.assertEqual(header.relocation_count, 1)

    def test_rejects_relocation_table_before_fixed_header(self):
        with self.assertRaisesRegex(
            ValueError,
            "MZ relocation table starts before byte 28",
        ):
            parse_mz(mz_file(relocation_count=1, relocation_table_offset=27))

    def test_rejects_relocation_table_beyond_header(self):
        with self.assertRaisesRegex(
            ValueError,
            "MZ relocation table exceeds header size",
        ):
            parse_mz(mz_file(relocation_count=2, relocation_table_offset=28))

    def test_load_image_excludes_header_and_trailing_bytes(self):
        data = mz_file(payload=b"B" * 32) + b"TRAILING"

        self.assertEqual(load_image(data), b"B" * 32)

    def test_extracts_ordered_relocation_entries(self):
        data = bytearray(
            mz_file(relocation_count=1, relocation_table_offset=28),
        )
        data[28:32] = struct.pack("<HH", 0x1234, 0x5678)

        self.assertEqual(relocation_entries(bytes(data)), ((0x1234, 0x5678),))

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

    def test_rejects_same_report_input_and_output_without_changing_source(self):
        from tools.re.report_mz import write_report

        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "SAMPLE.EXE"
            original = mz_file()
            source.write_bytes(original)

            with self.assertRaisesRegex(ValueError, "same path"):
                write_report(source, source.parent / "." / source.name)

            self.assertEqual(source.read_bytes(), original)

    def test_rejects_hard_link_output_without_changing_source(self):
        from tools.re.report_mz import write_report

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "SAMPLE.EXE"
            output = root / "report.json"
            original = mz_file()
            source.write_bytes(original)
            os.link(source, output)

            with self.assertRaisesRegex(ValueError, "same file"):
                write_report(source, output)

            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(output.read_bytes(), original)

    def test_replaces_existing_report_atomically_without_temp_files(self):
        from tools.re.report_mz import write_report

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "SAMPLE.EXE"
            output = root / "report.json"
            observer = root / "old-report.json"
            source.write_bytes(mz_file())
            output.write_bytes(b"old report")
            os.link(output, observer)

            write_report(source, output)

            self.assertEqual(observer.read_bytes(), b"old report")
            self.assertEqual(
                json.loads(output.read_text(encoding="ascii"))["file"],
                "SAMPLE.EXE",
            )
            self.assertEqual(list(root.glob(f".{output.name}.*.tmp")), [])

    def test_cleans_temporary_report_when_atomic_replace_fails(self):
        from tools.re.report_mz import write_report

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "SAMPLE.EXE"
            output = root / "report.json"
            source.write_bytes(mz_file())
            output.mkdir()

            with self.assertRaises(OSError):
                write_report(source, output)

            self.assertTrue(output.is_dir())
            self.assertEqual(list(root.glob(f".{output.name}.*.tmp")), [])

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

    def test_report_script_prints_concise_error_for_malformed_input(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "BROKEN.EXE"
            output = root / "report.json"
            source.write_bytes(b"not an MZ executable")

            result = subprocess.run(
                [
                    sys.executable,
                    "tools/re/report_mz.py",
                    str(source),
                    str(output),
                ],
                cwd=Path.cwd(),
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 2)
            self.assertTrue(result.stderr.startswith("error: "), result.stderr)
            self.assertNotIn("Traceback", result.stderr)
            self.assertEqual(result.stdout, "")
            self.assertFalse(output.exists())

    def test_report_script_rejects_same_input_and_output_without_data_loss(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "SAMPLE.EXE"
            original = mz_file()
            source.write_bytes(original)

            result = subprocess.run(
                [
                    sys.executable,
                    "tools/re/report_mz.py",
                    str(source),
                    str(source),
                ],
                cwd=Path.cwd(),
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 2)
            self.assertIn("same path", result.stderr)
            self.assertNotIn("Traceback", result.stderr)
            self.assertEqual(source.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
