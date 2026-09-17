# ASTRA COSMOS — v0.7 WINDOWS/GPU VALIDATION REPORT

## 1. Executive summary

**Outcome: BLOCKED — ENVIRONMENT LIMITATION.** The session executes inside a
Linux sandbox whose environment changed mid-flight (a sandbox re-provision wiped
`/tmp` entirely): **no GPU (not even software lavapipe id; zero ICDs), no
Windows, no Vulkan driver at runtime, and no network to a legal star catalog**
(GitHub-only egress). Therefore no claim of Windows/GPU/RunTime success can be
made. All source/static/build validation that is possible has been completed
and re-verified after the environment incident; the production repository
never regressed. Phase 10 (GPU timestamp instrumentation) was delivered at
SOURCE level with honest NOT AVAILABLE semantics and is the only honest,
on-spec source work the brief demanded.

## 2. Exact tested commit
`43255354dcb608336ae2bde50ce0e20b2839de02` (+v0.7 incremental commits on top —
see final report hash at push time).

## 3–5. Machine, CPU, RAM
Linux x86_64 container (Debian 12, kernel 6.1.158), shared vCPUs; RAM not
measured (irrelevant to result; no Windows device present)..

## 6–8. GPU / GPU driver / Vulkan version (runtime)
**NOT AVAILABLE / NO GPU**: no `/dev/dri`, no console ICDs in
`/usr/share/vulkan/icd.d` or `/etc/vulkan/icd.d`, `nvidia-smi`/`vulkaninfo`
absent, no lavapipe/swiftshader binaries. Vulkan SDK pieces exist only at
source level (headers + locally-built loader/glslang for static validation).

## 9–12. Windows version / compiler / CMake / SDK
Windows: NOT PRESENT (blocked).
Toolchain proven here: g++ (C++20), cmake 4.4.3 (pip), ninja 1.13.2,
Vulkan-Headers 1.4.362, glslang 16.6.0 (source-built), Vulkan-Loader 1.4.362
(source-built for linking).

## 13. Build result
Linux native build **PASS [13/13]** after environment re-provision (deterministic
repeat of v0.6 build). Windows build: **BLOCKED — ENVIRONMENT** (requires
Windows + MSVC; we do not substitute with cross-compilation claims).

## 14–15. Launch / runtime lifetime
**BLOCKED — ENVIRONMENT** (no Win32 host, no Vulkan driver at all).
No substitute was used (no mock headless run, no CPU fake frames, no
stub-recording of "launch evidence" — those would violate the mission).

## 16–18. Vulkan init / swapchain / HUD runtime evidence
**BLOCKED — ENVIRONMENT.** Static evidence: all device/surface/swapchain code
syntax-verified against real 1.4.362 headers (0 errors); diagnostics print REAL
driver values (API version, device type, limits, format, present mode) at
startup on the target machine.

## 19–21. GPU culling / indirect / HDR runtime evidence
**BLOCKED — ENVIRONMENT.** Source-level proof retained: single-workgroup
deterministic compaction mirror (v06 gates), IndirectCmd ==
VkDrawIndexedIndirectCommand `static_assert`, barriers SHADER_WRITE→
SHADER_READ + INDIRECT_COMMAND_READ, only `vkCmdDrawIndexedIndirect` draws
(no CPU visible-list regen). Runtime confirmation requires a real device.

## 22–23. Bloom / resize runtime evidence
**BLOCKED — ENVIRONMENT.** Static: half-extent policy gated (incl. zero/ext1
boundaries); resize path re-inspected (FB/view/scissor/descriptor lifecycle).

## 24–25. Selection/camera + simulation integration runtime evidence
**BLOCKED — ENVIRONMENT.** Static: `g_selection`/`selected_id` single identity
re-verified across the 4 consumers; kepler mirror 99/99 (max 3.55e-13) after
re-provision (deterministic scientific authority unchanged across environments).

## 26. GPU timing instrumentation — DELIVERED (source), RUNTIME BLOCKED
- Policy: `gpu_timing_supported(timestampValidBits, timestampPeriod)` —
  NOT AVAILABLE unless valid bits>0 and period>0.
- Path: DIAG printout of valid bits + period at startup; VkQueryPool(2) created
  only when supported (explicit skip message otherwise); `vkCmdWriteTimestamp`
  at TOP/BOTTOM of pipe around the frame; 1-frame-lag readback after the fence;
  `gpu_ms_from_ticks` for the conversion; HUD prints
  `GPU frame time=X.XX ms (REAL, VkQueryPool timestamps)` when supported,
  otherwise `GPU TIMING: NOT AVAILABLE` (CPU timing never substituted).
- Gates: availability semantics, exact 1 tick=1 ns conversion, zero delta,
  large-delta scale, monotonicity sweep (v06_gates 625 → 663 checks).
- **Runtime value: NOT VERIFIED (no device).**

## 27–28. Performance + screenshots
**None measured** (no device). All earlier in-app counters remain REAL-time
value sinks for the Windows machine (no fabricated numbers shipped in the repo).

## 29. Validation-layer results
Not obtainable (no device to attach layers to); DIAG line documents that
release build enables none.

## 30–31. Bugs discovered & root causes
1. **Sandbox environment destruction incident** (mid-session `/tmp` wipe +
   HEAD coercion): local branch had been reset externally to `89f98382`.
   Root cause: sandbox re-provision (external). Fix: explicit-refspec fetch +
   hash-verified reset to remote; added remote-sync verification & drift
   checks to session procedure. Remote never affected. **No repo bug.**
2. **v06 gate arithmetic slip in my Phase-10 test** (expected 500 ms for
   1e12 ticks × 0.5 ns — correct value 500,000 ms): caught by the gate
   itself; corrected the expectation with exact arithmetic.
3. **glslang CMake: `astra_shaders` target silently skipped when the
   validator binary isn't on PATH** (post-wipe): configure now explicitly passed
   `-DGLSLANG_VALIDATOR=/tmp/vk/bin/glslangValidator`; production shaders
   confirmed 13/13 (visible set at configure time, recorded).
4. **Shell-heredoc escaping artifacts while reconstructing the Win32 stub**
   (char-literal escapes): fixed by rewriting the affected lines.

## 32. Fixes applied
Environment/toolchain restoration (headers, glslang-from-source,
loader-from-source, venv, kepler regeneration from the repo's own generator —
identical result, determinism across re-provision proven); Phase-10 feature-
gated timestamp path (source); gate corrections above.

## 33. Remaining blockers (Windows machine required for each)
| # | Test | Status |
|---|------|--------|
| W1 | Build with MSVC + real Vulkan SDK, launch `ASTRA COSMOS.exe` | BLOCKED — ENVIRONMENT |
| W2 | Device bring-up DIAG line capture (API/device/driver/queue/HDR format) | BLOCKED — ENVIRONMENT |
| W3 | HDR+r16f image actually allocated; SRGB swapchain present | BLOCKED — ENVIRONMENT |
| W4 | HUD on-canvas pixels + H toggle + labels/NA rows on screen | BLOCKED — ENVIRONMENT |
| W5 | Compute dispatch + deterministic compaction executing; mask LOD splits | BLOCKED — ENVIRONMENT |
| W6 | Two `vkCmdDrawIndexedIndirect` executing with GPU-side counts; zero-visible safe | BLOCKED — ENVIRONMENT |
| W7 | Bloom passes visibly executing at half-res + ACES composite to screen | BLOCKED — ENVIRONMENT |
| W8 | Resize/minimize/out-of-date recreation loop w/o stale resources | BLOCKED — ENVIRONMENT |
| W9 | Tab/X/G/O/WASDQE/arrows interactions; selection marker follows | BLOCKED — ENVIRONMENT |
| W10 | Warp presets, `.` step, Backspace, F5, F2/F3 round-trips | BLOCKED — ENVIRONMENT |
| W11 | VkQueryPool timestamps produce REAL GPU ms; HUD wording matches | BLOCKED — ENVIRONMENT |
| W12 | RenderDoc capture: dispatch, compacted buffers, commands, indirect draws | BLOCKED — ENVIRONMENT |
| W13 | Full perf table (resolution/GPU/driver/Vulkan) with real values | BLOCKED — ENVIRONMENT |
| W14 | Validation layers enabled run (no errors) | BLOCKED — ENVIRONMENT |
| W15 | Real astronomical catalog (also blocked by network+license) | BLOCKED — DATA |

## 34. Recommended next phase
Run W1–W15 on the target Windows+GPU workstation exactly as listed above
(single session, save RenderDoc captures + console DIAG logs to `logs/` and
attach to a v0.7a report). Only after those pass should any rendering feature
work resume. Exact build/launch commands are recorded in
`ASTRA_V0_7_BASELINE_REPORT.md`.
