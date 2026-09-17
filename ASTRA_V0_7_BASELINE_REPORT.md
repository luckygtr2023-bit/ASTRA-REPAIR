# ASTRA v0.7 BASELINE REPORT (Phase 0 evidence)

## Git state (this session, including environment incident)

- Baseline target commit: **43255354** (v0.6, on `arena/01a0b082-astra-repair`).
- On session start the local clone HEAD had been externally coerced back to the
  upload commit `89f98382` (entire v0.4–v0.6 content showed as untracked). The
  remote branch still pointed at `4325535` — pushed work was safe remotely.
- Recovery (no history rewritten remotely): `git fetch` with explicit refspec
  (this clone's fetch refspec covers `main` only), verified zero content drift
  of the fourteen key production builds/docs files, then
  `git reset --hard origin/arena/01a0b082-astra-repair`.
  Result: local HEAD = remote = 4325535, clean tree. **No data lost.**
- Note: sandbox default fetch refspec = `+refs/heads/main:refs/remotes/origin/main`;
  fetching the arena branch requires the explicit `'refs/heads/*:refs/remotes/origin/*'`
  refspec (recorded for future turns).

## Execution environment (measured this session)

| Item | Value |
|------|-------|
| OS | Linux (Debian 12 bookworm), `Linux 6.1.158+ #1 SMP PREEMPT_DYNAMIC` x86_64 |
| Host type | containerized Linux sandbox — **NOT Windows** |
| GPU | **NONE**: no `/dev/dri`, no `nvidia-smi`, no vulkaninfo, **no ICD files** (`/usr/share/vulkan/icd.d`, `/etc/vulkan/icd.d` empty/absent); not even a software Vulkan device (lavapipe/swiftshader absent) |
| Network | GitHub reachable (git clone/fetch OK, pip OK); TLS to most hosts blocked. Attempted earlier: Harvard TDC (Yale BSC5 mirror) TLS-EOF, raw.githubusercontent TLS-EOF |
| Compiler | g++ (Debian GCC) `-std=c++20` |
| Python | 3.11.2 system |
| Toolchain re-provision | The /tmp tree was wiped mid-session; fully rebuilt this turn: python venv with cmake 4.4.3 + ninja + pytest 9.1.1 + numpy 2.4.6; Khronos Vulkan-Headers 1.4.362 (git clone, installed); glslang 16.6.0 built from source (StandAlone → `/tmp/vk/bin/glslangValidator`); Vulkan-Loader 1.4.362 built from source (`/tmp/vk/lib/libvulkan.so`, xcb/xlib WSI disabled — no system XCB headers); win32 syntax stub recreated from scratch |

## Build status (this sandbox)

- Production build (astra native via CMake+Ninja): **PASS** [13/13 steps].
- All 13 production shaders compile to SPIR-V via glslang: **13/13**.
- `main_production.cpp` against real Vulkan 1.4.362 headers + Win32 stub: **0 errors**.
- NO Windows build possible here (not Windows, no MSVC).
- NO real validation layers available (no Vulkan device to enable them for).

## Vulkan SDK status

- Headers: Vulkan-Headers 1.4.362 (source-verified).
- Loader: 1.4.362 (built locally; artifact only for linking the Linux test binary).
- glslang: 16.6.0 (built locally; validates all shaders).
- **Any REAL Vulkan ICD/driver: unavailable. Runtime Vulkan execution: CANNOT be performed.**

## Existing executable(s)

- Linux: `astra_native` (headless native test binary built by the repo CMake) — exists under sandbox build dir; the production app `main_production.cpp` is Win32-only by design (Win32 + `/SUBSYSTEM:WINDOWS` semantics). No Windows EXE can exist in this environment.

## Expected launch command (for the Windows machine)

```
cmake -S native_renderer -B build -G "Visual Studio 17 2022" -A x64 ^
      -DVulkan_LIBRARY="C:/VulkanSDK/<ver>/Lib/vulkan-1.lib" ^
      -DVulkan_GLSLANG_VALIDATOR_EXECUTABLE="C:/VulkanSDK/<ver>/Bin/glslangValidator.exe"
cmake --build build --config Release
copy native_renderer\assets\star_temperature_lut.ppm build\Release\assets\  (or assets\ launch cwd)
build\Release\ASTRA COSMOS.exe
```
(Exact exe name/install targets per `native_renderer/CMakeLists.txt` install rules.)

## v0.6 source-verified feature inventory (at 4325535)

HDR16F target w/ capability gating; ACES-approx composite + `[`/`]` exposure on SRGB swapchain;
bloom bright→blurH/V half-res→composite; instanced bodies (32 B/record SSBO); GPU culling with deterministic
single-workgroup compaction → **only `vkCmdDrawIndexedIndirect` (×2)**; LOD via two real icospheres + 1% rule;
HUD in-canvas stroke text from authoritative hud_state (H toggle); reference axes (G); selection marker;
real CPU telemetry; kepler-gated scientific overlays; persistence (F2/F3); classified audio routing (offline).

## Known BLOCKED items for this environment (unchanged in v0.7)

- Windows build + launch (needs Windows + MSVC + Win32).
- Any Vulkan runtime execution (needs a real driver/ICD; none present, not even software ICD).
- Resize/minimize/driver-behavior testing, RenderDoc, visual/pixel evidence, FPS/perf numbers.
- Real astronomical catalog ingestion (no network to a legal dataset mirror; none in-repo).
