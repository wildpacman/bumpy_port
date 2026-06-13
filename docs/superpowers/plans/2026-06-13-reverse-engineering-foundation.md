# Reverse Engineering Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Создать воспроизводимую исследовательскую базу и нативный SDL3-каркас, на которых можно доказуемо восстанавливать меню и первый уровень Bumpy's Arcade Fantasy.

**Architecture:** Python-инструменты отвечают за неизменность оригинальных файлов, разбор MZ EXE, воспроизводимую распаковку и экспорт отчётов анализа. Ghidra и DOSBox-X используются как независимые исследовательские инструменты. C++20 runtime собирается CMake/MSVC, держит игровую логику отдельно от SDL3 и на этом рубеже запускает окно с индексным framebuffer после проверки оригинальных ресурсов.

**Tech Stack:** Python 3.14 standard library, PowerShell 7/Windows PowerShell, Ghidra 12.1.2, DOSBox-X 2026.06.02, CMake 4.2+, MSVC 14.50+, C++20, SDL 3.4.10, Catch2 3.15.0.

---

## Граница плана

Это первый из четырёх последовательных планов вертикального среза:

1. **Исследовательская база и runtime-каркас** — этот документ.
2. **Форматы ресурсов и точное воспроизведение меню.**
3. **Восстановление симуляции, физики и объектов первого уровня.**
4. **Интеграция полного пути, звук и доказательство совпадения.**

Этот план завершён, когда:

- оригинальные файлы защищены проверяемым SHA-256-манифестом;
- `BUMPY.EXE` воспроизводимо распаковывается и результат проверен двумя
  независимыми распаковщиками на уровне MZ load image;
- создана Ghidra-база и экспортирован исходный каталог функций;
- DOSBox-X запускает оригинал в фиксированной эталонной конфигурации;
- C++/SDL3-приложение собирается, проверяет оригинальные ресурсы и показывает
  индексный 320x200 framebuffer без влияния SDL на будущую симуляцию;
- все Python- и C++-тесты проходят одной командой.

## Структура файлов

```text
CMakeLists.txt                          # Корневая CMake-конфигурация
CMakePresets.json                       # Воспроизводимые Windows presets
config/original-assets.sha256           # Зафиксированные хэши оригиналов
analysis/
  README.md                             # Правила и состояния исследования
  catalog/functions.csv                 # Экспорт функций с адресами и статусами
  catalog/globals.csv                   # Каталог глобальных данных
  ghidra_scripts/ExportFunctions.py     # Экспорт функций из Ghidra
  reports/mz-header.json                # Проверенный отчёт о DOS EXE
  reports/unpack-validation.json        # Сравнение двух распаковщиков
reference/
  dosbox-x.conf                         # Фиксированная эталонная конфигурация
  README.md                             # Запуск и работа с отладчиком
src/
  app/main.cpp                          # Композиция приложения
  core/asset_manifest.{h,cpp}           # Проверка входных файлов
  core/indexed_framebuffer.{h,cpp}      # Независимый от SDL индексный кадр
  platform_sdl3/sdl_app.{h,cpp}         # Окно, события и вывод SDL3
tests/
  cpp/asset_manifest_test.cpp
  cpp/indexed_framebuffer_test.cpp
  python/test_asset_manifest.py
  python/test_compare_mz_images.py
  python/test_mz.py
  python/test_unpack_bumpy.py
tools/
  assets/manifest.py                    # Генерация и проверка SHA-256
  re/bootstrap_tools.ps1                # Проверка/подготовка инструментов
  re/compare_mz_images.py               # Сравнение MZ load image
  re/mz.py                              # Разбор MZ-заголовка
  re/report_mz.py                       # Генерация JSON-отчёта
  re/unpack_bumpy.py                    # Контролируемый запуск распаковщика
  re/validate_unpack.py                 # Двойная проверка распаковки
  reference/run_reference.ps1           # Запуск оригинала в DOSBox-X
```

Каталоги `analysis/generated/`, `tools/vendor/`, `build/` и `out/` содержат
воспроизводимые или внешние артефакты и не коммитятся.

### Task 1: Защитить оригинальные файлы манифестом

**Files:**
- Modify: `.gitignore`
- Create: `tools/assets/manifest.py`
- Create: `tests/python/test_asset_manifest.py`
- Create: `config/original-assets.sha256`

- [ ] **Step 1: Написать падающий тест проверки манифеста**

```python
# tests/python/test_asset_manifest.py
import hashlib
import tempfile
import unittest
from pathlib import Path

from tools.assets.manifest import verify_manifest


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


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Запустить тест и подтвердить ожидаемое падение**

Run:

```powershell
python -m unittest tests.python.test_asset_manifest -v
```

Expected: `ModuleNotFoundError: No module named 'tools.assets.manifest'`.

- [ ] **Step 3: Реализовать генерацию и проверку SHA-256**

```python
# tools/assets/manifest.py
from __future__ import annotations

import argparse
import hashlib
from dataclasses import dataclass
from pathlib import Path

ASSET_SUFFIXES = {
    ".EXE", ".VEC", ".BNK", ".MID", ".BIN", ".BUM", ".DEC", ".PAV", ".CAR", ".NFO"
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
        path for path in root.iterdir()
        if path.is_file() and (path.suffix.upper() in ASSET_SUFFIXES or path.name in ASSET_NAMES)
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
    parser.add_argument("--manifest", type=Path, default=Path("config/original-assets.sha256"))
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
```

- [ ] **Step 4: Сгенерировать манифест и убедиться, что он содержит 50 файлов**

Run:

```powershell
python tools/assets/manifest.py write
(Get-Content config/original-assets.sha256).Count
python tools/assets/manifest.py verify
```

Expected: count is `50`; verification exits with code `0`.

- [ ] **Step 5: Исключить оригиналы и производные артефакты из git**

Добавить в `.gitignore`:

```gitignore
# User-supplied original game files
/*.EXE
/*.VEC
/*.BNK
/*.MID
/*.BIN
/*.BUM
/*.DEC
/*.PAV
/*.CAR
/*.NFO
/QUELDISK

# Reproducible reverse-engineering artifacts and downloaded tools
/analysis/generated/
/tools/vendor/
```

- [ ] **Step 6: Запустить тесты и зафиксировать изменение**

Run:

```powershell
python -m unittest discover -s tests/python -v
git add .gitignore config/original-assets.sha256 tools/assets/manifest.py tests/python/test_asset_manifest.py
git commit -m "build: protect original game assets"
```

Expected: one Python test passes; commit succeeds.

### Task 2: Разобрать MZ-заголовок и создать проверяемый отчёт

**Files:**
- Create: `tools/re/mz.py`
- Create: `tools/re/report_mz.py`
- Create: `tests/python/test_mz.py`
- Create: `analysis/reports/mz-header.json`

- [ ] **Step 1: Написать падающий тест известных полей `BUMPY.EXE`**

```python
# tests/python/test_mz.py
import unittest
from pathlib import Path

from tools.re.mz import parse_mz


class MzHeaderTest(unittest.TestCase):
    def test_parses_packed_bumpy_header(self):
        header = parse_mz(Path("BUMPY.EXE").read_bytes())
        self.assertEqual(header.signature, 0x5A4D)
        self.assertEqual(header.file_size, 46019)
        self.assertEqual(header.header_size, 32)
        self.assertEqual(header.initial_cs, 2783)
        self.assertEqual(header.initial_ip, 14)
        self.assertEqual(header.relocation_count, 0)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Запустить тест и подтвердить ожидаемое падение**

Run:

```powershell
python -m unittest tests.python.test_mz -v
```

Expected: `ModuleNotFoundError: No module named 'tools.re.mz'`.

- [ ] **Step 3: Реализовать строгий MZ-парсер**

```python
# tools/re/mz.py
from __future__ import annotations

import struct
from dataclasses import asdict, dataclass


@dataclass(frozen=True)
class MzHeader:
    signature: int
    bytes_in_last_page: int
    page_count: int
    relocation_count: int
    header_paragraphs: int
    min_extra_paragraphs: int
    max_extra_paragraphs: int
    initial_ss: int
    initial_sp: int
    checksum: int
    initial_ip: int
    initial_cs: int
    relocation_table_offset: int
    overlay: int

    @property
    def file_size(self) -> int:
        return (self.page_count - 1) * 512 + (self.bytes_in_last_page or 512)

    @property
    def header_size(self) -> int:
        return self.header_paragraphs * 16

    def to_dict(self) -> dict[str, int]:
        return {**asdict(self), "file_size": self.file_size, "header_size": self.header_size}


def parse_mz(data: bytes) -> MzHeader:
    if len(data) < 28:
        raise ValueError("MZ header is shorter than 28 bytes")
    values = struct.unpack_from("<14H", data, 0)
    header = MzHeader(*values)
    if header.signature != 0x5A4D:
        raise ValueError("missing MZ signature")
    if header.file_size > len(data):
        raise ValueError("declared MZ size exceeds file size")
    return header


def load_image(data: bytes) -> bytes:
    header = parse_mz(data)
    return data[header.header_size:header.file_size]
```

- [ ] **Step 4: Реализовать генератор стабильного JSON-отчёта**

```python
# tools/re/report_mz.py
import argparse
import json
from pathlib import Path

from tools.re.mz import parse_mz


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    report = {"file": args.input.name, **parse_mz(args.input.read_bytes()).to_dict()}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="ascii")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 5: Запустить тест и создать отчёт**

Run:

```powershell
python -m unittest tests.python.test_mz -v
python tools/re/report_mz.py BUMPY.EXE analysis/reports/mz-header.json
python tools/re/report_mz.py BUMPY.EXE out/mz-header-check.json
Compare-Object (Get-Content analysis/reports/mz-header.json) (Get-Content out/mz-header-check.json)
```

Expected: test passes; `Compare-Object` prints nothing.

- [ ] **Step 6: Зафиксировать MZ-парсер и отчёт**

Run:

```powershell
git add tools/re/mz.py tools/re/report_mz.py tests/python/test_mz.py analysis/reports/mz-header.json
git commit -m "feat: add strict DOS MZ inspection"
```

### Task 3: Подготовить закреплённые исследовательские инструменты

**Files:**
- Create: `tools/re/bootstrap_tools.ps1`
- Create: `analysis/README.md`

- [ ] **Step 1: Написать скрипт проверки локальных prerequisites и загрузки распаковщика**

```powershell
# tools/re/bootstrap_tools.ps1
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$vendor = Join-Path $root "tools\vendor"
New-Item -ItemType Directory -Force $vendor | Out-Null

foreach ($tool in @("python", "cmake", "git", "cl")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Required tool is not on PATH: $tool"
    }
}

$unpacker = Join-Path $vendor "unpacklzexe"
if (-not (Test-Path $unpacker)) {
    git clone https://github.com/samrussell/unpacklzexe.git $unpacker
}
git -C $unpacker fetch --all --tags
git -C $unpacker checkout --detach 3a1b8b54e63e7e03181916d40acf7626d5558f6d

foreach ($tool in @("ghidraRun", "analyzeHeadless", "dosbox-x")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Write-Warning "$tool is not on PATH; install the pinned version documented in analysis/README.md"
    }
}
```

- [ ] **Step 2: Документировать закреплённые версии и назначение инструментов**

```markdown
# Reverse-engineering workspace

## Pinned tools

- Ghidra 12.1.2: static analysis and decompilation.
- DOSBox-X 2026.06.02: reference execution and debugger.
- samrussell/unpacklzexe commit `3a1b8b54e63e7e03181916d40acf7626d5558f6d`:
  first LZEXE 0.91 unpacker.
- Official LZEXE 0.91 `UPACKEXE.EXE`: independent unpack validation under DOSBox-X.

Downloaded tools and generated binaries belong in ignored directories. Reports,
catalogs, scripts, and conclusions belong in git.

## Evidence states

- `unknown`: exported but not investigated.
- `hypothesis`: named from evidence that still needs an independent check.
- `confirmed`: validated by static analysis plus reference execution or an
  independent implementation.
```

- [ ] **Step 3: Запустить bootstrap и проверить закреплённый commit**

Run:

```powershell
& tools/re/bootstrap_tools.ps1
git -C tools/vendor/unpacklzexe rev-parse HEAD
```

Expected: prints `3a1b8b54e63e7e03181916d40acf7626d5558f6d`; missing Ghidra or DOSBox-X is reported as a warning, not hidden.

- [ ] **Step 4: Установить Ghidra и DOSBox-X, если bootstrap сообщил предупреждения**

Загрузить официальные архивы:

- `https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.2_build/ghidra_12.1.2_PUBLIC_20260605.zip`
- `https://github.com/joncampbell123/dosbox-x/releases/download/dosbox-x-v2026.06.02-osfree/dosbox-x-mingw64-dosbox-x-v2026.06.02-osfree-portable.zip`

Распаковать их вне репозитория, установить 64-bit JDK 21 для Ghidra, добавить
каталог Ghidra, `support/` Ghidra и каталог DOSBox-X в пользовательский `PATH`,
затем повторить:

```powershell
& tools/re/bootstrap_tools.ps1
Get-Command ghidraRun,analyzeHeadless,dosbox-x
```

Expected: all three commands resolve to executable paths.

- [ ] **Step 5: Зафиксировать bootstrap и правила анализа**

Run:

```powershell
git add tools/re/bootstrap_tools.ps1 analysis/README.md
git commit -m "build: pin reverse engineering tools"
```

### Task 4: Сделать распаковку `BUMPY.EXE` воспроизводимой

**Files:**
- Create: `tools/re/unpack_bumpy.py`
- Create: `tools/re/compare_mz_images.py`
- Create: `tools/re/validate_unpack.py`
- Create: `tests/python/test_compare_mz_images.py`
- Create: `tests/python/test_unpack_bumpy.py`
- Create: `analysis/reports/unpack-validation.json`

- [ ] **Step 1: Написать падающий тест сравнения MZ load image**

```python
# tests/python/test_compare_mz_images.py
import struct
import unittest

from tools.re.compare_mz_images import compare_load_images


class CompareMzImagesTest(unittest.TestCase):
    def test_ignores_different_headers_when_load_images_match(self):
        header = struct.pack(
            "<14H",
            0x5A4D, 64, 1, 0, 2, 0, 0xffff, 0, 0, 0, 0, 0, 28, 0,
        )
        first = header + b"\x00" * 4 + b"A" * 32
        second = bytearray(first)
        second[18:20] = b"\x34\x12"
        self.assertTrue(compare_load_images(first, bytes(second)).equal)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Реализовать сравнение load image**

```python
# tools/re/compare_mz_images.py
from dataclasses import dataclass
import hashlib

from tools.re.mz import load_image


@dataclass(frozen=True)
class Comparison:
    equal: bool
    first_sha256: str
    second_sha256: str
    size: int


def compare_load_images(first: bytes, second: bytes) -> Comparison:
    first_image = load_image(first)
    second_image = load_image(second)
    return Comparison(
        equal=first_image == second_image,
        first_sha256=hashlib.sha256(first_image).hexdigest(),
        second_sha256=hashlib.sha256(second_image).hexdigest(),
        size=len(first_image),
    )
```

- [ ] **Step 3: Написать падающий интеграционный тест распаковщика**

```python
# tests/python/test_unpack_bumpy.py
import tempfile
import unittest
from pathlib import Path

from tools.re.mz import parse_mz
from tools.re.unpack_bumpy import unpack


class UnpackBumpyTest(unittest.TestCase):
    def test_unpacks_lz91_executable_deterministically(self):
        with tempfile.TemporaryDirectory() as directory:
            first = Path(directory) / "first.exe"
            second = Path(directory) / "second.exe"
            unpack(Path("BUMPY.EXE"), first)
            unpack(Path("BUMPY.EXE"), second)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertGreater(parse_mz(first.read_bytes()).file_size, 46019)
            self.assertNotEqual(first.read_bytes()[28:32], b"LZ91")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 4: Реализовать контролируемую обёртку первого распаковщика**

```python
# tools/re/unpack_bumpy.py
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def unpack(source: Path, output: Path) -> None:
    script = Path("tools/vendor/unpacklzexe/unpacklzexe.py")
    if not script.exists():
        raise RuntimeError("run tools/re/bootstrap_tools.ps1 first")
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(script), str(source), str(output)], check=True)


if __name__ == "__main__":
    unpack(Path("BUMPY.EXE"), Path("analysis/generated/BUMPY.UNPACKED.EXE"))
```

- [ ] **Step 5: Получить независимую распаковку официальным `UPACKEXE.EXE`**

Поместить официальный `UPACKEXE.EXE` из LZEXE 0.91 в
`tools/vendor/lzexe91/`, затем выполнить его через DOSBox-X:

```powershell
New-Item -ItemType Directory -Force analysis/generated/official | Out-Null
Copy-Item BUMPY.EXE analysis/generated/official/BUMPY.EXE
dosbox-x -fastlaunch -defaultconf -c "mount c analysis/generated/official" -c "mount d tools/vendor/lzexe91" -c "c:" -c "d:\UPACKEXE.EXE BUMPY.EXE" -c "exit"
Move-Item analysis/generated/official/BUMPY.EXE analysis/generated/BUMPY.OFFICIAL.EXE
python tools/re/unpack_bumpy.py
```

Expected: both `analysis/generated/BUMPY.UNPACKED.EXE` and
`analysis/generated/BUMPY.OFFICIAL.EXE` exist.

- [ ] **Step 6: Реализовать отчёт независимой проверки**

```python
# tools/re/validate_unpack.py
import json
from dataclasses import asdict
from pathlib import Path

from tools.re.compare_mz_images import compare_load_images

first = Path("analysis/generated/BUMPY.UNPACKED.EXE").read_bytes()
second = Path("analysis/generated/BUMPY.OFFICIAL.EXE").read_bytes()
comparison = compare_load_images(first, second)
if not comparison.equal:
    raise SystemExit("independent unpackers produced different MZ load images")
output = Path("analysis/reports/unpack-validation.json")
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(asdict(comparison), indent=2, sort_keys=True) + "\n", encoding="ascii")
```

- [ ] **Step 7: Запустить все проверки распаковки**

Run:

```powershell
python -m unittest tests.python.test_compare_mz_images tests.python.test_unpack_bumpy -v
python tools/re/validate_unpack.py
Get-Content analysis/reports/unpack-validation.json
```

Expected: tests pass; JSON contains `"equal": true`.

- [ ] **Step 8: Зафиксировать инструменты и отчёт**

Run:

```powershell
git add tools/re/unpack_bumpy.py tools/re/compare_mz_images.py tools/re/validate_unpack.py tests/python/test_compare_mz_images.py tests/python/test_unpack_bumpy.py analysis/reports/unpack-validation.json
git commit -m "feat: add independently validated LZEXE unpacking"
```

### Task 5: Создать Ghidra-базу и исходный каталог функций

**Files:**
- Create: `analysis/ghidra_scripts/ExportFunctions.py`
- Create: `analysis/catalog/functions.csv`
- Create: `analysis/catalog/globals.csv`

- [ ] **Step 1: Реализовать стабильный экспорт функций из Ghidra**

```python
# analysis/ghidra_scripts/ExportFunctions.py
# @category Bumpy
import csv

args = getScriptArgs()
if len(args) != 1:
    raise ValueError("expected output CSV path")

output = args[0]
functions = list(currentProgram.getFunctionManager().getFunctions(True))
with open(output, "w") as stream:
    writer = csv.writer(stream, lineterminator="\n")
    writer.writerow(["address", "name", "status", "evidence", "cpp_symbol"])
    for function in functions:
        writer.writerow([
            str(function.getEntryPoint()),
            function.getName(),
            "unknown",
            "ghidra initial analysis",
            "",
        ])
```

- [ ] **Step 2: Импортировать проверенный распакованный EXE**

Run:

```powershell
New-Item -ItemType Directory -Force analysis/generated/ghidra | Out-Null
analyzeHeadless analysis/generated/ghidra Bumpy -import analysis/generated/BUMPY.UNPACKED.EXE -processor "x86:LE:16:Real Mode" -analysisTimeoutPerFile 900 -scriptPath analysis/ghidra_scripts -postScript ExportFunctions.py "$((Resolve-Path .).Path)\analysis\catalog\functions.csv"
```

Expected: headless analysis finishes without timeout and
`analysis/catalog/functions.csv` contains more than the header row.

- [ ] **Step 3: Создать каталог глобальных данных с явной схемой**

```csv
address,name,size,status,evidence,cpp_symbol
```

Записать эту строку в `analysis/catalog/globals.csv`. Пустой каталог является
корректным: глобальные данные ещё не подтверждены.

- [ ] **Step 4: Проверить отсутствие незаполненных статусов**

Run:

```powershell
$rows = Import-Csv analysis/catalog/functions.csv
if ($rows.Count -eq 0) { throw "no functions exported" }
if ($rows.status -contains "") { throw "function without evidence status" }
```

Expected: command exits successfully.

- [ ] **Step 5: Зафиксировать экспортируемый каталог, но не Ghidra project**

Run:

```powershell
git add analysis/ghidra_scripts/ExportFunctions.py analysis/catalog/functions.csv analysis/catalog/globals.csv
git commit -m "feat: seed address-linked function catalog"
```

### Task 6: Настроить воспроизводимый эталонный запуск

**Files:**
- Create: `reference/dosbox-x.conf`
- Create: `reference/README.md`
- Create: `tools/reference/run_reference.ps1`

- [ ] **Step 1: Создать фиксированную конфигурацию DOSBox-X**

```ini
# reference/dosbox-x.conf
[dosbox]
machine=vgaonly
memsize=16

[cpu]
core=normal
cputype=386
cycles=fixed 3000

[render]
aspect=false
scaler=none

[mixer]
rate=44100
blocksize=1024
prebuffer=25

[midi]
mididevice=none

[autoexec]
mount c .
c:
```

- [ ] **Step 2: Реализовать запуск только после проверки оригиналов**

```powershell
# tools/reference/run_reference.ps1
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Push-Location $root
try {
    python tools/assets/manifest.py verify
    if ($LASTEXITCODE -ne 0) {
        throw "original asset verification failed"
    }
    dosbox-x -conf reference/dosbox-x.conf -c "BUMPY.EXE"
} finally {
    Pop-Location
}
```

- [ ] **Step 3: Документировать эталон и отладочный порядок**

```markdown
# Reference execution

Run `tools/reference/run_reference.ps1`. The script refuses to start when an
original file differs from `config/original-assets.sha256`.

The pinned reference uses VGA, a normal 386 core, and fixed 3000 cycles. Do not
change these values while collecting comparable evidence. Open the DOSBox-X
debugger with Alt+Pause. Record every breakpoint, observed address, input
sequence, and conclusion in the relevant catalog or report before marking a
function `confirmed`.
```

- [ ] **Step 4: Запустить оригинал и проверить меню вручную**

Run:

```powershell
& tools/reference/run_reference.ps1
```

Expected: DOSBox-X opens Bumpy's Arcade Fantasy and reaches its original menu.
Close DOSBox-X after the check.

- [ ] **Step 5: Зафиксировать эталонную конфигурацию**

Run:

```powershell
git add reference/dosbox-x.conf reference/README.md tools/reference/run_reference.ps1
git commit -m "build: add reproducible reference execution"
```

### Task 7: Создать C++20/CMake-каркас с SDL3 и Catch2

**Files:**
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`
- Create: `src/core/indexed_framebuffer.h`
- Create: `src/core/indexed_framebuffer.cpp`
- Create: `tests/cpp/indexed_framebuffer_test.cpp`

- [ ] **Step 1: Создать CMake-конфигурацию с закреплёнными зависимостями**

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.25)
project(bumpy_port VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(SDL
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG 8e37db5e797b6167f3a00d697d816a684bd259c7)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG b8acae26b0453dad6250447ad32f6b0015b162ae)
FetchContent_MakeAvailable(SDL Catch2)

add_library(bumpy_core
  src/core/indexed_framebuffer.cpp)
target_include_directories(bumpy_core PUBLIC src)

enable_testing()
add_executable(bumpy_tests
  tests/cpp/indexed_framebuffer_test.cpp)
target_link_libraries(bumpy_tests PRIVATE bumpy_core Catch2::Catch2WithMain)
add_test(NAME bumpy_tests COMMAND bumpy_tests)
```

```json
// CMakePresets.json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "windows-debug",
      "generator": "Visual Studio 18 2026",
      "binaryDir": "${sourceDir}/build/windows-debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "windows-debug",
      "configurePreset": "windows-debug",
      "configuration": "Debug"
    }
  ],
  "testPresets": [
    {
      "name": "windows-debug",
      "configurePreset": "windows-debug",
      "configuration": "Debug",
      "output": { "outputOnFailure": true }
    }
  ]
}
```

- [ ] **Step 2: Написать падающий тест индексного framebuffer**

```cpp
// tests/cpp/indexed_framebuffer_test.cpp
#include <catch2/catch_test_macros.hpp>
#include "core/indexed_framebuffer.h"

TEST_CASE("indexed framebuffer converts palette entries exactly") {
    bumpy::IndexedFramebuffer frame(2, 1);
    frame.set_palette(7, {0x12, 0x34, 0x56, 0xff});
    frame.pixel(0, 0) = 7;
    frame.pixel(1, 0) = 7;

    const auto rgba = frame.to_rgba();

    REQUIRE(rgba == std::vector<std::uint32_t>{0xff563412, 0xff563412});
}
```

- [ ] **Step 3: Запустить тест и подтвердить ожидаемое падение**

Run:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

Expected: build fails because `core/indexed_framebuffer.h` does not exist.

- [ ] **Step 4: Реализовать SDL-независимый индексный framebuffer**

```cpp
// src/core/indexed_framebuffer.h
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace bumpy {

struct Rgba {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
};

class IndexedFramebuffer {
public:
    IndexedFramebuffer(int width, int height);
    std::uint8_t& pixel(int x, int y);
    void set_palette(std::uint8_t index, Rgba color);
    [[nodiscard]] std::vector<std::uint32_t> to_rgba() const;
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }

private:
    int width_;
    int height_;
    std::vector<std::uint8_t> pixels_;
    std::array<Rgba, 256> palette_{};
};

}  // namespace bumpy
```

```cpp
// src/core/indexed_framebuffer.cpp
#include "core/indexed_framebuffer.h"

#include <stdexcept>

namespace bumpy {

IndexedFramebuffer::IndexedFramebuffer(int width, int height)
    : width_(width), height_(height) {
    if (width_ <= 0 || height_ <= 0) {
        throw std::invalid_argument("framebuffer dimensions must be positive");
    }
    pixels_.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_));
}

std::uint8_t& IndexedFramebuffer::pixel(int x, int y) {
    return pixels_.at(static_cast<std::size_t>(y * width_ + x));
}

void IndexedFramebuffer::set_palette(std::uint8_t index, Rgba color) {
    palette_[index] = color;
}

std::vector<std::uint32_t> IndexedFramebuffer::to_rgba() const {
    std::vector<std::uint32_t> result;
    result.reserve(pixels_.size());
    for (const auto index : pixels_) {
        const auto color = palette_[index];
        result.push_back(
            static_cast<std::uint32_t>(color.r) |
            (static_cast<std::uint32_t>(color.g) << 8) |
            (static_cast<std::uint32_t>(color.b) << 16) |
            (static_cast<std::uint32_t>(color.a) << 24));
    }
    return result;
}

}  // namespace bumpy
```

- [ ] **Step 5: Собрать и запустить тест**

Run:

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug
```

Expected: `bumpy_tests` passes.

- [ ] **Step 6: Зафиксировать C++-каркас**

Run:

```powershell
git add CMakeLists.txt CMakePresets.json src/core/indexed_framebuffer.h src/core/indexed_framebuffer.cpp tests/cpp/indexed_framebuffer_test.cpp
git commit -m "build: add C++20 SDL3 test scaffold"
```

### Task 8: Добавить runtime-проверку ресурсов и SDL3-окно

**Files:**
- Create: `src/core/asset_manifest.h`
- Create: `src/core/asset_manifest.cpp`
- Create: `src/platform_sdl3/sdl_app.h`
- Create: `src/platform_sdl3/sdl_app.cpp`
- Create: `src/app/main.cpp`
- Create: `tests/cpp/asset_manifest_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Написать падающий C++-тест манифеста**

```cpp
// tests/cpp/asset_manifest_test.cpp
#include <catch2/catch_test_macros.hpp>
#include "core/asset_manifest.h"

TEST_CASE("asset manifest recognizes the supplied BUMPY executable") {
    const auto manifest = bumpy::AssetManifest::load("config/original-assets.sha256");
    const auto result = manifest.verify(".");

    REQUIRE(result.missing.empty());
    REQUIRE(result.changed.empty());
    REQUIRE(result.file_count == 50);
}
```

- [ ] **Step 2: Расширить `CMakeLists.txt` для Windows CNG SHA-256 и новых целей**

```cmake
target_sources(bumpy_core PRIVATE
  src/core/asset_manifest.cpp)
target_link_libraries(bumpy_core PUBLIC bcrypt)

add_library(bumpy_platform_sdl3
  src/platform_sdl3/sdl_app.cpp)
target_include_directories(bumpy_platform_sdl3 PUBLIC src)
target_link_libraries(bumpy_platform_sdl3 PUBLIC bumpy_core SDL3::SDL3)

add_executable(bumpy_port src/app/main.cpp)
target_link_libraries(bumpy_port PRIVATE bumpy_platform_sdl3)

target_sources(bumpy_tests PRIVATE tests/cpp/asset_manifest_test.cpp)
```

- [ ] **Step 3: Реализовать `AssetManifest` с тем же контрактом, что Python-проверка**

```cpp
// src/core/asset_manifest.h
#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace bumpy {

struct AssetVerification {
    std::vector<std::string> missing;
    std::vector<std::string> changed;
    std::size_t file_count{};
};

class AssetManifest {
public:
    static AssetManifest load(const std::filesystem::path& path);
    [[nodiscard]] AssetVerification verify(const std::filesystem::path& root) const;

private:
    std::vector<std::pair<std::string, std::string>> entries_;
};

}  // namespace bumpy
```

```cpp
// src/core/asset_manifest.cpp
#include "core/asset_manifest.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {

void check(NTSTATUS status, const char* operation) {
    if (status < 0) {
        throw std::runtime_error(operation);
    }
}

std::string sha256_file(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0),
          "BCryptOpenAlgorithmProvider failed");
    try {
        check(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0),
              "BCryptCreateHash failed");
        std::ifstream source(path, std::ios::binary);
        if (!source) {
            throw std::runtime_error("cannot open asset: " + path.string());
        }
        std::array<char, 64 * 1024> buffer{};
        while (source) {
            source.read(buffer.data(), buffer.size());
            const auto count = static_cast<ULONG>(source.gcount());
            if (count != 0) {
                check(BCryptHashData(
                          hash, reinterpret_cast<PUCHAR>(buffer.data()), count, 0),
                      "BCryptHashData failed");
            }
        }
        std::array<UCHAR, 32> digest{};
        check(BCryptFinishHash(hash, digest.data(), digest.size(), 0),
              "BCryptFinishHash failed");
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);

        std::ostringstream text;
        text << std::hex << std::setfill('0');
        for (const auto byte : digest) {
            text << std::setw(2) << static_cast<unsigned>(byte);
        }
        return text.str();
    } catch (...) {
        if (hash) {
            BCryptDestroyHash(hash);
        }
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw;
    }
}

}  // namespace

namespace bumpy {

AssetManifest AssetManifest::load(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open asset manifest: " + path.string());
    }
    AssetManifest result;
    std::string line;
    while (std::getline(input, line)) {
        if (line.size() < 67 || line.substr(64, 2) != "  ") {
            throw std::runtime_error("invalid asset manifest line");
        }
        result.entries_.emplace_back(line.substr(0, 64), line.substr(66));
    }
    return result;
}

AssetVerification AssetManifest::verify(const std::filesystem::path& root) const {
    AssetVerification result;
    result.file_count = entries_.size();
    for (const auto& [expected, name] : entries_) {
        const auto path = root / name;
        if (!std::filesystem::exists(path)) {
            result.missing.push_back(name);
        } else if (sha256_file(path) != expected) {
            result.changed.push_back(name);
        }
    }
    std::sort(result.missing.begin(), result.missing.end());
    std::sort(result.changed.begin(), result.changed.end());
    return result;
}

}  // namespace bumpy
```

- [ ] **Step 4: Запустить C++-тест и убедиться, что проверка совпадает с Python**

Run:

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug
python tools/assets/manifest.py verify
```

Expected: both verifiers succeed and C++ reports `file_count == 50`.

- [ ] **Step 5: Реализовать тонкий SDL3-адаптер**

```cpp
// src/platform_sdl3/sdl_app.h
#pragma once

#include <SDL3/SDL.h>

#include "core/indexed_framebuffer.h"

namespace bumpy {

class SdlApp {
public:
    SdlApp();
    ~SdlApp();
    SdlApp(const SdlApp&) = delete;
    SdlApp& operator=(const SdlApp&) = delete;
    int run(IndexedFramebuffer& frame);

private:
    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    SDL_Texture* texture_{};
};

}  // namespace bumpy
```

```cpp
// src/platform_sdl3/sdl_app.cpp
#include "platform_sdl3/sdl_app.h"

#include <stdexcept>

namespace {

void require(bool ok) {
    if (!ok) {
        throw std::runtime_error(SDL_GetError());
    }
}

}  // namespace

namespace bumpy {

SdlApp::SdlApp() {
    require(SDL_Init(SDL_INIT_VIDEO));
    window_ = SDL_CreateWindow("Bumpy accurate port", 960, 600, 0);
    if (!window_) {
        SDL_Quit();
        throw std::runtime_error(SDL_GetError());
    }
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        SDL_DestroyWindow(window_);
        SDL_Quit();
        throw std::runtime_error(SDL_GetError());
    }
    texture_ = SDL_CreateTexture(
        renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 320, 200);
    if (!texture_) {
        SDL_DestroyRenderer(renderer_);
        SDL_DestroyWindow(window_);
        SDL_Quit();
        throw std::runtime_error(SDL_GetError());
    }
    require(SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST));
}

SdlApp::~SdlApp() {
    SDL_DestroyTexture(texture_);
    SDL_DestroyRenderer(renderer_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
}

int SdlApp::run(IndexedFramebuffer& frame) {
    bool running = true;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            }
        }

        const auto rgba = frame.to_rgba();
        require(SDL_UpdateTexture(
            texture_, nullptr, rgba.data(), frame.width() * sizeof(std::uint32_t)));
        require(SDL_RenderClear(renderer_));
        require(SDL_RenderTexture(renderer_, texture_, nullptr, nullptr));
        SDL_RenderPresent(renderer_);
    }
    return 0;
}

}  // namespace bumpy
```

- [ ] **Step 6: Реализовать композицию приложения**

```cpp
// src/app/main.cpp
#include <exception>
#include <iostream>

#include <SDL3/SDL_main.h>

#include "core/asset_manifest.h"
#include "core/indexed_framebuffer.h"
#include "platform_sdl3/sdl_app.h"

int main() {
    try {
        const auto verification =
            bumpy::AssetManifest::load("config/original-assets.sha256").verify(".");
        if (!verification.missing.empty() || !verification.changed.empty()) {
            std::cerr << "Original Bumpy assets are missing or changed\n";
            return 2;
        }

        bumpy::IndexedFramebuffer frame(320, 200);
        frame.set_palette(0, {0, 0, 0, 255});
        frame.set_palette(1, {255, 255, 255, 255});
        for (int x = 0; x < frame.width(); ++x) {
            frame.pixel(x, 0) = 1;
            frame.pixel(x, frame.height() - 1) = 1;
        }
        for (int y = 0; y < frame.height(); ++y) {
            frame.pixel(0, y) = 1;
            frame.pixel(frame.width() - 1, y) = 1;
        }

        bumpy::SdlApp app;
        return app.run(frame);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

- [ ] **Step 7: Собрать, протестировать и вручную проверить окно**

Run:

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug
& build/windows-debug/Debug/bumpy_port.exe
```

Expected: all tests pass; a 3x-scaled black 320x200 image with a one-pixel white
border opens and Escape closes it. Оригинальные файлы во время проверки не
изменяются.

- [ ] **Step 8: Зафиксировать runtime-каркас**

Run:

```powershell
git add CMakeLists.txt src/core/asset_manifest.h src/core/asset_manifest.cpp src/platform_sdl3/sdl_app.h src/platform_sdl3/sdl_app.cpp src/app/main.cpp tests/cpp/asset_manifest_test.cpp
git commit -m "feat: launch validated SDL3 runtime shell"
```

### Task 9: Добавить единую проверку рубежа

**Files:**
- Create: `tools/verify.ps1`
- Modify: `analysis/README.md`

- [ ] **Step 1: Реализовать одну команду проверки**

```powershell
# tools/verify.ps1
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Push-Location $root
try {
    python tools/assets/manifest.py verify
    python -m unittest discover -s tests/python -v
    python tools/re/validate_unpack.py
    cmake --preset windows-debug
    cmake --build --preset windows-debug
    ctest --preset windows-debug

    $functions = Import-Csv analysis/catalog/functions.csv
    if ($functions.Count -eq 0) {
        throw "Ghidra function catalog is empty"
    }
} finally {
    Pop-Location
}
```

- [ ] **Step 2: Дополнить `analysis/README.md` командой рубежа**

Добавить:

```markdown
## Foundation milestone verification

Run `tools/verify.ps1`. A successful run proves asset integrity, deterministic
and independently checked unpacking, non-empty address-linked analysis output,
and a passing native C++/SDL3 build.
```

- [ ] **Step 3: Запустить полную проверку с чистой сборкой**

Run:

```powershell
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
& tools/verify.ps1
git status --short
```

Expected: all checks pass; `git status --short` contains no tracked changes and
does not list original game files.

- [ ] **Step 4: Зафиксировать проверку рубежа**

Run:

```powershell
git add tools/verify.ps1 analysis/README.md
git commit -m "test: verify reverse engineering foundation"
```

## Следующий план

После прохождения `tools/verify.ps1` создать отдельный план
`resource-formats-and-menu`, который начинается с восстановления функций
открытия/чтения файлов из каталога Ghidra, специфицирует `.VEC` и необходимые
для меню ресурсы, а завершается пиксельно сравнимым исходным меню в SDL3.
