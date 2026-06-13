from __future__ import annotations

import argparse
import hashlib
from dataclasses import dataclass
from pathlib import Path


ASSET_SUFFIXES = {
    ".EXE",
    ".VEC",
    ".BNK",
    ".MID",
    ".BIN",
    ".BUM",
    ".DEC",
    ".PAV",
    ".CAR",
    ".NFO",
}
ASSET_NAMES = {"QUELDISK"}


@dataclass(frozen=True)
class Verification:
    changed: list[str]
    missing: list[str]

    @property
    def ok(self) -> bool:
        return not self.changed and not self.missing


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def asset_paths(root: Path) -> list[Path]:
    return sorted(
        path
        for path in root.iterdir()
        if path.is_file()
        and (
            path.suffix.upper() in ASSET_SUFFIXES
            or path.name in ASSET_NAMES
        )
    )


def write_manifest(root: Path, output: Path) -> None:
    lines = [f"{sha256(path)}  {path.name}\n" for path in asset_paths(root)]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("".join(lines), encoding="ascii")


def verify_manifest(root: Path, manifest: Path) -> Verification:
    changed: list[str] = []
    missing: list[str] = []
    for line in manifest.read_text(encoding="ascii").splitlines():
        expected, name = line.split("  ", 1)
        path = root / name
        if not path.exists():
            missing.append(name)
        elif sha256(path) != expected:
            changed.append(name)
    return Verification(changed=changed, missing=missing)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("write", "verify"))
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("config/original-assets.sha256"),
    )
    args = parser.parse_args()
    if args.command == "write":
        write_manifest(args.root, args.manifest)
        return 0

    result = verify_manifest(args.root, args.manifest)
    for name in result.missing:
        print(f"missing: {name}")
    for name in result.changed:
        print(f"changed: {name}")
    return 0 if result.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
