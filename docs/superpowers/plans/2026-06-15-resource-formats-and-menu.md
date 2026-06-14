# Resource Formats and Accurate Menu Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Recover the original VGA game-menu path and its resource formats from the DOS binary, then render and operate pixel-comparable menu states through the native SDL3 runtime.

**Architecture:** A reproducible reverse-engineering lane exports deterministic Ghidra evidence and bounded DOSBox-X reference captures before any format or behavior is ported. SDL-independent C++ modules then read the original files, decode the confirmed formats, compose indexed 320x200 menu frames, and update a pure menu state from original-style input signals. SDL3 remains a thin adapter for keyboard events and presentation; Python and PowerShell tools compare reference and native artifacts and verify the milestone.

**Tech Stack:** Python 3.14 standard library, PowerShell 7, Ghidra 12.1.2, DOSBox-X 2026.06.02, C++20, CMake 4.2+, MSVC 14.50+, Catch2 3.15.0, SDL 3.4.10.

---

## Scope and Accuracy Contract

The native port starts directly on the original VGA game-menu path. It does
not reproduce the DOS startup EGA/VGA selector because EGA support is outside
the target platform's requirements. The reference harness still sends the
original VGA-selection input as setup before approved captures, so the
resource-backed menu remains tied to the original execution path.

The milestone is complete only when:

- original file-open, seek, read, and close routines on the menu path have
  confirmed addresses and evidence;
- the exact resource list and complete formats used before level loading are
  documented without unknown fields;
- every menu resource is decoded directly from the supplied original files;
- menu composition, palette, cursor positions, accepted keys, and transitions
  are tied to binary/reference evidence;
- native reference-dump mode produces deterministic indexed 320x200 frames;
- automated comparison reports zero differing pixels for approved menu
  checkpoints;
- `tools/verify-menu.ps1` passes without modifying original files.

No screenshot may be used to infer missing resource bytes, drawing commands,
palette entries, hit boxes, or behavior. Screenshots are comparison evidence
only.

## File Structure

```text
analysis/
  catalog/functions.csv                   # Confirmed menu-path function names/evidence
  catalog/globals.csv                     # Confirmed menu globals and tables
  ghidra_scripts/ExportMenuEvidence.py    # Deterministic strings/xrefs/calls export
  reports/menu-path.json                  # Curated address-linked menu call path
  reports/menu-resources.json             # Exact resource inventory and decoded facts
  reports/menu-reference.json             # Inputs and hashes for approved checkpoints
  specs/menu-resource-formats.md          # Complete byte-level resource specifications
  specs/menu-behavior.md                  # Palette, composition, input, and transitions
reference/
  menu-sequence.json                      # Bounded reference inputs/checkpoints
src/
  app/main.cpp                            # Runtime composition and dump CLI
  core/indexed_framebuffer.{h,cpp}        # Indexed pixels/palette access and clear
  game/menu.{h,cpp}                       # SDL-independent menu state and rendering
  resources/binary_reader.{h,cpp}         # Bounds-checked little-endian reads
  resources/menu_resources.{h,cpp}        # Typed menu resource bundle
  resources/vec.{h,cpp}                   # Confirmed VEC decoder
  video/menu_renderer.{h,cpp}             # Confirmed draw-command interpreter
  platform_sdl3/sdl_app.{h,cpp}           # Keyboard-to-menu-input adapter
tests/
  cpp/binary_reader_test.cpp
  cpp/vec_test.cpp
  cpp/menu_resources_test.cpp
  cpp/menu_renderer_test.cpp
  cpp/menu_test.cpp
  python/test_menu_evidence.py
  python/test_menu_reference.py
  python/test_compare_frames.py
tools/
  re/menu_evidence.py                     # Validate/publish curated RE evidence
  re/probe_menu_resources.py              # Deterministic structural resource report
  reference/run_menu_sequence.ps1         # Drive and capture original checkpoints
  reference/capture_client.ps1            # Normalize captures to 320x200 BMP
  compare_frames.py                       # Exact RGB comparison and diff artifact
  verify-menu.ps1                         # One-command menu milestone verification
```

Generated Ghidra exports, raw captures, native frame dumps, and visual diffs
belong under ignored `analysis/generated/`.

### Task 1: Export and Confirm the Original File I/O and Menu Call Path

**Files:**
- Create: `analysis/ghidra_scripts/ExportMenuEvidence.py`
- Create: `tools/re/menu_evidence.py`
- Create: `tests/python/test_menu_evidence.py`
- Create: `analysis/reports/menu-path.json`
- Modify: `tools/re/run_ghidra_analysis.ps1`
- Modify: `analysis/catalog/functions.csv`
- Modify: `analysis/catalog/globals.csv`
- Modify: `analysis/README.md`

- [ ] **Step 1: Write failing tests for a complete address-linked menu report**

Create `tests/python/test_menu_evidence.py` with tests that require:

```python
REQUIRED_ROLES = {
    "dos_open",
    "dos_seek",
    "dos_read",
    "dos_close",
    "game_menu",
}
```

The repository report must contain one confirmed segmented address per role,
at least one static and one dynamic evidence item for every DOS file routine,
the complete caller chain from `entry` to `game_menu`, and every referenced
menu resource name. Every reported function/global address must exist in the
corresponding catalog with status `confirmed`.

- [ ] **Step 2: Run the tests and verify the report is absent**

Run:

```powershell
python -m unittest tests.python.test_menu_evidence -v
```

Expected: FAIL because `analysis/reports/menu-path.json` and
`tools.re.menu_evidence` do not exist.

- [ ] **Step 3: Add a deterministic Ghidra evidence exporter**

Implement `ExportMenuEvidence.py` to emit ASCII JSON containing:

- all defined strings matching supplied original filenames or menu text;
- the address and raw bytes of each matching string;
- every code/data reference to those strings;
- containing functions for references;
- direct callers and callees for those functions;
- decompiler text and instruction listings for the resulting transitive
  candidate set.

Sort every array by segmented address and write atomically. Pass the output
path and search strings as script arguments; do not hard-code discovered
addresses.

- [ ] **Step 4: Integrate the exporter into both clean Ghidra analyses**

Extend `tools/re/run_ghidra_analysis.ps1` so both clean projects export menu
evidence and the wrapper rejects differing evidence hashes. Publish the agreed
raw export only under `analysis/generated/ghidra-clean-*/`; the curated report
remains tracked separately.

Run:

```powershell
& tools/re/run_ghidra_analysis.ps1
```

Expected: both clean analyses report identical menu-evidence SHA-256 values.

- [ ] **Step 5: Recover and confirm the file routines first**

Start from references to `TITRE.VEC`, `FLECHE.BIN`, `BUMPRESE.VEC`,
`MASKBUMP.VEC`, and the other filename strings exported in Step 4.

For each candidate:

1. identify the DOS interrupt or runtime wrapper semantics in the listing;
2. record parameters, return values, failure behavior, and direct callers;
3. place a DOSBox-X debugger breakpoint at the candidate address;
4. select VGA and continue until the first game menu;
5. record observed filename, offset/count, result, and caller address.

Do not mark a routine confirmed from naming or static analysis alone.

- [ ] **Step 6: Recover the VGA game-menu root**

Trace the caller chain from `entry` through the original VGA-selection branch
into the resource-backed game menu. Record the exact branch/input that selects
VGA, the menu loop address, all menu-path globals, and every resource opened
before level loading. The selector itself is evidence/setup only and is not a
native-port feature.

- [ ] **Step 7: Publish and validate curated evidence**

Implement `tools/re/menu_evidence.py` to validate and atomically publish this
schema:

```json
{
  "schema_version": 1,
  "roles": {
    "dos_open": {"address": "ssss:oooo", "static_evidence": [], "dynamic_evidence": []},
    "dos_seek": {"address": "ssss:oooo", "static_evidence": [], "dynamic_evidence": []},
    "dos_read": {"address": "ssss:oooo", "static_evidence": [], "dynamic_evidence": []},
    "dos_close": {"address": "ssss:oooo", "static_evidence": [], "dynamic_evidence": []},
    "game_menu": {"address": "ssss:oooo", "static_evidence": [], "dynamic_evidence": []}
  },
  "entry_to_game_menu": [],
  "globals": [],
  "resource_names": []
}
```

Here `ssss:oooo` describes the validated format, not a value to copy. The
published report must contain the actual discovered addresses and evidence.
Update both catalogs with meaningful names, `confirmed` status, evidence
summaries, and future C++ symbols where applicable.

- [ ] **Step 8: Run tests and commit the recovered call path**

Run:

```powershell
python -m unittest tests.python.test_menu_evidence tests.python.test_ghidra_catalog -v
git add analysis/ghidra_scripts/ExportMenuEvidence.py tools/re/menu_evidence.py tests/python/test_menu_evidence.py analysis/reports/menu-path.json analysis/catalog/functions.csv analysis/catalog/globals.csv analysis/README.md tools/re/run_ghidra_analysis.ps1
git commit -m "re: confirm menu file IO and call path"
```

Expected: tests pass; every required role is confirmed and no resource name in
the report is inferred from screenshots.

### Task 2: Build Deterministic Original-Menu Reference Captures

**Files:**
- Create: `reference/menu-sequence.json`
- Create: `tools/reference/capture_client.ps1`
- Create: `tools/reference/run_menu_sequence.ps1`
- Create: `tests/python/test_menu_reference.py`
- Create: `analysis/reports/menu-reference.json`
- Modify: `reference/README.md`

- [ ] **Step 1: Write failing tests for bounded menu checkpoints**

Require these named checkpoints in `reference/menu-sequence.json`:

```text
game-menu-idle
game-menu-cursor-next
game-menu-cursor-wrap
```

Each checkpoint must specify the exact preceding key transitions, settle
duration or observed breakpoint, a 320x200 capture path, and a SHA-256. Tests
must reject duplicate names, unbounded waits, non-320x200 captures, or paths
outside `analysis/generated/reference/menu/`. The sequence setup must select
VGA before the first checkpoint, but the selector is not captured or approved.

- [ ] **Step 2: Run the tests and verify they fail**

Run:

```powershell
python -m unittest tests.python.test_menu_reference -v
```

Expected: FAIL because the sequence and capture tools do not exist.

- [ ] **Step 3: Implement native-client capture normalization**

Implement `capture_client.ps1` using Win32 client-rectangle coordinates and
`System.Drawing`. Capture only the DOSBox-X client surface, reject unexpected
aspect ratios, and normalize a nearest-neighbor integer-scaled surface to a
24-bit 320x200 BMP. Never include title bar, menu bar, borders, or scaling
interpolation.

- [ ] **Step 4: Implement the bounded reference sequence runner**

Reuse the pinned archive/hash/version checks and disposable run directory from
`run_reference.ps1`. Drive key-down/key-up transitions from
`reference/menu-sequence.json`, capture every checkpoint, and terminate the
process in `finally`. Verify original assets before and after the run.

- [ ] **Step 5: Capture and approve the three reference states**

Run:

```powershell
& tools/reference/run_menu_sequence.ps1 -PublishReport analysis/reports/menu-reference.json
python -m unittest tests.python.test_menu_reference -v
```

Expected: three normalized 320x200 captures are produced; the report records
input transitions and hashes; the original asset manifest passes after exit.

- [ ] **Step 6: Document evidence limits and commit**

Document that captures prove observable pixels and transitions but do not
prove resource formats or algorithms. Commit the sequence, scripts, report,
tests, and docs:

```powershell
git add reference/menu-sequence.json tools/reference/capture_client.ps1 tools/reference/run_menu_sequence.ps1 tests/python/test_menu_reference.py analysis/reports/menu-reference.json reference/README.md
git commit -m "test: capture deterministic original menu states"
```

### Task 3: Recover and Specify Every Menu Resource Format

**Files:**
- Create: `tools/re/probe_menu_resources.py`
- Create: `tests/python/test_menu_resources_report.py`
- Create: `analysis/reports/menu-resources.json`
- Create: `analysis/specs/menu-resource-formats.md`
- Create: `analysis/specs/menu-behavior.md`
- Modify: `analysis/catalog/functions.csv`
- Modify: `analysis/catalog/globals.csv`

- [ ] **Step 1: Write failing coverage tests for the resource report**

The test must load `resource_names` from `menu-path.json` and require exactly
one entry per name in `menu-resources.json`. Every entry must include size,
SHA-256, loader function address, format name, field/command coverage, decoded
record count, and terminal offset equal to file size. Reject `unknown`,
`hypothesis`, `TBD`, trailing bytes, and silent unsupported commands.

- [ ] **Step 2: Run the tests and verify they fail**

Run:

```powershell
python -m unittest tests.python.test_menu_resources_report -v
```

Expected: FAIL because the report and specifications do not exist.

- [ ] **Step 3: Implement a deterministic structural probe**

Implement `probe_menu_resources.py` to read only names from the confirmed
menu-path report, verify their manifest hashes, and emit stable facts useful
during analysis: byte length, prefix/suffix hex, repeated offsets/words,
candidate little-endian values, and exact byte-frequency counts. The probe
must not claim format semantics.

- [ ] **Step 4: Recover each loader and format from code and traces**

For each confirmed resource:

1. follow its filename xref to the loader;
2. recover each read size, loop, command dispatch, field width, signedness,
   terminator, and error path;
3. validate the interpretation against every supplied file sharing that
   format;
4. use watchpoints/breakpoints to connect decoded values to menu drawing;
5. update function/global catalogs with confirmed names and evidence.

`.VEC` is mandatory. Add other formats only when the confirmed menu path opens
them; do not pull level-only `.BUM`, `.DEC`, or `.PAV` behavior into this
milestone.

- [ ] **Step 5: Write complete byte-level specifications**

For each format, `menu-resource-formats.md` must define:

- byte order and integer signedness;
- header and record layouts with exact offsets and widths;
- command values and payloads;
- termination and end-of-file rules;
- validation/error behavior observed in the original;
- worked hex examples from supplied resources;
- loader/function addresses and evidence.

`menu-behavior.md` must define the exact palette source/transformation, draw
order, coordinates, transparency/mask rules, cursor states, accepted keys,
repeat behavior, wrap behavior, and transitions before level loading.

- [ ] **Step 6: Publish a complete machine-readable report**

Run:

```powershell
python tools/re/probe_menu_resources.py --publish analysis/reports/menu-resources.json
python -m unittest tests.python.test_menu_resources_report tests.python.test_menu_evidence -v
```

Expected: every confirmed menu resource is fully consumed and classified; no
unknown command or trailing byte remains.

- [ ] **Step 7: Commit the recovered formats and behavior**

```powershell
git add tools/re/probe_menu_resources.py tests/python/test_menu_resources_report.py analysis/reports/menu-resources.json analysis/specs/menu-resource-formats.md analysis/specs/menu-behavior.md analysis/catalog/functions.csv analysis/catalog/globals.csv
git commit -m "re: specify menu resources and behavior"
```

### Task 4: Add Bounds-Checked Resource Reads and Framebuffer Inspection

**Files:**
- Create: `src/resources/binary_reader.h`
- Create: `src/resources/binary_reader.cpp`
- Create: `tests/cpp/binary_reader_test.cpp`
- Modify: `src/core/indexed_framebuffer.h`
- Modify: `src/core/indexed_framebuffer.cpp`
- Modify: `tests/cpp/indexed_framebuffer_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing C++ tests**

Test exact little-endian `u8`, `u16`, `i16`, byte-span, seek, and offset
behavior. Require every failure message to contain the resource name and
failing hexadecimal offset. Add framebuffer tests for `clear`, read-only
indexed pixels, and read-only palette access.

- [ ] **Step 2: Run the tests and verify they fail**

Run:

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug
```

Expected: build fails because `resources/binary_reader.h` and new framebuffer
APIs do not exist.

- [ ] **Step 3: Implement the minimal stable APIs**

Use these public contracts:

```cpp
class BinaryReader {
public:
    static BinaryReader from_file(const std::filesystem::path& path);
    std::uint8_t u8();
    std::uint16_t u16_le();
    std::int16_t i16_le();
    std::span<const std::uint8_t> bytes(std::size_t count);
    void seek(std::size_t offset);
    [[nodiscard]] std::size_t offset() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
};
```

```cpp
void clear(std::uint8_t color);
[[nodiscard]] std::span<const std::uint8_t> pixels() const noexcept;
[[nodiscard]] const std::array<Rgba, 256>& palette() const noexcept;
```

Use checked arithmetic before every range conversion/read. Do not use packed
C++ structs to parse original bytes.

- [ ] **Step 4: Build, test, and commit**

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug
git add CMakeLists.txt src/resources/binary_reader.h src/resources/binary_reader.cpp tests/cpp/binary_reader_test.cpp src/core/indexed_framebuffer.h src/core/indexed_framebuffer.cpp tests/cpp/indexed_framebuffer_test.cpp
git commit -m "feat: add checked resource reading primitives"
```

### Task 5: Implement Confirmed Menu Resource Decoders

**Files:**
- Create: `src/resources/vec.h`
- Create: `src/resources/vec.cpp`
- Create: `src/resources/menu_resources.h`
- Create: `src/resources/menu_resources.cpp`
- Create: `tests/cpp/vec_test.cpp`
- Create: `tests/cpp/menu_resources_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing decoder tests from the approved report**

Tests must load every confirmed menu resource and compare independently
observable decoder results with `menu-resources.json`: complete byte
consumption, record/command counts, dimensions/bounds, and deterministic
decoded-command SHA-256. Add one malformed/truncated fixture per recovered
format and assert filename plus failing offset diagnostics.

- [ ] **Step 2: Run the tests and verify they fail**

Run:

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug
```

Expected: build fails because the decoder headers do not exist.

- [ ] **Step 3: Define typed decoded data without rendering concerns**

`vec.h` must expose a variant of the exact command records documented in
`menu-resource-formats.md`; names and field types must mirror confirmed
semantics. `menu_resources.h` must expose one typed field per resource role
from `menu-path.json`, not per hard-coded screenshot layer.

- [ ] **Step 4: Transcribe the confirmed loader algorithms**

Implement each decoder in the same command/field order as the documented
original loader. Preserve signed conversions, wrap behavior, ignored values,
and rejection behavior. Every branch must correspond to a documented command
or original error path; unsupported values throw with file/offset diagnostics.

- [ ] **Step 5: Verify all supplied same-format files**

Tests must decode every supplied `.VEC` when the recovered VEC loader accepts
the family, even if only a subset is used by the menu. This proves the format
implementation is not overfit to menu screenshots.

- [ ] **Step 6: Build, test, and commit**

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug
git add CMakeLists.txt src/resources/vec.h src/resources/vec.cpp src/resources/menu_resources.h src/resources/menu_resources.cpp tests/cpp/vec_test.cpp tests/cpp/menu_resources_test.cpp
git commit -m "feat: decode original menu resources"
```

### Task 6: Render the Confirmed Menu Composition into Indexed Pixels

**Files:**
- Create: `src/video/menu_renderer.h`
- Create: `src/video/menu_renderer.cpp`
- Create: `tests/cpp/menu_renderer_test.cpp`
- Modify: `src/core/indexed_framebuffer.h`
- Modify: `src/core/indexed_framebuffer.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing renderer tests**

Use small synthetic decoded commands to test clipping, signed coordinates,
draw order, transparency/mask behavior, and palette conversion. Add an
integration test that renders the confirmed idle menu and compares indexed
pixel SHA-256 plus palette SHA-256 with `menu-resources.json`.

- [ ] **Step 2: Run the tests and verify they fail**

Run:

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug
```

Expected: build fails because `video/menu_renderer.h` does not exist.

- [ ] **Step 3: Implement the renderer as a pure command interpreter**

Use this boundary:

```cpp
class MenuRenderer {
public:
    explicit MenuRenderer(const MenuResources& resources);
    void render(const MenuView& view, IndexedFramebuffer& target) const;
};
```

Implement only primitives proven in `menu-behavior.md`. Keep coordinates and
intermediate arithmetic in the confirmed integer widths. The renderer must
not read files, poll SDL, or mutate menu state.

- [ ] **Step 4: Build, test, and commit**

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug
git add CMakeLists.txt src/video/menu_renderer.h src/video/menu_renderer.cpp tests/cpp/menu_renderer_test.cpp src/core/indexed_framebuffer.h src/core/indexed_framebuffer.cpp
git commit -m "feat: render confirmed indexed menu composition"
```

### Task 7: Implement the SDL-Independent Menu State and Input Semantics

**Files:**
- Create: `src/game/menu.h`
- Create: `src/game/menu.cpp`
- Create: `tests/cpp/menu_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing state-transition tests**

Translate every approved sequence from `menu-reference.json` into C++ tests.
Assert initial selection, cursor movement, wrap behavior, ignored keys,
key-down/key-up handling, repeat timing if confirmed, and the action emitted
when the first playable selection is accepted.

- [ ] **Step 2: Run the tests and verify they fail**

Run:

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug
```

Expected: build fails because `game/menu.h` does not exist.

- [ ] **Step 3: Implement the pure menu state machine**

Use original-style input signals rather than SDL key codes:

```cpp
struct MenuInput {
    bool up{};
    bool down{};
    bool left{};
    bool right{};
    bool confirm{};
    bool cancel{};
};

enum class MenuAction { none, start_first_level, quit };
```

The menu state owns selection/timing variables, exposes an immutable
`MenuView`, and returns `MenuAction` from a fixed update step. Preserve the
confirmed update order and integer behavior.

- [ ] **Step 4: Build, test, and commit**

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug
git add CMakeLists.txt src/game/menu.h src/game/menu.cpp tests/cpp/menu_test.cpp
git commit -m "feat: reproduce original menu state transitions"
```

### Task 8: Integrate Menu Input, Runtime Rendering, and Deterministic Dumps

**Files:**
- Modify: `src/platform_sdl3/sdl_app.h`
- Modify: `src/platform_sdl3/sdl_app.cpp`
- Modify: `src/app/main.cpp`
- Create: `tests/python/test_native_menu_dump.py`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write a failing native dump integration test**

Run `bumpy_port.exe --dump-menu-sequence reference/menu-sequence.json
analysis/generated/native/menu` and require three 320x200 BMP files plus a
stable JSON report. The process must be headless/bounded and must not require
an SDL window.

- [ ] **Step 2: Run the test and verify it fails**

Run:

```powershell
python -m unittest tests.python.test_native_menu_dump -v
```

Expected: FAIL because the dump CLI is not implemented.

- [ ] **Step 3: Add SDL keyboard translation**

Translate SDL events into `MenuInput` only in `platform_sdl3`. Preserve
separate key-down/key-up state and ignore SDL repeat unless the confirmed
original behavior requires it. Keep Escape/quit handling consistent with
`menu-behavior.md`.

- [ ] **Step 4: Replace the border demo with the menu runtime**

At startup, verify the original manifest, load `MenuResources`, initialize
the confirmed VGA game-menu state directly, render through `MenuRenderer`, and
present through `SdlApp`.
Do not introduce level loading in this milestone; report
`start_first_level` as the terminal successful menu action.

- [ ] **Step 5: Add deterministic dump mode**

Dump mode must replay the same logical input sequence without SDL timing,
render each checkpoint to a simple 24-bit BMP, and emit input/frame hashes.
Reuse the production resource decoder, menu state, renderer, and framebuffer.

- [ ] **Step 6: Build, test, manually inspect, and commit**

Run:

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug
python -m unittest tests.python.test_native_menu_dump -v
& build/windows-debug/Debug/bumpy_port.exe
```

Expected: automated tests pass; the runtime reaches the resource-backed menu,
responds according to the confirmed input contract, and exits without loading
a level.

```powershell
git add CMakeLists.txt src/platform_sdl3/sdl_app.h src/platform_sdl3/sdl_app.cpp src/app/main.cpp tests/python/test_native_menu_dump.py
git commit -m "feat: run accurate menu through SDL3"
```

### Task 9: Compare Original and Native Menu Frames Exactly

**Files:**
- Create: `tools/compare_frames.py`
- Create: `tests/python/test_compare_frames.py`
- Modify: `analysis/reports/menu-reference.json`
- Modify: `reference/README.md`

- [ ] **Step 1: Write failing comparator tests**

Create synthetic 24-bit BMP fixtures in a temporary directory. Tests must
prove that the comparator:

- accepts identical 320x200 frames;
- reports exact differing-pixel count and first coordinate;
- rejects dimensions other than 320x200;
- emits a deterministic visual diff BMP;
- rejects alpha/scaling/tolerance options.

- [ ] **Step 2: Run tests and verify they fail**

Run:

```powershell
python -m unittest tests.python.test_compare_frames -v
```

Expected: FAIL because `tools.compare_frames` does not exist.

- [ ] **Step 3: Implement strict BMP comparison**

Use Python standard library only. Parse the bounded 24-bit uncompressed BMP
format produced by both capture paths, normalize bottom-up row order, and
compare exact RGB triples. Exit nonzero for any mismatch and print checkpoint,
differing count, maximum channel delta, and first differing coordinate.

- [ ] **Step 4: Compare every approved checkpoint**

Run:

```powershell
& tools/reference/run_menu_sequence.ps1 -PublishReport analysis/reports/menu-reference.json
& build/windows-debug/Debug/bumpy_port.exe --dump-menu-sequence reference/menu-sequence.json analysis/generated/native/menu
python tools/compare_frames.py --reference analysis/generated/reference/menu --actual analysis/generated/native/menu --diff analysis/generated/menu-diff
```

Expected: all three checkpoints report `0 differing pixels`. Any nonzero result
must be investigated through binary/resource evidence; do not add tolerance.

- [ ] **Step 5: Commit comparator and approved evidence**

```powershell
git add tools/compare_frames.py tests/python/test_compare_frames.py analysis/reports/menu-reference.json reference/README.md
git commit -m "test: prove pixel exact menu checkpoints"
```

### Task 10: Add One-Command Menu Verification and Update Handoff

**Files:**
- Create: `tools/verify-menu.ps1`
- Modify: `tools/verify.ps1`
- Modify: `analysis/README.md`
- Modify: `PROJECT_STATUS.md`

- [ ] **Step 1: Implement the menu milestone verifier**

`tools/verify-menu.ps1` must:

1. verify original assets;
2. run the complete Python suite;
3. validate `menu-path.json` and all resource/spec coverage;
4. configure/build and run all C++ tests;
5. run the bounded original menu sequence;
6. run native dump mode;
7. require zero differing pixels at every checkpoint;
8. verify original assets again;
9. reject tracked worktree changes caused by verification.

Use the checked-command pattern from `tools/verify.ps1`; always stop the
reference process and restore location in `finally`.

- [ ] **Step 2: Make the main verifier include the completed milestone**

After `verify-menu.ps1` is stable, have `tools/verify.ps1` invoke it rather
than duplicating menu checks. Preserve foundation validation, including unpack
validation and non-empty Ghidra catalog checks.

- [ ] **Step 3: Run a clean full verification**

Run:

```powershell
Remove-Item -LiteralPath build -Recurse -Force -ErrorAction SilentlyContinue
& tools/verify.ps1
git status --short --branch
```

Expected: asset verification passes before and after reference execution; all
Python and C++ tests pass; all approved checkpoints have zero differing
pixels; tracked worktree is clean on the feature branch.

- [ ] **Step 4: Update status and commit**

Record confirmed addresses, resource formats, checkpoint hashes, test counts,
and the next milestone in `PROJECT_STATUS.md` and `analysis/README.md`.

```powershell
git add tools/verify-menu.ps1 tools/verify.ps1 analysis/README.md PROJECT_STATUS.md
git commit -m "test: verify resource formats and accurate menu"
```

## Plan Self-Review

- **Required outcome coverage:** Tasks 1 and 3 recover file routines, exact
  resources, formats, palette, composition, cursor, and input. Tasks 4-8
  implement SDL-independent decoding/menu behavior and SDL3 integration.
  Tasks 2 and 9 provide deterministic reference capture and exact comparison.
  Task 10 provides one-command verification.
- **Scope:** Level formats, physics, gameplay, audio, and level loading remain
  outside this milestone. The menu may emit `start_first_level`, but no level
  is loaded.
- **Evidence boundary:** Discovered addresses and format values are outputs of
  Tasks 1-3. Later implementation tasks are blocked until those tracked
  reports/specifications are complete and tests reject unknown fields.
- **No approximation:** No task permits deriving algorithms or resource
  semantics from screenshots, adding pixel tolerance, or silently skipping
  unknown commands.
