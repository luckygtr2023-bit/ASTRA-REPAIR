#version 450
// ASTRA COSMOS v1.7 — CAS-class contrast-adaptive sharpening (algorithmic
// family: AMD FidelityFX CAS, published algorithm; this is an in-repo
// implementation of the published math, not the AMD source). CINEMATIC
// display transform only — display sharpening never touches scientific
// state. Operates on the display-linear composite result.
// STATUS: shader authored + compile-verified (glslangValidator). Pipeline
// integration is gated on the Windows/Vulkan run — NOT VERIFIED until then.

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTex;

layout(push_constant) uniform CasPC { vec4 params; } pc; // x = sharpness in [0,1]

void main() {
    ivec2 sz = textureSize(inputTex, 0);
    vec2 texel = 1.0 / vec2(sz);

    vec3 b = texture(inputTex, inUV - vec2(0.0, texel.y)).rgb;
    vec3 d = texture(inputTex, inUV - vec2(texel.x, 0.0)).rgb;
    vec3 e = texture(inputTex, inUV).rgb;
    vec3 f = texture(inputTex, inUV + vec2(texel.x, 0.0)).rgb;
    vec3 h = texture(inputTex, inUV + vec2(0.0, texel.y)).rgb;

    // min/max of the cross + center, luma-domain
    vec3 mn = min(min(b, d), min(min(e, f), h));
    vec3 mx = max(max(b, d), max(max(e, f), h));

    // CAS weight: w = -1 / mix(8, 5, sharpness)
    float sharp = clamp(pc.params.x, 0.0, 1.0);
    float wDenom = mix(8.0, 5.0, sharp);
    vec3 amp = sqrt(min(mn, 1.0 - mx));             // soft peak limiting
    vec3 w = -amp / wDenom;

    vec3 rcpW = 1.0 / (1.0 + 4.0 * w);
    outColor = vec4(clamp((b + d + f + h + e) * w + e, 0.0, 1.0) * rcpW, 1.0);
}
