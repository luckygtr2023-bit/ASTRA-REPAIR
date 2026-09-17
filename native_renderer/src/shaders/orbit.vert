#version 450
// ASTRA COSMOS — orbit trajectory overlay vertex shader.
// Evaluates the SAME Kepler model as the scientific engine
// (M -> E via Newton-Raphson -> true anomaly -> perifocal -> IJK rotation;
// mirror of astra/orbital/kepler.py, anomalies.py, elements.py).
// Vertex index j sweeps one full period in mean anomaly.

layout(push_constant) uniform OrbitPC {
    mat4 viewProj;
    vec4 e1;      // a_km, e, i, raan
    vec4 e2;      // argp, M_start(wrapped), km->render scale, unused
    vec4 parent;  // parent body render position (target-relative), unused
    vec4 color;   // SIMULATED overlay color
} pc;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265358979;
const float TWO_PI = 6.28318530717959;

void main() {
    const int N = 128;
    float j = float(gl_VertexIndex % N);
    float M = pc.e2.y + TWO_PI * (j / float(N));

    // Kepler elliptic solve (Newton-Raphson) — e is small for planets (<=0.206),
    // 12 iterations reach the engine's 1e-12 policy comfortably.
    float e = pc.e1.y;
    float E = (e < 0.8) ? M : PI * sign(M);
    for (int k = 0; k < 12; ++k) {
        float f = E - e * sin(E) - M;
        float fp = 1.0 - e * cos(E);
        E += -f / fp;
    }
    float nu = 2.0 * atan(sqrt((1.0 + e) / (1.0 - e)) * tan(0.5 * E));

    float a = pc.e1.x;
    float p = a * (1.0 - e * e);
    float r = p / (1.0 + e * cos(nu));
    vec3 pqw = vec3(r * cos(nu), r * sin(nu), 0.0);

    float cO = cos(pc.e1.w), sO = sin(pc.e1.w);
    float ci = cos(pc.e1.z), si = sin(pc.e1.z);
    float cw = cos(pc.e2.x), sw = sin(pc.e2.x);
    vec3 ijk = vec3(
        (cO * cw - sO * ci * sw) * pqw.x + (-cO * sw - sO * ci * cw) * pqw.y + (sO * si) * pqw.z,
        (sO * cw + cO * ci * sw) * pqw.x + (-sO * sw + cO * ci * cw) * pqw.y + (-cO * si) * pqw.z,
        (si * sw) * pqw.x + (si * cw) * pqw.y + ci * pqw.z);

    vec3 world = pc.parent.xyz + ijk * pc.e2.z;
    gl_Position = pc.viewProj * vec4(world, 1.0);
    outColor = pc.color;
}
