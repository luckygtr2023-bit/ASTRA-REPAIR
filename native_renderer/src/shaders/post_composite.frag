#version 450
// ASTRA COSMOS v0.5 — final composite: HDR scene + bloom, exposure, and an
// ACES-approximation filmic display transform (Narkowicz 2015 fit of the
// ACES RRT; CINEMATIC display transform only — scientific state untouched).
// Output is LINEAR: the swapchain attachment is *SRGB, whose fixed-function
// hardware applies the sRGB EOTF on write (no manual gamma duplication).

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D hdrScene;
layout(set = 0, binding = 1) uniform sampler2D bloomTex;

layout(push_constant) uniform CompositePC { vec4 params; } pc; // x = exposure, y = bloom strength

vec3 aces_approx(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(hdrScene, inUV).rgb;
    vec3 bloom = texture(bloomTex, inUV).rgb;
    vec3 linear = pc.params.x * (hdr + pc.params.y * bloom);
    outColor = vec4(aces_approx(linear), 1.0);
}
