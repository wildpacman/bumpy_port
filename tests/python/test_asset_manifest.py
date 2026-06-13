import hashlib
import tempfile
import unittest
from pathlib import Path

from tools.assets.manifest import asset_paths, verify_manifest, write_manifest


class AssetManifestTest(unittest.TestCase):
    def test_reports_changed_and_missing_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "A.BIN").write_bytes(b"original")
            digest = hashlib.sha256(b"original").hexdigest()
            manifest = root / "assets.sha256"
            manifest.write_text(
                f"{digest}  A.BIN\n{'0' * 64}  MISSING.BIN\n",
                encoding="ascii",
            )

            (root / "A.BIN").write_bytes(b"changed")
            result = verify_manifest(root, manifest)

            self.assertEqual(result.changed, ["A.BIN"])
            self.assertEqual(result.missing, ["MISSING.BIN"])
            self.assertFalse(result.ok)

    def test_rejects_manifest_names_that_are_not_plain_filenames(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            digest = hashlib.sha256(b"outside").hexdigest()
            for name in (
                "../OUTSIDE.BIN",
                "subdir/ASSET.BIN",
                "subdir\\ASSET.BIN",
                "/ABSOLUTE.BIN",
                "C:\\ABSOLUTE.BIN",
            ):
                with self.subTest(name=name):
                    manifest = root / "assets.sha256"
                    manifest.write_text(
                        f"{digest}  {name}\n",
                        encoding="ascii",
                    )

                    with self.assertRaisesRegex(ValueError, "plain filename"):
                        verify_manifest(root, manifest)

    def test_reports_directory_entries_as_missing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "A.BIN").mkdir()
            manifest = root / "assets.sha256"
            manifest.write_text(
                f"{'0' * 64}  A.BIN\n",
                encoding="ascii",
            )

            result = verify_manifest(root, manifest)

            self.assertEqual(result.missing, ["A.BIN"])
            self.assertEqual(result.changed, [])

    def test_writes_selected_assets_in_name_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            selected_names = [
                "GAME.EXE",
                "ART.VEC",
                "SOUND.BNK",
                "MUSIC.MID",
                "DATA.BIN",
                "LEVEL.BUM",
                "SCENE.DEC",
                "TILES.PAV",
                "FONT.CAR",
                "README.NFO",
                "QUELDISK",
            ]
            for name in reversed(selected_names):
                (root / name).write_bytes(name.encode("ascii"))
            (root / "ignored.txt").write_bytes(b"ignored")
            output = root / "assets.sha256"

            write_manifest(root, output)

            names = [line.split("  ", 1)[1] for line in output.read_text(
                encoding="ascii"
            ).splitlines()]
            self.assertEqual(names, sorted(selected_names))
            self.assertEqual(
                [path.name for path in asset_paths(root)],
                sorted(selected_names),
            )
            self.assertTrue(verify_manifest(root, output).ok)
            expected = b"".join(
                f"{hashlib.sha256(name.encode('ascii')).hexdigest()}  {name}\n".encode(
                    "ascii"
                )
                for name in sorted(selected_names)
            )
            self.assertEqual(output.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
