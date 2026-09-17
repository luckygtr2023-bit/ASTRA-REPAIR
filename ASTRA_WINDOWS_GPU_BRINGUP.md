# ASTRA COSMOS — WINDOWS/GPU BRING-UP PACKAGE (executable validation checklist)

**Purpose:** run these on a real Windows machine with a real Vulkan-capable GPU
to promote the source-verified renderer (v0.4→v0.6 + v0.7 Phase-10) to
WINDOWS/GPU/RUNTIME VERIFIED. Do not self-grade: each item needs a named
artifact (console log snippet, RenderDoc capture, screenshot, validation
output). Gate order matters: stop and root-cause on first failure.

**Session verifier:** the repo records incident-resilient environment facts in
`ASTRA_V0_7_BASELINE_REPORT.md`; this file is the live procedure.

## 0. Prerequisites (record actual values)

| Item | How to check | Record |
|------|--------------|--------|
| Windows 10/11 x64 | `winver` | |
| GPU + driver | Task Manager / device vendor | |
| Vulkan runtime installed | `vulkaninfo` runs, reports driver | |
| Vulkan SDK (≥1.3) | `%VULKAN_SDK%` present; `glslangValidator.exe` exists | |
| Visual Studio 2022 (MSVC, C++20) | `cl` | |
| CMake ≥ 3.26 + Ninja | `cmake --version`, `ninja --version` | |
| RenderDoc (recommended) | installed | |
| Git | `git rev-parse HEAD` = tested commit | |

## 1. Build (W1)

```
git clone <repo> ASTRA-REPAIR && cd ASTRA-REPAIR
cmake -S native_renderer -B build -G "Visual Studio 17 2022" -A x64 ^
  -DVulkan_GLSLANG_VALIDATOR_EXECUTABLE="%VULKAN_SDK%\Bin\glslangValidator.exe"
cmake --build build --config Release
```
Expected artifacts:
- [ ] `build\Release\ASTRA COSMOS.exe` exists (record size) → log excerpt
- [ ] `build\Release\shaders\*.spv` **13 files** next to the exe (COPY target runs)
- [ ] copy `native_renderer\assets\star_temperature_lut.ppm` → `build\Release\assets\` (or run from repo root)

## 2. First launch (W2)

Run `ASTRA COSMOS.exe` from `build\Release` (console window opens).
- [ ] Process stays alive ≥ 60 s; window renders a changing scene (planets move/vector ticks) — screenshot A
- [ ] Startup console block present — capture **full log** (screenshot or redirect `> log_start.txt 2>&1`)

Required DIAG lines (all REAL, no zeros unless device says so):
- [ ] Vulkan instance + GPU name (`[ASTRA] GPU: ...`), apiVersion
- [ ] device type discrete/integrated + vendorID/driverVersion
- [ ] HDR format line: `HDR color format: R16G16B16A16_SFLOAT (capability-verified)`
- [ ] DIAG queue timestamp bits/period → `SUPPORTED` or `NOT AVAILABLE` (both honest)
- [ ] Swapchain format/presentMode FIFO, image count
- [ ] Shader loads 13/13 (astra/sphere/orbit/vector/cull/post x4/hud x2)
- [ ] Zero `[ASTRA] VK ERROR` lines

## 3. Vulkan validation layers (W3)

```
set VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
run exe again; monitor console for validation messages
```
- [ ] Attach full stderr/stdout; ANY `VALIDATION` error/warning triaged and logged (expect: none; fix root cause if any)

## 4. RenderDoc capture (W4 — GPU-driven path proof)

RenderDoc → Launch `ASTRA COSMOS.exe` → capture frame (F12) after ~3 s.
Inspect the frame and screenshot each:
- [ ] Event list contains: compute `vkCmdDispatch(cull)` → scene pass (`bg`, **2 indirect draws**, orbits, vectors, HUD) → bright → blur → blur → composite → Present
- [ ] Pipeline state at indirect draws: `vkCmdDrawIndexedIndirect` args buffer has LOW/HIGH commands (instance counts <= 10 typical solar scene; zero allowed)
- [ ] Compute shader writes: SSBO `g_low_buf`/`g_high_buf` populated (inspect buffer contents)
- [ ] Instance SSBO (`g_inst_buf`) has 10 records (32 B stride) with pos/radius/color/flag
- [ ] Render targets: HDR 1280×720 `R16G16B16A16_SFLOAT`; bright0/bright1 **640×360** (half-res); depth D32
- [ ] Bound descriptor sets per stage consistent (mesh set = 2 SSBOs; compute = 5; post = 1/2 samplers asymmetric as designed)

## 5. Interaction matrix (W5)

| Key | Expect | Pass? | Evidence |
|-----|--------|-------|----------|
| arrows | orbit camera rotates | [ ] | screenshot |
| PgUp/PgDn | zoom (follow) / speed (free) | [ ] | |
| Tab | selection+focus cycles; HUD SELECTED row follows | [ ] | |
| X | deselect; crosshair/highlight/`SELECTED none` NA rows | [ ] | |
| O | orbit-follow ⇄ FREE; axes follow frame | [ ] | |
| WASDQE(+SHIFT) | FREE movement | [ ] | |
| `+`/`-`,`0..8` | warp changes; planets visibly speed | [ ] | |
| Space | pause banner in HUD/title | [ ] | |
| `.` | one 60 s sim step when paused | [ ] | |
| Backspace | epoch→J2000 | [ ] | |
| F5 | full restart | [ ] | |
| F2/F3 | save/load in `<exe>\saves\scenario_1.json`; load restores state | [ ] | |
| V | velocity vectors on/off | [ ] | |
| P | peri/apo tick marks | [ ] | |
| G | reference axes on/off | [ ] | |
| H | HUD text on/off | [ ] | |
| `[` `]` | exposure steps ×1.25÷ (visual brightening) | [ ] | |
| F1 | console inspector + full HUD matrix | [ ] | log |
| ESC | clean quit, `Clean shutdown`, exit code 0 | [ ] | |

## 6. HDR/bloom visuals (W6)
- [ ] Screenshot with sun centered: visible disc bloom (not white blob = tone mapping) — screenshot B (z1)
- [ ] Exposure `[`/`]` alters screen; HUD exp line updates; ACES rolloff evident at high exposure

## 7. Resize robustness (W7)
- [ ] Drag-resize ×20: no crash, no garbage regions, HUD stable; log `[ASTRA] Swapchain recreated` lines counting up
- [ ] Minimize → restore: safe (render loop idles/presents continue)
- [ ] Maximize/restore + close: clean exit; no validation leaks (W3 log)

## 8. GPU timestamps (W8, Phase 10)
- [ ] If DIAG said SUPPORTED: Inspector/title line shows `gpu=X.XX ms (REAL, VkQueryPool timestamps)` — log line captured; if NOT AVAILABLE: that exact phrase on the F1 telemetry line — captured either way

## 9. Perf snapshot (W9 — 5 min of honest capture)
Record: resolution, driver, API version, avg CPU ms (title), gpu ms (W8),
draws/frame, indirect draws, LOD hi/lo counts, warp x1 and x1e6, FREE+FOLLOW —
**in the validation report table; no estimates.** Post this into the next
status document as PERF rows (real numbers only).

## 10. Failure handling policy
- Any crash: repro → root-cause → patch smallest component → regression gate →
  full battery (`v04_gates 562, v05 2984, v06 645, kepler 99, pytest 1535,
  shaders 13/13, validator 19, build PASS, main syntax 0 errors`) → THEN
  re-promote with evidence. Never bypass with toggles that hide the defect.

**Current repo state: all items BLOCKED pending this hardware run — nothing is
pre-filled. Copy this file's tables into `ASTRA_V0_7a_RUNTIME_VERIFICATION.md`
as they complete.**
