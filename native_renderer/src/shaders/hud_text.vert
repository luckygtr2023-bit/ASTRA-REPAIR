#version 450
// ASTRA COSMOS v0.6 — stroke text on canvas (HUD). Vertex positions are NDC
// segments produced by app/hud_text.cpp from the authoritative HudState;
// per-vertex color class via flat interpolation from the vertex attribute.
layout(location = 0) in vec2 inPos;
layout(location = 1) in float inColorIdx;

layout(push_constant) uniform HudPC { vec4 misc; } pc; // unused (uniform scale), kept for layout compat

layout(location = 0) flat out float colorIdx;

void main() {
    gl_Position = vec4(inPos, 0.0, 1.0);
    colorIdx = inColorIdx;
}
