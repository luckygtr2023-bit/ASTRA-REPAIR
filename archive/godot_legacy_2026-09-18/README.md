# GODOT LEGACY ARCHIVE — 2026-09-18

This location preserves the complete Godot 4.4 experimental visualization track exactly as it
existed before its removal from the active production tree. **Nothing here participates in the
build, runtime, or test pipeline.** It is kept for historical reference only.

- `GODOT_PHASES/` — five phase design-spec markdowns (2026-09-17), HISTORICAL DOCUMENTATION.
- `visualization/` — the Godot 4.4 prototype (project.godot, GDScript bridge, gdshaders, VFX
  materials, GDExtension skeleton), OBSOLETE PROTOTYPE, plus its historical prose docs.

Why archived: the full audit (`/ASTRA_GODOT_LEGACY_AUDIT.md`, repo root) verified that no
production code, build script, CMake file, batch script, CI definition, test suite, or runtime
package ever referenced this material. The single production visualization architecture is:

`ASTRA Scientific Engine → RenderState → Native C++ Renderer → Vulkan → GPU`

Asset note: `visualization/assets/planets/earth_like_albedo.ppm` was the repository's only copy
and is preserved here per the standing rule that graphical assets are never deleted.
`visualization/assets/stars/star_temperature_lut.ppm` duplicates the copy in
`native_renderer/assets/` used by the production starfield.

If any future phase resurrects a secondary renderer, it must be re-added as new tracked work —
this archive is read-only history, not a staging area.
