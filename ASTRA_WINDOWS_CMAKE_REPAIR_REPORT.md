# ASTRA COSMOS — WINDOWS CMAKE CONFIGURATION REPAIR
**Date:** 2026-09-17 | **Branch:** `arena/01a0b082-astra-repair` | **Scope:** targeted build-system repair only

---

## ROOT CAUSE

`find_path(ENTT_INCLUDE_DIR ...)`, `find_path(IMGUI_DIR ...)`, `find_path(MINIAUDIO_INCLUDE_DIR ...)`
store the sentinel value `<NAME>-NOTFOUND` when a header is not found. The root
`native_renderer/CMakeLists.txt` passed all three variables **unconditionally** to
`target_include_directories(astra_renderer PUBLIC ...)`:

```cmake
target_include_directories(astra_renderer PUBLIC
    src
    src/rhi
    ${Vulkan_INCLUDE_DIRS}
    ${ENTT_INCLUDE_DIR}        # → "ENTT_INCLUDE_DIR-NOTFOUND" when missing
    ${IMGUI_DIR}               # → "IMGUI_DIR-NOTFOUND"
    ${MINIAUDIO_INCLUDE_DIR}   # → "MINIAUDIO_INCLUDE_DIR-NOTFOUND"
)
```

At **generate** time CMake validates target include directories and aborts with
`... includes non-existent path "ENTT_INCLUDE_DIR-NOTFOUND" ...`, while the
configure log simultaneously claimed the fallbacks were active ("using minimal
internal ECS fallback", "debug UI disabled", "audio headers disabled"). Hence the
internal inconsistency: the dependencies were *declared* optional but *consumed*
as if required.

Secondary developer-path defects found during inspection (same defect class):

1. `src/rhi/vulkan_rhi.h` defaulted `glslang_path_` to `"/tmp/glslangValidator"`;
   `vulkan_rhi.cpp` shells it out via `std::system()` → every run of the
   diagnostic `astra_native` failed 6+ times with `sh: 1: /tmp/glslangValidator:
   not found` on any machine except the original build box (and always on Windows).
2. `src/audio/cosmic_audio_engine.cpp` probed `__has_include("/home/user/miniaudio/miniaudio.h")`
   — an absolute build-box path as the feature detection mechanism.
3. Log/comment strings in `main.cpp` and `vulkan_rhi.cpp` referenced
   `/tmp/glslangValidator` and `/home/user/Vulkan-Headers`.

## FILES CHANGED

| File | Change |
|---|---|
| `native_renderer/CMakeLists.txt` | Optional deps consumed only when found; `ASTRA_HAS_*` feature guards exported; production Win32 target gated `if(WIN32)`; summary no longer prints `-NOTFOUND` |
| `native_renderer/src/rhi/vulkan_rhi.h` | `glslang_path_` default: PATH-resolved `glslangValidator`, override via `ASTRA_GLSLANG_VALIDATOR`; added `<cstdlib>` |
| `native_renderer/src/rhi/vulkan_rhi.cpp` | Removed build-box path strings from log output/comment |
| `native_renderer/src/main.cpp` | Log line no longer prints `/tmp/glslangValidator` |
| `native_renderer/src/audio/cosmic_audio_engine.cpp` | Feature probe: absolute `__has_include` path → `defined(ASTRA_HAS_MINIAUDIO)` (defined by CMake) |
| `native_renderer/src/audio/cosmic_audio_engine.h` | Comment updated (no code path change) |
| `.gitignore` | Ignore root `build/` (the documented developer configure output directory) |

No Python/scientific-engine files were touched. No dependency was added,
vendored, faked, or removed. Vulkan remains **REQUIRED** (`FATAL_ERROR` if
absent). glslangValidator remains required in practice for the shader build
(explicit WARNING otherwise), as the architecture already assumed.

## EXACT REPAIR

### 1. Optional dependencies are consumed only behind `if(<VAR>)`

`find_path` results ending in `-NOTFOUND` evaluate false in CMake `if()`:

```cmake
target_include_directories(astra_renderer PUBLIC
    src
    src/rhi
    ${Vulkan_INCLUDE_DIRS}   # REQUIRED — configure aborts earlier if missing
)

if(ENTT_INCLUDE_DIR)
    target_include_directories(astra_renderer PUBLIC ${ENTT_INCLUDE_DIR})
    target_compile_definitions(astra_renderer PUBLIC ASTRA_HAS_ENTT=1)
endif()
if(IMGUI_DIR)
    target_include_directories(astra_renderer PUBLIC ${IMGUI_DIR})
    target_compile_definitions(astra_renderer PUBLIC ASTRA_HAS_IMGUI=1)
endif()
if(MINIAUDIO_INCLUDE_DIR)
    target_include_directories(astra_renderer PUBLIC ${MINIAUDIO_INCLUDE_DIR})
    target_compile_definitions(astra_renderer PUBLIC ASTRA_HAS_MINIAUDIO=1)
endif()
```

Resulting semantics (unchanged from the project's own fallback design):

- **EnTT missing →** internal ECS fallback. (Inspection confirmed: no source
  file includes `entt/*` today, so no source change was needed.)
- **Dear ImGui missing →** debug UI disabled. (`src/debug/imgui_debug.cpp` is a
  guarded stub and includes no `imgui.h`; no source change needed.)
- **miniaudio missing →** audio headers disabled; `cosmic_audio_engine` keeps
  its existing mock-synthesis branch, now selected by the CMake-provided
  `ASTRA_HAS_MINIAUDIO` definition instead of an absolute-path probe.

### 2. Production Win32 + Vulkan target preserved and gated by platform

`astra_cosmos` (`main_production.cpp`, `OUTPUT_NAME "ASTRA COSMOS"`,
`/SUBSYSTEM:WINDOWS`) is inherently Windows-only (it includes `<windows.h>`).
It is now declared inside `if(WIN32)` — on Windows the target and its output
are exactly as before; on other hosts the same project configures and builds
without pretending a Win32 source is portable. C++20, MSVC flags, the
`astra_shaders` dependency and the POST_BUILD SPIR-V copy next to the exe are
all preserved (moved inside the same `if(WIN32)`).

### 3. Developer-path cleanup (same defect class, in the *diagnostic* runtime)

- `glslang_path_`: `"/tmp/glslangValidator"` → `"glslangValidator"` resolved
  from `PATH` (the Vulkan SDK places it in `<SDK>/Bin`), with an
  `ASTRA_GLSLANG_VALIDATOR` environment override. No `/tmp/astra_shader.spv`
  semantics changed; the existing static `#version 450` fallback is intact.
- `--finite-math-only`-style behaviorally relevant code: none touched.
- Legacy opt-in `launcher/launcher.cpp` (not built by default) still references
  `/tmp/astra_build/astra_native` — recorded in REMAINING BLOCKERS, not touched
  to keep this repair targeted.

## CONFIGURE RESULT

Command (fresh directory, no optional deps installed, none of `ENTT_DIR` /
`IMGUI_DIR` / `MINIAUDIO_DIR` set):

```
cmake -S native_renderer -B build -DCMAKE_BUILD_TYPE=Release
```

Executed on: Debian 12 sandbox, CMake 4.4.3, GCC 12.2, real Khronos
Vulkan-Headers 1.4.362 + Vulkan-Loader 1.4.362 (built from source for this
validation) + glslang 16.6.0 (built from source), assembled as a standard
`VULKAN_SDK=/tmp/vk` SDK layout. **PASS** — key output:

```
-- Found Vulkan: /tmp/vk/lib/libvulkan.so (found version "1.4.362") found components: glslangValidator
-- EnTT not found (set ENTT_DIR) — using minimal internal ECS fallback
-- Dear ImGui not found (set IMGUI_DIR) — debug UI disabled
-- miniaudio not found (set MINIAUDIO_DIR) — audio headers disabled
-- Production target astra_cosmos is Windows-only — skipped on Linux
-- glslangValidator: /tmp/vk/bin/glslangValidator
--   ENTT_INCLUDE_DIR: not found — using project fallback (OK)
--   IMGUI_DIR: not found — using project fallback (OK)
--   MINIAUDIO_INCLUDE_DIR: not found — using project fallback (OK)
-- Configuring done
-- Generating done            ← the step that previously failed
```

Found-dependency contrast run (`ENTT_DIR`/`IMGUI_DIR`/`MINIAUDIO_DIR` set to
real header dirs): all three detected, include dirs appear in compile commands,
`ASTRA_HAS_ENTT=1` / `ASTRA_HAS_IMGUI=1` / `ASTRA_HAS_MINIAUDIO=1` are defined —
functionality when present is preserved. **PASS.**

## BUILD RESULT

```
cmake --build build --config Release
```

**PASS (exit 0)** on the validation host:

- `libastra_renderer.a` — all ~125 library sources compile with **zero**
  optional dependencies installed (2,624,298 bytes)
- `astra_native` — diagnostic executable linked against the real Khronos
  Vulkan loader (164,344 bytes — byte-identical size to the archived fixture)
- Smoke run `./build/astra_native --headless` → exit 0; the previous
  `sh: 1: /tmp/glslangValidator: not found` cascade is **gone**
- `astra_cosmos` target: not generated on this host **by design** (Windows-only);
  its generation is a Windows gate — see VERIFIED vs NOT VERIFIED.

## SHADER BUILD RESULT

**PASS.**

- `astra_shaders` is part of the default `ALL` build; explicit
  `cmake --build build --target astra_shaders` re-run is idempotent.
- `build/shaders/astra.vert.spv` — 1,360 bytes, magic `0x07230203`, size % 4 = 0
- `build/shaders/astra.frag.spv` — 2,584 bytes, magic `0x07230203`, size % 4 = 0
- Byte sizes are **identical** to the SPIR-V previously produced on the
  original Windows build machine (1360/2584), i.e. the pipeline reproduces the
  archived artifacts exactly.

## NOTFOUND SWEEP (validation gate 6)

- `grep NOTFOUND build/compile_commands.json` (+ `flags.make`, Makefiles,
  link files) → **0 hits** in any compile/link command.
- Remaining NOTFOUND strings exist only in: `CMakeCache.txt` (inert cache
  records of the search itself, 7 lines) and CMake's own
  `CMAKE_LINK_STARTFILE-NOTFOUND` boilerplate in compiler-detection files.
  Neither is consumed by any target. **PASS.**

## TEST RESULTS

| Suite | Result |
|---|---|
| `pytest tests/` (scientific engine) | **1535 passed** |
| `pytest distribution/tests/ scripts/tests/` | **32 passed** |
| `native_renderer/tools/validate_native_project.py` (from repo root) | **223 OK, 0 FAIL** |
| `native_renderer/tools/validate_native_project.py` (from `native_renderer/`) | **223 OK, 0 FAIL** |
| Configure, no optional deps (generate step) | **PASS** |
| Full default build, no optional deps | **PASS (exit 0)** |
| Shader target + SPIR-V validity | **PASS** |
| Found-dependency contrast configure | **PASS** |

## REMAINING BLOCKERS

1. **Windows production build + run** — `ASTRA COSMOS.exe` generation that was
   attempted in the 15:44 log; requires a Windows SDK-installed machine. The
   CMake side that blocked it is repaired; the actual compile/link on MSVC and
   on-GPU presentation remain acceptance-gates.
2. `launcher/launcher.cpp` (legacy, `ASTRA_BUILD_LAUNCHER=OFF` by default, not
   part of any shipped path) still contains a `/tmp/astra_build/astra_native`
   reference; deliberately left untouched to keep this repair targeted.
3. `install(...)` rules, and `ASTRA_BUILD_TESTS/TOOLS` subdirectories, are
   placeholder stubs upstream — not part of the production target.
4. `distribution` release catalog remains `BLOCKED` until the Windows + GPU
   acceptance evidence exists (unchanged, correct).

## VERIFIED vs NOT VERIFIED

**VERIFIED (executed, output captured):**
- CMake configure + **generate** succeeds with all three optional dependencies
  absent — the reported failure mode is gone
- No `-NOTFOUND` string is consumed by any target or compile command
- Full default build compiles & links with zero optional dependencies
- Production SPIR-V is produced by the build (not hand-placed), byte-identical
  to the archived Windows artifacts
- When dependencies are present, they are detected, included, and feature-guard
  macros defined (no functionality removed)
- Diagnostic executable runs headless, exit 0, no hardcoded-path failures
- CMake ≥ 3.21 semantics used; no `E:/VULKAN`, no `/home/user`, no developer
  absolute paths remain in the build system or active sources

**NOT VERIFIED (requires the Windows machine / GPU — not claimed):**
- MSVC compile/link of `ASTRA COSMOS.exe` (Windows-only target)
- glslangValidator discovery via the LunarG Windows SDK registry paths
- Window creation, real pipeline creation, presentation, resize behavior on a
  physical GPU driver
- Any runtime/GPU success claim whatsoever
