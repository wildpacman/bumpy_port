from __future__ import annotations

import hashlib
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.re.compare_mz_images import compare_execution_semantics, execution_semantics
from tools.re.mz import parse_mz
from tools.re.unpack_bumpy import (
    PRIMARY_COMMIT,
    atomic_write,
    require_distinct_files,
    require_pristine_checkout,
    run_checked,
    unpack,
)


VALIDATOR_COMMIT = "066aac7be3b27813c221d3b03621ad6dfaecd285"
VALIDATOR_SOURCE_SHA256 = (
    "9263b494ca61016503b76453c5663ab2f66f208bd5259743b5d73e59dd28997c"
)
PRIMARY_IDENTITY = "samrussell/unpacklzexe Python implementation"
VALIDATOR_IDENTITY = "mywave82/unlzexe historical Kou Kurizono UNLZEXE C implementation"


def _msvc_environment(vcvars: Path) -> dict[str, str]:
    result = run_checked(
        ["cmd.exe", "/d", "/c", "call vcvars64.bat >nul && set"],
        cwd=vcvars.parent,
        purpose="initialize MSVC environment",
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


def _build_unlzexe(root: Path, build: Path) -> Path:
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

    executable = build / "unlzexe.exe"
    build.mkdir(parents=True, exist_ok=True)
    environment = _msvc_environment(_find_vcvars())
    run_checked(
        [
            "cl",
            "/nologo",
            "/O2",
            "/Dstrcasecmp=_stricmp",
            f"/Fo:{build / 'unlzexe.obj'}",
            f"/Fe:{executable}",
            str(source),
        ],
        cwd=build,
        env=environment,
        purpose="compile historical UNLZEXE",
    )
    require_pristine_checkout(checkout, VALIDATOR_COMMIT)
    return executable


def unpack_with_unlzexe(source: Path, output: Path) -> None:
    source = source.resolve()
    output = output.resolve()
    require_distinct_files(source, output)
    root = Path(__file__).resolve().parents[2]
    output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(
        dir=root / "analysis" / "generated", prefix="unlzexe-run-"
    ) as directory:
        run = Path(directory)
        executable = _build_unlzexe(root, run / "build")
        copied_source = run / "INPUT.EXE"
        generated = run / "OUTPUT.EXE"
        shutil.copyfile(source, copied_source)
        run_checked(
            [str(executable), "INPUT.EXE", "OUTPUT.EXE"],
            cwd=run,
            purpose="historical UNLZEXE unpack",
        )
        data = generated.read_bytes()
        parse_mz(data)
        if data[28:32] == b"LZ91":
            raise RuntimeError("UNLZEXE left the LZ91 signature intact")
        atomic_write(output, data)


def _artifact(identity: str, commit: str, data: bytes) -> dict[str, object]:
    semantics = execution_semantics(data)
    return {
        "commit": commit,
        "file_sha256": hashlib.sha256(data).hexdigest(),
        "file_size": len(data),
        "identity": identity,
        "initial_cs": semantics.initial_cs,
        "initial_ip": semantics.initial_ip,
        "initial_sp": semantics.initial_sp,
        "initial_ss": semantics.initial_ss,
        "load_image_sha256": semantics.load_image_sha256,
        "load_image_size": len(semantics.load_image),
        "max_extra_paragraphs": semantics.max_extra_paragraphs,
        "min_extra_paragraphs": semantics.min_extra_paragraphs,
        "relocation_count": len(semantics.relocations),
        "relocations_sha256": hashlib.sha256(
            b"".join(
                offset.to_bytes(2, "little") + segment.to_bytes(2, "little")
                for offset, segment in semantics.relocations
            )
        ).hexdigest(),
    }


def validate(primary: Path, validator: Path, output: Path) -> None:
    require_distinct_files(primary, output)
    require_distinct_files(validator, output)
    primary_data = primary.read_bytes()
    validator_data = validator.read_bytes()
    comparison = compare_execution_semantics(primary_data, validator_data)
    if not comparison.semantic_match:
        raise ValueError(
            "independent unpackers produced different execution semantics: "
            + ", ".join(comparison.differences)
        )
    report = {
        "differences": list(comparison.differences),
        "semantic_match": comparison.semantic_match,
        "primary": _artifact(PRIMARY_IDENTITY, PRIMARY_COMMIT, primary_data),
        "validator": _artifact(
            VALIDATOR_IDENTITY,
            VALIDATOR_COMMIT,
            validator_data,
        ),
    }
    content = (json.dumps(report, indent=2, sort_keys=True) + "\n").encode("ascii")
    atomic_write(output, content)


def main() -> int:
    try:
        primary = Path("analysis/generated/BUMPY.UNPACKED.EXE")
        validator = Path("analysis/generated/BUMPY.UNLZEXE.EXE")
        unpack(Path("BUMPY.EXE"), primary)
        unpack_with_unlzexe(Path("BUMPY.EXE"), validator)
        validate(primary, validator, Path("analysis/reports/unpack-validation.json"))
    except (OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
