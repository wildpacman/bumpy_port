from __future__ import annotations

import csv
import hashlib
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.re.ghidra_catalog import (
    CATALOG_FIELDS,
    ADDRESS_FIELDS,
    address_mapping,
    merge_catalog,
    parse_segmented_address,
    read_csv,
    validate_catalog,
    write_csv_atomic,
)


ROOT = Path(__file__).resolve().parents[2]


class GhidraCatalogTest(unittest.TestCase):
    def test_merge_preserves_annotations_and_adds_new_discoveries(self) -> None:
        existing = [
            {
                "address": "1000:0010",
                "name": "render_player",
                "status": "confirmed",
                "evidence": "manual trace",
                "cpp_symbol": "render_player",
            }
        ]
        discovered = [
            {"address": "1000:0010", "name": "FUN_1000_0010"},
            {"address": "1000:0020", "name": "FUN_1000_0020"},
        ]

        merged = merge_catalog(existing, discovered)

        self.assertEqual(
            merged[0],
            {
                "address": "1000:0010",
                "name": "render_player",
                "status": "confirmed",
                "evidence": "manual trace",
                "cpp_symbol": "render_player",
            },
        )
        self.assertEqual(merged[1]["status"], "unknown")
        self.assertEqual(merged[1]["evidence"], "ghidra initial analysis")

    def test_merge_retains_missing_rows_with_explicit_evidence(self) -> None:
        existing = [
            {
                "address": "1000:0010",
                "name": "old_name",
                "status": "hypothesis",
                "evidence": "manual trace",
                "cpp_symbol": "",
            }
        ]

        first = merge_catalog(existing, [])
        second = merge_catalog(first, [])

        self.assertEqual(first, second)
        self.assertEqual(first[0]["status"], "hypothesis")
        self.assertIn("manual trace", first[0]["evidence"])
        self.assertIn("not discovered in latest analysis", first[0]["evidence"])

    def test_atomic_write_failure_does_not_truncate_catalog(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "functions.csv"
            target.write_text("original\n", encoding="ascii")
            with mock.patch.object(os, "replace", side_effect=OSError("stopped")):
                with self.assertRaises(OSError):
                    write_csv_atomic(
                        target,
                        CATALOG_FIELDS,
                        [
                            {
                                "address": "1000:0010",
                                "name": "FUN_1000_0010",
                                "status": "unknown",
                                "evidence": "ghidra initial analysis",
                                "cpp_symbol": "",
                            }
                        ],
                    )
            self.assertEqual(target.read_text(encoding="ascii"), "original\n")
            self.assertEqual(list(target.parent.glob(f".{target.name}.*.tmp")), [])

    def test_segmented_address_mapping_is_relative_to_load_module_base(self) -> None:
        parsed = parse_segmented_address("1000:1234")
        self.assertEqual(parsed, (0x1000, 0x1234))
        self.assertEqual(
            address_mapping("1000:1234"),
            {
                "address": "1000:1234",
                "segment": "0x1000",
                "offset": "0x1234",
                "linear_address": "0x11234",
                "image_offset": "0x1234",
            },
        )
        with self.assertRaisesRegex(ValueError, "below load-module base"):
            address_mapping("0fff:0000")

    def test_repository_catalogs_and_report_correspond_exactly(self) -> None:
        functions = read_csv(ROOT / "analysis/catalog/functions.csv", CATALOG_FIELDS)
        mappings = read_csv(
            ROOT / "analysis/catalog/function_addresses.csv", ADDRESS_FIELDS
        )
        validate_catalog(functions)
        self.assertEqual(
            [row["address"] for row in functions],
            [row["address"] for row in mappings],
        )
        self.assertEqual(
            mappings,
            [address_mapping(row["address"]) for row in functions],
        )
        self.assertTrue(
            all(int(row["image_offset"], 16) < 108096 for row in mappings)
        )
        self.assertEqual(
            (ROOT / "analysis/catalog/globals.csv").read_text(encoding="ascii"),
            "address,name,size,status,evidence,cpp_symbol\n",
        )

        report = json.loads(
            (ROOT / "analysis/reports/ghidra-analysis.json").read_text(
                encoding="ascii"
            )
        )
        catalog_bytes = (ROOT / "analysis/catalog/functions.csv").read_bytes()
        self.assertEqual(report["function_count"], len(functions))
        self.assertEqual(report["discovered_function_count"], 509)
        self.assertEqual(report["loader"], "MzLoader")
        self.assertEqual(report["loader_display"], "Old-style DOS Executable (MZ)")
        self.assertEqual(report["language"], "x86:LE:16:Real Mode:default")
        self.assertEqual(
            report["input_sha256"],
            "3ff2f60b474dc04b1de7c69cf3764b95e31967b74a00f755d231ddd3235adbe0",
        )
        self.assertEqual(
            report["catalog_sha256"], hashlib.sha256(catalog_bytes).hexdigest()
        )
        self.assertEqual(
            report["address_catalog_sha256"],
            hashlib.sha256(
                (ROOT / "analysis/catalog/function_addresses.csv").read_bytes()
            ).hexdigest(),
        )
        self.assertEqual(len(report["clean_imports"]), 2)
        self.assertEqual(
            [item["function_count"] for item in report["clean_imports"]],
            [509, 509],
        )
        self.assertEqual(report["clean_imports"][0]["discovery_sha256"],
                         report["clean_imports"][1]["discovery_sha256"])
        self.assertEqual(
            report["discovery_sha256"],
            report["clean_imports"][0]["discovery_sha256"],
        )
        self.assertEqual(
            [item["warning_count"] for item in report["clean_imports"]], [2, 2]
        )
        self.assertEqual(report["warning"]["count_per_import"], 2)
        self.assertEqual(
            report["warning"]["signature"],
            "Decompiling 1000:35a5, pcode error at 1000:84d7: "
            "Unable to resolve constructor at 1000:84d7",
        )
        self.assertIn(
            "no broader effect has been established",
            report["warning"]["confirmed_effect"],
        )
        self.assertEqual(report["tools"]["ghidra_version"], "12.1.2")
        self.assertEqual(report["tools"]["jdk_version"], "21.0.11")
        self.assertEqual(report["tools"]["pyghidra_version"], "3.1.0")
        self.assertEqual(
            report["tools"]["ghidra_archive_sha256"],
            "b62e81a0390618466c019c60d8c2f796ced2509c4c1aea4a37644a77272cf99d",
        )
        self.assertEqual(
            report["tools"]["jdk_archive_sha256"],
            "d3625e7cadf23787ea540229544b6e2ab494b3b54da1801879e583e1dfee0a64",
        )
        self.assertEqual(
            report["tools"]["pyghidra_wheel_sha256"],
            "d4d21729c126190ca358700220fed62af4be2252b4e255ffb889d82dd5a263ac",
        )


if __name__ == "__main__":
    unittest.main()
