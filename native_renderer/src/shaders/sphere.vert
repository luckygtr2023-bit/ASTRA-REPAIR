#version 450
// ASTRA COSMOS — celestial body vertex shader.
// Consumes the SAME per-draw constants the CPU builds from the simulation
// (floating-origin render units; scientific state stays in the engine).

layout(location = 0) in vec3 inPos;   // unit icosphere

layout(push_constant) uniform BodyPC {
    mat4 mvp;        // proj * view * model
    vec4 bodyPosRad; // xyz = render position (target-relative), w = visual radius
    vec4 sunPosEmis; // xyz = sun render position, w = 1.0 => emissive (star)
    vec4 color;      // SIMULATED material color
} pc;

layout(location = 0) out vec3 outColor;

void main() {
    vec3 world = pc.bodyPosRad.xyz + pc.bodyPosRad.w * inPos;
    gl_Position = pc.mvp * vec4(inPos, 1.0);

    float light;
    if (pc.sunPosEmis.w > 0.5) {
        light = 1.0; // star: self-luminous
    } else {
        vec3 nrm = inPos;                 // unit sphere: normal == position
        vec3 ldir = normalize(pc.sunPosEmis.xyz - pc.bodyPosRad.xyz);
        light = max(dot(nrm, ldir), 0.03); // lambert + small ambient
    }
    outColor = pc.color.rgb * light;
}
