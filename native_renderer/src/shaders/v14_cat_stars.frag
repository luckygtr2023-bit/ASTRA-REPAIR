#version 450
// Point-star bloom: soft radial falloff, additive into the HDR target.
layout(location = 0) in vec3 v_rgb;
layout(location = 1) in vec2 v_disc;
layout(location = 2) in float v_boost;
layout(location = 0) out vec4 out_color;

void main() {
    float r2 = dot(v_disc, v_disc);
    if (r2 > 1.0) discard;
    float fall = exp(-r2 * 4.0);
    float core = exp(-r2 * 22.0) * 1.6;
    vec3 col = (v_rgb * (fall + core)) * (1.0 + v_boost);
    out_color = vec4(col, fall);
}
