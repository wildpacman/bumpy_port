import struct
import unittest

from tools.re.compare_mz_images import compare_execution_semantics


def mz_with_image(image: bytes, **changes: int) -> bytes:
    header_size = 48
    file_size = header_size + len(image)
    values = {
        "bytes_in_last_page": file_size % 512,
        "page_count": (file_size + 511) // 512,
        "relocation_count": 2,
        "header_paragraphs": header_size // 16,
        "min_extra_paragraphs": 3,
        "max_extra_paragraphs": 4,
        "initial_ss": 5,
        "initial_sp": 6,
        "checksum": 0,
        "initial_ip": 7,
        "initial_cs": 8,
        "relocation_table_offset": 28,
    }
    values.update(changes)
    header = struct.pack(
        "<14H",
        0x5A4D,
        values["bytes_in_last_page"],
        values["page_count"],
        values["relocation_count"],
        values["header_paragraphs"],
        values["min_extra_paragraphs"],
        values["max_extra_paragraphs"],
        values["initial_ss"],
        values["initial_sp"],
        values["checksum"],
        values["initial_ip"],
        values["initial_cs"],
        values["relocation_table_offset"],
        0,
    )
    relocations = struct.pack("<HHHH", 1, 2, 3, 4)
    return header + relocations + b"\x00" * (header_size - 36) + image


class CompareExecutionSemanticsTest(unittest.TestCase):
    def test_ignores_non_semantic_header_difference(self):
        first = mz_with_image(b"A" * 32)
        second = mz_with_image(b"A" * 32, checksum=0x1234)

        comparison = compare_execution_semantics(first, second)

        self.assertTrue(comparison.semantic_match)

    def test_reports_different_load_images(self):
        comparison = compare_execution_semantics(
            mz_with_image(b"A" * 32),
            mz_with_image(b"B" * 32),
        )

        self.assertFalse(comparison.semantic_match)
        self.assertIn("load_image", comparison.differences)

    def test_rejects_changed_relocation_order(self):
        second = bytearray(mz_with_image(b"A" * 32))
        second[28:36] = struct.pack("<HHHH", 3, 4, 1, 2)

        comparison = compare_execution_semantics(
            mz_with_image(b"A" * 32),
            bytes(second),
        )

        self.assertFalse(comparison.semantic_match)
        self.assertIn("relocations", comparison.differences)

    def test_rejects_each_runtime_significant_header_field(self):
        for field in (
            "initial_cs",
            "initial_ip",
            "initial_ss",
            "initial_sp",
            "min_extra_paragraphs",
            "max_extra_paragraphs",
        ):
            with self.subTest(field=field):
                comparison = compare_execution_semantics(
                    mz_with_image(b"A" * 32),
                    mz_with_image(b"A" * 32, **{field: 99}),
                )
                self.assertFalse(comparison.semantic_match)
                self.assertIn(field, comparison.differences)


if __name__ == "__main__":
    unittest.main()
