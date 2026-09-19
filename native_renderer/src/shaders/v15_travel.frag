#version 450
// v1.5 EXTREME SPACETIME travel overlay fragment — additive marks.
// KINDS come from TravelMark.flags (set by the CPU mirror of the validated
// python authority). THEORETICAL/SPECULATIVE labels live on the HUD; the
// marks themselves are just data-driven view aids (CINEMATIC presentation).
layout(location = 0) in vec3 v_rgb;
layout(location = 1) in vec2 v_disc;
layout(location = 2) in float v_kind;
layout(location = 0) out vec4 outColor;

void main() {
    float r2 = dot(v_disc, v_disc);
    if (r2 > 1.0) discard;
    float edge = 1.0 - r2;
    // kind-specific shaping: observer marker (3) is a hard bright core;
    // tunnel (1)/bubble (2) soft glow; mouths (0) thin rings (low alpha center).
    float a;
    if (v_kind < 0.5) a = smoothstep(0.15, 0.95, edge) * 0.55;          // mouths
    else if (v_kind < 1.5) a = smoothstep(0.0, 1.0, edge) * 0.20;       // tunnel
    else if (v_kind < 2.5) a = smoothstep(0.0, 1.0, edge) * 0.18;       // bubble
    else if (v_kind < 3.5) a = pow(edge, 2.0) * 1.0;                    // observer
    else a = smoothstep(0.0, 1.0, edge) * 0.30;                         // line
    outColor = vec4(v_rgb, a);
}
