# ASTRA COSMOS — REPAIR REPORT
**Date:** 2026-09-17 | **Scope:** full source checkout (ex `ASTRA.V.0.1.zip`) | **Result of every executed check below: PASS**

This report is written in the same "claim only what was executed" style as the
project's other audits. Items marked **CODE FIXED** are repaired and statically
validated. Items marked **NEEDS HARDWARE** still require a Windows machine with
a real GPU + Vulkan driver before release claims can change.

---

## 1. Build system repaired (`native_renderer/CMakeLists.txt`) — CODE FIXED

| Problem (before) | Repair |
|---|---|
| Vulkan SDK hardcoded to `E:/VULKAN` (include, lib **and** `ENV{VULKAN_SDK}` forcibly overwritten) — guaranteed configure/build failure on any other machine | Portable `find_package(Vulkan)` consuming the standard `VULKAN_SDK` environment; explicit `FATAL_ERROR` with install guidance when absent |
| Header-only deps probed at `/home/user/entt`, `/home/user/imgui`, `/home/user/miniaudio`, `/home/user/glslang` (paths from an AI build box) | Portable `find_path` with `ENTT_DIR` / `IMGUI_DIR` / `MINIAUDIO_DIR` hints; graceful fallbacks preserved |
| glslangValidator searched in `/tmp`, `/tmp/glslang_build2/StandAlone` | `Vulkan_GLSLANG_VALIDATOR_EXECUTABLE` (CMake ≥ 3.21) + `find_program` in `$ENV{VULKAN_SDK}/Bin` |
| Shader `add_custom_command`s had **no consuming target → never executed** (SPIR-V in build dirs was placed manually) | New `astra_shaders` ALL target; `astra_cosmos` depends on it; `POST_BUILD` copies `astra.vert.spv`/`astra.frag.spv` next to the exe |
| `main.cpp` + `main_production.cpp` compiled **into** the `astra_renderer` static library (duplicate entry points; linking worked only by static-linker accident) | Both mains excluded from the library (`list(REMOVE_ITEM ...)`) |
| `astra_cosmos` included/linked `E:/VULKAN/...` literally | Uses `Vulkan_INCLUDE_DIRS` / imported `Vulkan::Vulkan` |
| `cmake_minimum_required(VERSION 3.28)` — nothing in the file needs >3.21; blocked users on older-but-fine CMake (exact failure captured in `logs/astra_startup.log` of the original zip) | Minimum lowered to **3.21**; `scripts/terminal.bat` messages updated to match |
| Tests/tools subdirectories are dummy stubs yet defaulted ON | Default OFF (developer flow passes the flags explicitly anyway) |

## 2. Renderer repaired (`native_renderer/src/main_production.cpp`) — CODE FIXED

Implements exactly the "What needs fix next" list in `ASTRA_CURRENT_RUNTIME_VERIFICATION.md` §9:

| # | Finding (before) | Repair |
|---|---|---|
| 1 | No graphics pipeline; `render_frame()` only cleared the framebuffer with an animated color | `create_pipeline()`: shader stages, empty vertex input, dynamic viewport/scissor, rasterization, MSAA=1, no-blend color attachment, subpass 0 |
| 2 | Shaders existed but were **never loaded** | Runtime SPIR-V loader (<exe>/shaders → ./shaders → <exe> → CWD) + SPIR-V magic validation + `vkCreateShaderModule` |
| 3 | ASTRA state updated every frame, **never consumed by renderer** | `vkCmdPushConstants` feeds `g_scene.state.sim_time_s`, framebuffer extent and frame counter to `astra.frag` (layout verified against the GLSL: 16 bytes, 4 floats, fragment stage) |
| 4 | No draw call | `vkCmdDraw(3, 1, 0, 0)` — fullscreen triangle, vertex positions from `gl_VertexIndex` (matches `astra.vert`) |
| 5 | No swapchain recreation; first resize broke presentation | `recreate_swapchain()` + `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR` handling on **both** acquire and present; `WM_SIZE` marks the swapchain dirty |

Additional real bugs found and fixed in the same file:

- **Command pool used hardcoded queue family 0** ("Simplified") — a Vulkan spec violation whenever the graphics+present family isn't 0. Now uses the selected family.
- **Swapchain image count could exceed `caps.maxImageCount`** (some drivers reject `minImageCount+1`) — now clamped.
- **Zero surface formats** would read `fmts[0]` out of bounds — now guarded.
- **Extent never clamped** to `min/maxImageExtent` — now clamped; zero-extent surfaces defer instead of creating an invalid swapchain.
- **Minimized window spun the GPU at 100%** (or presented garbage) — now idles at ~60 Hz and recreates on restore.
- **Device selection took `devs[0]`** — now prefers discrete > integrated > virtual.
- **Misleading error strings** — `VK_CHECK` now prints file:line and the failing call.
- Dead `g_swapchain_dirty` declaration (previously never read or written) is now the wired recreation trigger.

**Static validation performed here:** `g++ -std=c++20 -fsyntax-only -Wall` against a
faithful stub of the used Win32 + Vulkan 1.3 API surface → **0 errors**.
Push-constant layout cross-checked against `src/shaders/astra.frag` → exact match.
**NEEDS HARDWARE:** actual pipeline creation, presentation and visual output on a
real driver (run the acceptance protocol in `distribution/acceptance.template.json`).

## 3. Repository hygiene — CODE FIXED

- **`.gitignore` was malformed:** it contained pasted Markdown code fences (```` ``` ````), an invalid `ignore pyc` directive, ignored the **entire `tests/` suite** (84 files, 1,535 tests) while *not* ignoring the actual build trees that were committed. Rewritten: tests are tracked; native build dirs, MSVC residue, logs, env secrets and compiled binaries are ignored; the `release/ASTRA-COSMOS/bin/*` staging fixtures keep an explicit exception because `test_launcher_deletions_preserve_runtime` asserts their presence.
- **~35 MB of committed build outputs deleted:** `native_renderer/build-prod/`, `build-windows/`, `build-production/` (PE executables, `.obj`, `.tlog`, MSVC logs).
- **Privacy leak removed:** `logs/astra_startup.log` contained a real user's Windows username and absolute machine paths. Log pattern is now gitignored; `logs/.gitkeep` kept.
- **Stale nested archive removed from the working tree:** `ASTRA.V.0.1.zip` (the deliverable this checkout was made from; it remains in git history at commit `89f9838`). The double-nested `ASTRA-COSMOS--arena-…/ASTRA-COSMOS--arena-…/` wrapper is flattened to the repo root.
- **Metadata aligned with reality:** `pyproject.toml` + `astra/__init__.py` author → Lucky Kumar (per `COPYRIGHT.md`); repository URLs → the actual GitHub repo (were `github.com/astra/astra-core`, `astra@example.com`).
- **README "Source of Truth" line corrected:** it claimed "No Godot" while `visualization/godot/project.godot` is a complete Godot 4.4 project. Now states the truth: native C++/Vulkan is the end-user path; `visualization/` is a separate experimental Godot track outside the Windows runtime distribution.

## 4. Tooling repaired — CODE FIXED

- `native_renderer/tools/validate_native_project.py`:
  - `ROOT = Path("native_renderer")` (CWD-relative) **crashed with `FileNotFoundError`** from any directory except the repo root → now resolves from `__file__`; verified from three different working directories: **223 OK, 0 FAIL** each time.
  - Operator-precedence bug at the shader-classification check (`A and B or C or True`) read possibly-missing files and made the check a no-op → guarded read, semantics unchanged.
- `scripts/terminal.bat`: "CMake 3.28+" strings → 3.21 (CRLF line endings preserved, verified byte-level).

## 5. Full verification executed after repairs

| Suite | Result |
|---|---|
| `pytest tests/` (Python scientific engine) | **1535 passed** |
| `pytest distribution/tests/ scripts/tests/` | **32 passed** |
| `native_renderer/tools/validate_native_project.py` | **223 OK, 0 FAIL** |
| `main_production.cpp` syntax (-fsyntax-only, `g++ -std=c++20 -Wall`) | **PASS** |
| GLSL ↔ push-constant layout cross-check (`astra.frag` vs pipeline) | **MATCH** |
| `START.bat` / `terminal.bat` CRLF integrity | **PASS** |

## 6. What did NOT change (deliberately)

- `release/` staging tree contents (asserted by the distribution policy tests).
- `ASTRA_CURRENT_RUNTIME_VERIFICATION.md` and other dated reports — historical evidence; this report supersedes the "What needs fix next" list only, items 1–5 now **CODE FIXED**.
- Release catalog stays `BLOCKED` — correct until the acceptance protocol runs on GPU hardware.

## 7. Release gates still open (NEEDS HARDWARE)

1. Build on a Windows machine: `cmake -S native_renderer -B build -DCMAKE_BUILD_TYPE=Release` with the Vulkan SDK installed → confirm `glslangValidator` produces `shaders/astra.vert.spv` + `astra.frag.spv` beside `ASTRA COSMOS.exe`.
2. Run the exe on a real GPU: window shows the animated deep-space core glow + resolution grid (fragment shader), resize/minimize/restore works indefinitely, ESC closes cleanly.
3. Record results through `distribution/release_tools.py` acceptance flow before touching the release catalog.
