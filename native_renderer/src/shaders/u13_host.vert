#version 450
// ASTRA COSMOS — v1.3 host-frame overlay shader (destruction / observation).
// Positions are already target-relative render units for an ARBITRARY anchor
// supplied as pc.host (the production app converts scientific km into units
// only at this visualization boundary — double-precision authority upstream,
// float conversion once, here).
//
// Provenance: geometry computed by the native mirrors of astra.destruction /
// astra.temporal; colors are per-batch visualization labels (push constant).

layout(push_constant) uniform U13HostPC {
    mat4 viewProj;
    vec4 host;     // xyz = anchor render position (camera-target-relative)
    vec4 color;    // xyz = batch color (visualization label)
} pc;

layout(location = 0) in vec3 inPos;
layout(location = 0) out vec4 outColor;

void main() {
    // Single subtraction for every vertex; identical operation order to
    // bh_shell.vert (center.xyz + inPos) to keep fp predictability.
    vec3 lp = inPos - pc.host.xyz;
    gl_Position = pc.viewProj * vec4(lp, 1.0);
    outColor = pc.color;
}
