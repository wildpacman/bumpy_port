from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.re.compare_mz_images import compare_load_images
from tools.re.mz import load_image, parse_mz
from tools.re.unpack_bumpy import (
    PRIMARY_COMMIT,
    require_distinct_files,
    require_pristine_checkout,
    unpack,
)


VALIDATOR_COMMIT = "066aac7be3b27813c221d3b03621ad6dfaecd285"
VALIDATOR_SOURCE_SHA256 = (
    "9263b494ca61016503b76453c5663ab2f66f208bd5259743b5d73e59dd28997c"
)
PRIMARY_IDENTITY = "samrussell/unpacklzexe Python implementation"
VALIDATOR_IDENTITY = "mywave82/unlzexe historical Kou Kurizono UNLZEXE C implementation"


def _msvc_environment(vcvars: Path) -> dict[str, str]:
    result = subprocess.run(
        ["cmd.exe", "/d", "/c", "call vcvars64.bat >nul && set"],
        cwd=vcvars.parent,
        check=True,
        capture_output=True,
        text=True,
    )
    environment = dict(os.environ)
    for line in result.stdout.splitlines():
        if "=" in line:
            name, value = line.split("=", 1)
            environment[name] = value
    return environment


def _find_vcvars() -> Path:
    program_files = Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
    matches = sorted(
        program_files.glob(
            "Microsoft Visual Studio/*/*/VC/Auxiliary/Build/vcvars64.bat"
        ),
        reverse=True,
    )
    if not matches:
        raise RuntimeError("Visual Studio vcvars64.bat was not found")
    return matches[0]


def _build_unlzexe(root: Path) -> Path:
    checkout = root / "tools" / "vendor" / "unlzexe"
    source = checkout / "unlzexe.c"
    if not source.is_file():
        raise RuntimeError(
            "clone mywave82/unlzexe into tools/vendor/unlzexe at the pinned commit"
        )
    require_pristine_checkout(checkout, VALIDATOR_COMMIT)
    actual_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    if actual_hash != VALIDATOR_SOURCE_SHA256:
        raise RuntimeError(
            f"UNLZEXE source SHA-256 mismatch: expected {VALIDATOR_SOURCE_SHA256}, "
            f"got {actual_hash}"
        )

    build = root / "analysis" / "generated" / "unlzexe-build"
    executable = build / "unlzexe.exe"
    build.mkdir(parents=True, exist_ok=True)
    environment = _msvc_environment(_find_vcvars())
    subprocess.run(
        [
            "cl",
            "/nologo",
            "/O2",
            "/Dstrcasecmp=_stricmp",
            f"/Fo:{build / 'unlzexe.obj'}",
            f"/Fe:{executable}",
            str(source),
        ],
        cwd=root,
        env=environment,
        check=True,
    )
    require_pristine_checkout(checkout, VALIDATOR_COMMIT)
    return executable


def unpack_with_unlzexe(source: Path, output: Path) -> None:
    source = source.resolve()
    output = output.resolve()
    require_distinct_files(source, output)
    root = Path(__file__).resolve().parents[2]
    executable = _build_unlzexe(root)
    output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(
        dir=root / "analysis" / "generated", prefix="unlzexe-run-"
    ) as directory:
        run = Path(directory)
        copied_source = run / "INPUT.EXE"
        generated = run / "OUTPUT.EXE"
        shutil.copyfile(source, copied_source)
        subprocess.run(
            [str(executable), str(copied_source), str(generated)],
            cwd=run,
            check=True,
        )
        data = generated.read_bytes()
        parse_mz(data)
        if data[28:32] == b"LZ91":
            raise RuntimeError("UNLZEXE left the LZ91 signature intact")
        _atomic_write(output, data)


def _atomic_write(output: Path, data: bytes) -> None:
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
        os.replace(temporary, output)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def _artifact(identity: str, commit: str, data: bytes) -> dict[str, object]:
    image = load_image(data)
    return {
        "commit": commit,
        "file_sha256": hashlib.sha256(data).hexdigest(),
        "file_size": len(data),
        "identity": identity,
        "load_image_sha256": hashlib.sha256(image).hexdigest(),
        "load_image_size": len(image),
    }


def validate(primary: Path, validator: Path, output: Path) -> None:
    require_distinct_files(primary, output)
    require_distinct_files(validator, output)
    primary_data = primary.read_bytes()
    validator_data = validator.read_bytes()
    comparison = compare_load_images(primary_data, validator_data)
    if not comparison.equal:
        raise ValueError("independent unpackers produced different MZ load images")
    report = {
        "equal": comparison.equal,
        "load_image_size": comparison.size,
        "primary": _artifact(PRIMARY_IDENTITY, PRIMARY_COMMIT, primary_data),
        "validator": _artifact(
            VALIDATOR_IDENTITY,
            VALIDATOR_COMMIT,
            validator_data,
        ),
    }
    content = (json.dumps(report, indent=2, sort_keys=True) + "\n").encode("ascii")
    output.parent.mkdir(parents=True, exist_ok=True)
    _atomic_write(output, content)


def main() -> int:
    primary = Path("analysis/generated/BUMPY.UNPACKED.EXE")
    validator = Path("analysis/generated/BUMPY.UNLZEXE.EXE")
    unpack(Path("BUMPY.EXE"), primary)
    unpack_with_unlzexe(Path("BUMPY.EXE"), validator)
    validate(primary, validator, Path("analysis/reports/unpack-validation.json"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
