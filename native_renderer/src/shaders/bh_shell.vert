#version 450
// ASTRA COSMOS — black-hole structure / spacetime light-ray overlay (v1.2).
// Vertex positions come from app/bh_viz (CPU-built polylines: horizon shell,
// photon sphere, ISCO ring, null-geodesic photons); they are central-body
// relative, already in render units. Scientific values produced the geometry;
// only the CINEMATIC magnification sits here (documented in the HUD).

layout(push_constant) uniform BhShellPC {
    mat4 viewProj;
    vec4 center;   // xyz = central body render position (target-relative)
    vec4 color;
} pc;

layout(location = 0) in vec3 inPos;
layout(location = 0) out vec4 outColor;

void main() {
    gl_Position = pc.viewProj * vec4(pc.center.xyz + inPos, 1.0);
    outColor = pc.color;
}
