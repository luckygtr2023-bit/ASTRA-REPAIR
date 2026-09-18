#version 450
// DSO glow: soft elliptical falloff (no texture dependency — procedural
// CINEMATIC glow geometry; the POSITION and SIZE are REAL catalog data).
layout(location = 0) in vec3 v_rgb;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in float v_flags;
layout(location = 0) out vec4 out_color;

void main() {
    float r2 = dot(v_uv, v_uv);
    if (r2 > 1.0) discard;
    float fall = exp(-r2 * 3.5) * 0.42;
    out_color = vec4(v_rgb * fall, fall * 0.6);
}
