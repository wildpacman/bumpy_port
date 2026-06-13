from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.re.mz import parse_mz


# Command-line input and reporting errors use exit code 2.
ERROR_EXIT_CODE = 2


def write_report(source: Path, output: Path) -> None:
    if source.resolve() == output.resolve():
        raise ValueError("input and output resolve to the same path")
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
    try:
        write_report(args.input, args.output)
    except (ValueError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return ERROR_EXIT_CODE
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
