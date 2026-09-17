#version 450
// ASTRA COSMOS — background pass: deep-space gradient + deterministic starfield.
// Hash matches the project's deterministic convention (common.glsl 43758.5453,
// seed policy 0xA573). Stars here are CINEMATIC background sky (not catalog
// data); catalog/data-derived objects are rendered as simulated bodies.

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    float sim_time;
    float width;
    float height;
    float frame;
} pc;

float astra_hash(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}

void main() {
    // Deep-space gradient (very subtle vertical falloff).
    vec3 base = mix(vec3(0.004, 0.004, 0.010), vec3(0.010, 0.012, 0.028), inUV.y);

    // Deterministic star speckle — two density layers, gentle time twinkle.
    float stars = 0.0;
    for (int layer = 0; layer < 2; ++layer) {
        float scale = (layer == 0) ? 160.0 : 90.0;
        vec2 grid = floor(inUV * scale);
        vec2 cell = fract(inUV * scale) - 0.5;
        float h = astra_hash(vec3(grid, float(layer)));
        // Sparse threshold ~1.2% of cells host a star.
        if (h > 0.988) {
            vec2 off = vec2(astra_hash(vec3(grid, 7.0)) - 0.5,
                            astra_hash(vec3(grid, 13.0)) - 0.5) * 0.6;
            float d = length(cell - off);
            float mag = 0.5 + 0.5 * astra_hash(vec3(grid, 29.0));
            float tw = 0.75 + 0.25 * sin(pc.sim_time * (0.3 + h) + h * 40.0);
            stars += mag * tw * smoothstep(0.10, 0.0, d);
        }
    }

    outColor = vec4(base + vec3(stars), 1.0);
}
