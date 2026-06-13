from __future__ import annotations

import struct
from dataclasses import asdict, dataclass


@dataclass(frozen=True)
class MzHeader:
    signature: int
    bytes_in_last_page: int
    page_count: int
    relocation_count: int
    header_paragraphs: int
    min_extra_paragraphs: int
    max_extra_paragraphs: int
    initial_ss: int
    initial_sp: int
    checksum: int
    initial_ip: int
    initial_cs: int
    relocation_table_offset: int
    overlay: int

    @property
    def file_size(self) -> int:
        return (self.page_count - 1) * 512 + (self.bytes_in_last_page or 512)

    @property
    def header_size(self) -> int:
        return self.header_paragraphs * 16

    def to_dict(self) -> dict[str, int]:
        return {
            **asdict(self),
            "file_size": self.file_size,
            "header_size": self.header_size,
        }


def parse_mz(data: bytes) -> MzHeader:
    if len(data) < 28:
        raise ValueError("MZ header is shorter than 28 bytes")

    header = MzHeader(*struct.unpack_from("<14H", data))
    if header.signature != 0x5A4D:
        raise ValueError("missing MZ signature")
    if header.page_count == 0:
        raise ValueError("MZ page count must be positive")
    if header.bytes_in_last_page > 511:
        raise ValueError("MZ last-page byte count exceeds 511")
    if header.header_size < 28:
        raise ValueError("MZ header size is shorter than 28 bytes")
    if header.file_size > len(data):
        raise ValueError("declared MZ size exceeds file size")
    if header.header_size > header.file_size:
        raise ValueError("MZ header exceeds declared file size")
    if header.relocation_count > 0:
        if header.relocation_table_offset < 28:
            raise ValueError("MZ relocation table starts before byte 28")
        relocation_table_end = (
            header.relocation_table_offset + header.relocation_count * 4
        )
        if relocation_table_end > header.header_size:
            raise ValueError("MZ relocation table exceeds header size")
    return header


def load_image(data: bytes) -> bytes:
    header = parse_mz(data)
    return data[header.header_size:header.file_size]


def relocation_entries(data: bytes) -> tuple[tuple[int, int], ...]:
    header = parse_mz(data)
    return tuple(
        struct.unpack_from("<HH", data, header.relocation_table_offset + index * 4)
        for index in range(header.relocation_count)
    )
