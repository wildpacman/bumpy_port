from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.re.mz import parse_mz


def write_report(source: Path, output: Path) -> None:
    report = {
        "file": source.name,
        **parse_mz(source.read_bytes()).to_dict(),
    }
    content = json.dumps(report, indent=2, sort_keys=True) + "\n"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(content.encode("ascii"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect a DOS MZ executable")
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    write_report(args.input, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
