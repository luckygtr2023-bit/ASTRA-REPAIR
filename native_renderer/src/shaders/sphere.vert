#version 450
// ASTRA COSMOS v0.6 — INSTANCED celestial bodies (GPU-driven path).
// Instance data = compacted batch list written by cull.comp (deterministic
// order); draw count comes ONLY from vkCmdDrawIndexedIndirect (GPU count).
// Selection highlight: per-instance flag in color_flag.w (authoritative
// single identity packed from RenderState — no duplicate selection path).

layout(location = 0) in vec3 inPos; // unit icosphere

struct BodyInstance { vec4 pos_radius; vec4 color_flag; };
layout(std430, binding = 0) readonly buffer BatchList { BodyInstance inst[]; };

layout(push_constant) uniform InstPC {
    mat4 viewProj;
    vec4 sunPosEmis; // xyz = sun render pos
    vec4 misc;       // x = selected boost table flag (unused), y = batch lod (unused in compacted path)
} pc;

layout(location = 0) out vec3 outColor;

void main() {
    BodyInstance bi = inst[gl_InstanceIndex]; // compact lists start at 0
    vec3 world = bi.pos_radius.xyz + bi.pos_radius.w * inPos;
    gl_Position = pc.viewProj * vec4(world, 1.0);

    // color_flag.w = selected(0/1) + emissive*2 (packed by app::pack_body_instance)
    bool emissive = bi.color_flag.w >= 1.5;
    bool selected = (bi.color_flag.w - (emissive ? 2.0 : 0.0)) >= 0.5;
    float light;
    if (emissive) {
        light = 1.0;
    } else {
        vec3 nrm = inPos;
        vec3 ldir = normalize(pc.sunPosEmis.xyz - bi.pos_radius.xyz);
        light = max(dot(nrm, ldir), 0.03);
    }
    float boost = selected ? 1.3 : 1.0;
    outColor = min(vec3(1.0), bi.color_flag.rgb * boost) * light * 3.0; // x3: HDR domain gain
}
