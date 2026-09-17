#version 450
// ASTRA COSMOS v0.6 — stroke text fragment: palette by color class.
// Palette is display styling only (CINEMATIC); content semantics come from hud_state.
layout(location = 0) flat in float colorIdx;
layout(location = 0) out vec4 outColor;

void main() {
    const vec3 P[6] = vec3[6](
        vec3(0.95, 0.95, 1.0),   // 0 HEADER white
        vec3(0.30, 0.95, 0.45),  // 1 REAL green
        vec3(0.35, 0.75, 1.0),   // 2 DATA-DERIVED blue
        vec3(0.35, 0.95, 0.95),  // 3 SIMULATED cyan
        vec3(1.0, 0.72, 0.30),   // 4 CINEMATIC amber
        vec3(0.55, 0.58, 0.62)); // 5 NOT AVAILABLE gray
    int idx = int(colorIdx + 0.5);
    outColor = vec4(P[clamp(idx, 0, 5)], 1.0);
}
