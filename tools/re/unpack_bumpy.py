from __future__ import annotations

import hashlib
import os
import subprocess
import sys
import tempfile
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.re.mz import parse_mz


PRIMARY_COMMIT = "3a1b8b54e63e7e03181916d40acf7626d5558f6d"
PRIMARY_SCRIPT_SHA256 = (
    "3e27a6b09cd6911f7b47332e67786d833a1303757dfa46977dedf749f2b0d5d5"
)


def require_distinct_files(source: Path, output: Path) -> None:
    if source.resolve() == output.resolve():
        raise ValueError("input and output resolve to the same file")
    if output.exists() and source.samefile(output):
        raise ValueError("input and output refer to the same file")


def require_pristine_checkout(checkout: Path, commit: str) -> None:
    head = subprocess.run(
        ["git", "-C", str(checkout), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    if head != commit:
        raise RuntimeError(f"checkout is at {head}, expected {commit}")
    status = subprocess.run(
        ["git", "-C", str(checkout), "status", "--porcelain"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    if status:
        raise RuntimeError(f"vendor checkout is not pristine: {checkout}")


def _compatibility_source(source: str) -> str:
    replacements = {
        'if checkstring == "LZ91":': 'if checkstring == b"LZ91":',
        '  outdata = ""': "  outdata = bytearray()",
        "      outdata = outdata + indata[si]": "      outdata.append(indata[si])",
        "        outdata = outdata + tempbyte": "        outdata.append(tempbyte)",
        "  headerdata['relocnt'] = len(unpackedreloc)/4": (
            "  headerdata['relocnt'] = len(unpackedreloc)//4"
        ),
        "  headerdata['hdrsize'] = headersize/0x10": (
            "  headerdata['hdrsize'] = headersize//0x10"
        ),
        "  headerdata['pagecnt'] = filesize / 0x200": (
            "  headerdata['pagecnt'] = filesize // 0x200"
        ),
        "  headerdata['minalloc'] = headerdata['minalloc'] - lessmemory/0x10": (
            "  headerdata['minalloc'] = headerdata['minalloc'] - lessmemory//0x10"
        ),
        "  padding = ''.join(['\\x00' for x in range(padbytes)])": (
            "  padding = b''.join([b'\\x00' for x in range(padbytes)])"
        ),
        "  fout = open(outfile, 'w')": "  fout = open(outfile, 'wb')",
        '    print "Detected LZ91 file"': '    print("Detected LZ91 file")',
        "  f = open(filename, 'r')": "  f = open(filename, 'rb')",
        '    print "Done"': '    print("Done")',
        '    print "Not a valid LZ91 file, could not unpack"': (
            '    print("Not a valid LZ91 file, could not unpack")'
        ),
    }
    for old, new in replacements.items():
        if old not in source:
            raise RuntimeError(f"pinned unpacker no longer contains expected text: {old}")
        source = source.replace(old, new)
    return source


def _prepare_primary_unpacker(root: Path) -> Path:
    checkout = root / "tools" / "vendor" / "unpacklzexe"
    script = checkout / "unpacklzexe.py"
    if not script.is_file():
        raise RuntimeError("run tools/re/bootstrap_tools.ps1 first")
    require_pristine_checkout(checkout, PRIMARY_COMMIT)
    actual_hash = hashlib.sha256(script.read_bytes()).hexdigest()
    if actual_hash != PRIMARY_SCRIPT_SHA256:
        raise RuntimeError(
            f"primary unpacker SHA-256 mismatch: expected {PRIMARY_SCRIPT_SHA256}, "
            f"got {actual_hash}"
        )

    compatible = root / "analysis" / "generated" / "unpacklzexe_py3.py"
    compatible.parent.mkdir(parents=True, exist_ok=True)
    compatible.write_bytes(
        _compatibility_source(script.read_text(encoding="ascii")).encode("ascii")
    )
    return compatible


def unpack(source: Path, output: Path) -> None:
    source = source.resolve()
    output = output.resolve()
    require_distinct_files(source, output)
    root = Path(__file__).resolve().parents[2]
    script = _prepare_primary_unpacker(root)
    output.parent.mkdir(parents=True, exist_ok=True)

    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=output.parent,
            prefix=f".{output.name}.",
            suffix=".tmp",
            delete=False,
        ) as destination:
            temporary = Path(destination.name)
        temporary.unlink()
        subprocess.run(
            [sys.executable, str(script), str(source), str(temporary)],
            check=True,
        )
        data = temporary.read_bytes()
        parse_mz(data)
        if data[28:32] == b"LZ91":
            raise RuntimeError("primary unpacker left the LZ91 signature intact")
        os.replace(temporary, output)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main() -> int:
    unpack(Path("BUMPY.EXE"), Path("analysis/generated/BUMPY.UNPACKED.EXE"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
