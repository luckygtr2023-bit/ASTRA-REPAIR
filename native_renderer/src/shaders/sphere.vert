#version 450
// ASTRA COSMOS v0.5 — INSTANCED celestial body vertex shader.
// Instance data: authoritative RenderState-derived SSBO (camera-relative).
// LOD/visibility: GPU-side deferral via per-instance mask slot from the
// culling compute dispatch (deterministic, fixed-slot writes; 0=invisible,
// 1=LOW-LOD batch, 2=HIGH-LOD batch, matched against gl_InstanceID batch).
// Scientific state is NEVER modified here (visualization boundary).

layout(location = 0) in vec3 inPos; // unit icosphere (16-bit indexed, both LOD meshes share layout)

struct BodyInstance { vec4 pos_radius; vec4 color_flag; }; // 32B
layout(std430, binding = 0) readonly buffer Instances { BodyInstance inst[]; };
layout(std430, binding = 1) readonly buffer LodMask { uint mask[]; }; // 1=LOW, 2=HIGH

layout(push_constant) uniform InstPC {
    mat4 viewProj;
    vec4 sunPosEmis; // xyz sun render pos, w = 1.0 => lighting on (kept: star itself is emissive via color_flag? no — sun emissive by index flag below)
    vec4 misc;       // x = selected index (-1 none), y = batch lod (1 or 2), z/w unused
} pc;

layout(location = 0) out vec3 outColor;

void main() {
    BodyInstance bi = inst[gl_InstanceIndex];
    // GPU-side visibility gate (compute-determined): cull to a zero-area
    // position at the far plane when this batch must not draw the instance.
    bool draw = (mask[gl_InstanceIndex] == uint(pc.misc.y));
    vec3 world = bi.pos_radius.xyz + bi.pos_radius.w * inPos;
    vec4 clip = draw ? (pc.viewProj * vec4(world, 1.0)) : vec4(0.0, 0.0, 2.0, 1.0);
    gl_Position = clip;

    float light;
    // The star self-emits (index 0 records carry emissive via color_flag.w>=2;
    // simpler: index 0 of the table is the star — provenance in app layer).
    bool emissive = (gl_InstanceIndex == 0);
    if (emissive) {
        light = 1.0;
    } else {
        vec3 nrm = inPos;
        vec3 ldir = normalize(pc.sunPosEmis.xyz - bi.pos_radius.xyz);
        light = max(dot(nrm, ldir), 0.03);
    }
    float boost = (float(gl_InstanceIndex) == pc.misc.x) ? 1.3 : 1.0; // authoritative selection index
    outColor = min(vec3(1.0), bi.color_flag.rgb * boost) * light * 3.0; // x3: HDR domain gain (HDR pipeline)
}
