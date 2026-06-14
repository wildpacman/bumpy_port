from __future__ import annotations

import argparse
import csv
import os
import re
import tempfile
from pathlib import Path
from typing import Iterable


CATALOG_FIELDS = ("address", "name", "status", "evidence", "cpp_symbol")
DISCOVERY_FIELDS = ("address", "name")
ADDRESS_FIELDS = (
    "address",
    "segment",
    "offset",
    "linear_address",
    "image_offset",
)
LOAD_MODULE_LINEAR_BASE = 0x10000
MISSING_EVIDENCE = "not discovered in latest analysis"
ADDRESS_PATTERN = re.compile(r"^([0-9a-fA-F]{4}):([0-9a-fA-F]{4})$")


def parse_segmented_address(address: str) -> tuple[int, int]:
    match = ADDRESS_PATTERN.fullmatch(address)
    if match is None:
        raise ValueError(f"invalid segmented address: {address}")
    return int(match.group(1), 16), int(match.group(2), 16)


def _sort_key(row: dict[str, str]) -> tuple[int, int]:
    return parse_segmented_address(row["address"])


def read_csv(path: Path, fields: Iterable[str]) -> list[dict[str, str]]:
    expected = tuple(fields)
    with path.open("r", encoding="ascii", newline="") as stream:
        reader = csv.DictReader(stream)
        if tuple(reader.fieldnames or ()) != expected:
            raise ValueError(f"{path} has unexpected CSV schema")
        return [dict(row) for row in reader]


def validate_catalog(rows: list[dict[str, str]]) -> None:
    seen: set[str] = set()
    for row in rows:
        if tuple(row) != CATALOG_FIELDS:
            raise ValueError("function catalog row has unexpected fields")
        parse_segmented_address(row["address"])
        if row["address"] in seen:
            raise ValueError(f"duplicate function address: {row['address']}")
        seen.add(row["address"])
        if not row["name"]:
            raise ValueError(f"function without name: {row['address']}")
        if not row["status"].strip():
            raise ValueError(f"function without status: {row['address']}")
        if not row["evidence"].strip():
            raise ValueError(f"function without evidence: {row['address']}")


def validate_discovery(rows: list[dict[str, str]]) -> None:
    seen: set[str] = set()
    for row in rows:
        if tuple(row) != DISCOVERY_FIELDS:
            raise ValueError("Ghidra discovery row has unexpected fields")
        parse_segmented_address(row["address"])
        if row["address"] in seen:
            raise ValueError(f"duplicate discovered address: {row['address']}")
        seen.add(row["address"])
        if not row["name"]:
            raise ValueError(f"discovered function without name: {row['address']}")


def merge_catalog(
    existing: list[dict[str, str]], discovered: list[dict[str, str]]
) -> list[dict[str, str]]:
    validate_catalog(existing)
    validate_discovery(discovered)
    existing_by_address = {row["address"]: dict(row) for row in existing}
    discovered_addresses = {row["address"] for row in discovered}

    merged: list[dict[str, str]] = []
    for row in discovered:
        prior = existing_by_address.get(row["address"])
        if prior is None:
            merged.append(
                {
                    "address": row["address"],
                    "name": row["name"],
                    "status": "unknown",
                    "evidence": "ghidra initial analysis",
                    "cpp_symbol": "",
                }
            )
        else:
            evidence = prior["evidence"].replace(f"; {MISSING_EVIDENCE}", "")
            merged.append({**prior, "evidence": evidence})

    for address, prior in existing_by_address.items():
        if address in discovered_addresses:
            continue
        evidence = prior["evidence"]
        if MISSING_EVIDENCE not in evidence:
            evidence = f"{evidence}; {MISSING_EVIDENCE}"
        merged.append({**prior, "evidence": evidence})

    merged.sort(key=_sort_key)
    validate_catalog(merged)
    return merged


def address_mapping(address: str) -> dict[str, str]:
    segment, offset = parse_segmented_address(address)
    linear = segment * 16 + offset
    if linear < LOAD_MODULE_LINEAR_BASE:
        raise ValueError(f"address below load-module base: {address}")
    return {
        "address": address,
        "segment": f"0x{segment:04x}",
        "offset": f"0x{offset:04x}",
        "linear_address": f"0x{linear:08x}",
        "image_offset": f"0x{linear - LOAD_MODULE_LINEAR_BASE:08x}",
    }


def write_csv_atomic(
    path: Path, fields: Iterable[str], rows: Iterable[dict[str, str]]
) -> None:
    path = path.resolve()
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        dir=path.parent, prefix=f".{path.name}.", suffix=".tmp"
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="ascii", newline="") as stream:
            writer = csv.DictWriter(
                stream, fieldnames=tuple(fields), lineterminator="\n"
            )
            writer.writeheader()
            writer.writerows(rows)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def publish(discovery_path: Path, catalog_path: Path, addresses_path: Path) -> None:
    discovered = read_csv(discovery_path, DISCOVERY_FIELDS)
    existing = (
        read_csv(catalog_path, CATALOG_FIELDS) if catalog_path.is_file() else []
    )
    merged = merge_catalog(existing, discovered)
    mappings = [address_mapping(row["address"]) for row in merged]
    write_csv_atomic(catalog_path, CATALOG_FIELDS, merged)
    write_csv_atomic(addresses_path, ADDRESS_FIELDS, mappings)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("discovery", type=Path)
    parser.add_argument("catalog", type=Path)
    parser.add_argument("addresses", type=Path)
    args = parser.parse_args()
    try:
        publish(args.discovery, args.catalog, args.addresses)
    except (OSError, ValueError) as error:
        parser.exit(2, f"error: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
