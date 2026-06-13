from __future__ import annotations

import hashlib
import os
import subprocess
import struct
import sys
import tempfile
import time
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.re.mz import parse_mz


PRIMARY_COMMIT = "3a1b8b54e63e7e03181916d40acf7626d5558f6d"
PRIMARY_SCRIPT_SHA256 = (
    "3e27a6b09cd6911f7b47332e67786d833a1303757dfa46977dedf749f2b0d5d5"
)
SUBPROCESS_TIMEOUT_SECONDS = 60
ATOMIC_REPLACE_TIMEOUT_SECONDS = 5


def run_checked(
    command: list[str],
    *,
    purpose: str,
    timeout: float = SUBPROCESS_TIMEOUT_SECONDS,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            command,
            cwd=cwd,
            env=env,
            check=True,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(f"{purpose} timed out after {timeout} seconds") from error
    except subprocess.CalledProcessError as error:
        detail = (error.stderr or error.stdout or "").strip()
        suffix = f": {detail}" if detail else ""
        raise RuntimeError(
            f"{purpose} failed with exit code {error.returncode}{suffix}"
        ) from error
    except OSError as error:
        raise RuntimeError(f"{purpose} could not start: {error}") from error


def require_distinct_files(source: Path, output: Path) -> None:
    if source.resolve() == output.resolve():
        raise ValueError("input and output resolve to the same file")
    if output.exists() and source.samefile(output):
        raise ValueError("input and output refer to the same file")


def atomic_write(output: Path, data: bytes) -> None:
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
            destination.write(data)
        deadline = time.monotonic() + ATOMIC_REPLACE_TIMEOUT_SECONDS
        while True:
            try:
                os.replace(temporary, output)
                temporary = None
                return
            except PermissionError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.01)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def require_pristine_checkout(checkout: Path, commit: str) -> None:
    head = run_checked(
        ["git", "-C", str(checkout), "rev-parse", "HEAD"],
        purpose=f"inspect {checkout.name} commit",
    ).stdout.strip()
    if head != commit:
        raise RuntimeError(f"checkout is at {head}, expected {commit}")
    status = run_checked(
        ["git", "-C", str(checkout), "status", "--porcelain"],
        purpose=f"inspect {checkout.name} status",
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


def _primary_source(root: Path) -> str:
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

    return _compatibility_source(script.read_text(encoding="ascii"))


def _normalize_primary_header(source: bytes, unpacked: bytes) -> bytes:
    packed_header = parse_mz(source)
    loader_offset = packed_header.header_size + packed_header.initial_cs * 16
    metadata = struct.unpack_from("<8H", source, loader_offset)
    increase_paragraphs = metadata[5]
    decompressor_bytes = metadata[6]

    normalized = bytearray(unpacked)
    min_extra = packed_header.min_extra_paragraphs
    max_extra = packed_header.max_extra_paragraphs
    if max_extra != 0:
        min_extra -= increase_paragraphs + (decompressor_bytes + 15) // 16 + 9
        if max_extra != 0xFFFF:
            max_extra -= packed_header.min_extra_paragraphs - min_extra
    struct.pack_into("<HH", normalized, 10, min_extra, max_extra)
    return bytes(normalized)


def unpack(source: Path, output: Path) -> None:
    source = source.resolve()
    output = output.resolve()
    require_distinct_files(source, output)
    root = Path(__file__).resolve().parents[2]
    compatible_source = _primary_source(root)
    output.parent.mkdir(parents=True, exist_ok=True)

    generated = root / "analysis" / "generated"
    generated.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=generated, prefix="primary-run-") as directory:
        run = Path(directory)
        script = run / "unpacklzexe_py3.py"
        temporary = run / "OUTPUT.EXE"
        script.write_bytes(compatible_source.encode("ascii"))
        run_checked(
            [sys.executable, str(script), str(source), str(temporary)],
            purpose="primary LZEXE unpack",
        )
        data = _normalize_primary_header(source.read_bytes(), temporary.read_bytes())
        parse_mz(data)
        if data[28:32] == b"LZ91":
            raise RuntimeError("primary unpacker left the LZ91 signature intact")
        atomic_write(output, data)


def main() -> int:
    try:
        unpack(Path("BUMPY.EXE"), Path("analysis/generated/BUMPY.UNPACKED.EXE"))
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
