import hashlib
import struct
import unittest

from tools.re.compare_mz_images import Comparison, compare_load_images


def mz_with_image(image: bytes, *, checksum: int = 0) -> bytes:
    header_size = 32
    file_size = header_size + len(image)
    header = struct.pack(
        "<14H",
        0x5A4D,
        file_size % 512,
        (file_size + 511) // 512,
        0,
        header_size // 16,
        0,
        0xFFFF,
        0,
        0,
        checksum,
        0,
        0,
        28,
        0,
    )
    return header + b"\x00" * (header_size - len(header)) + image


class CompareMzImagesTest(unittest.TestCase):
    def test_ignores_different_headers_when_load_images_match(self):
        first = mz_with_image(b"A" * 32)
        second = mz_with_image(b"A" * 32, checksum=0x1234)

        comparison = compare_load_images(first, second)

        expected_hash = hashlib.sha256(b"A" * 32).hexdigest()
        self.assertEqual(
            comparison,
            Comparison(
                equal=True,
                first_sha256=expected_hash,
                second_sha256=expected_hash,
                size=32,
            ),
        )

    def test_reports_different_load_images(self):
        comparison = compare_load_images(
            mz_with_image(b"A" * 32),
            mz_with_image(b"B" * 32),
        )

        self.assertFalse(comparison.equal)
        self.assertNotEqual(comparison.first_sha256, comparison.second_sha256)
        self.assertEqual(comparison.size, 32)


if __name__ == "__main__":
    unittest.main()
