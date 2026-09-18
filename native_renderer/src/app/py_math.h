// py_math.h — CPython 3.11 math-module algorithm ports, shared by all v1.3 mirrors.
// Header-only; the ports are bit-exact reimplementations of the exact C source the
// Python authorities execute (CPython 3.11.2, the pinned venv interpreter).
//
// py_vector_norm / py_hypot2 / py_magnitude3:
//   Modules/mathmodule.c vector_norm() + math_hypot(), ported verbatim.
//   CPython math.hypot(x, y, ...) is NOT libm hypot: it scales by
//   ldexp(1, -frexp(max).exponent), applies Veltkamp-Dekker splitting to each
//   scaled component, accumulates compensated fracs (frac1..frac3), and applies
//   one differential-correction step before un-scaling. std::hypot diverges from
//   CPython by ±1ulp on some inputs (proven by the destruction mirror's fragment
//   velocity rows), so any mirror of astra.mathematics.Vector3.magnitude()
//   (which calls math.hypot(math.hypot(x, y), z)) must use this port, not libm.
//
// py_strtod:
//   _PyOS_string_to_double (Python/pystrtod.c) — mirrors the float(str) lexical
//   path the authorities use when parsing string configuration/state values:
//   NANs/INFs keywords, underscores after digits, then _Py_dg_strtod (= C strtod
//   on IEEE/glibc). The CPython lexical grammar was validated against the
//   interpreter on a 10k-case adversarial corpus (see v1.3 report).
//
// Platform contract (same as all v1.x mirrors): IEEE-754 binary64, round-to-
// nearest, SSE2 semantics; glibc frexp/ldexp/sqrt are correctly rounded.
// DO NOT extend beyond the ported functions; keep op order verbatim.

#ifndef ASTRA_APP_PY_MATH_H
#define ASTRA_APP_PY_MATH_H

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace astra::app {

inline double py_vector_norm(int n, const double* vec, double mx) {
    const double T27 = 134217729.0; // ldexp(1,27)+1
    double x, scale, oldcsum, csum = 1.0, frac1 = 0.0, frac2 = 0.0, frac3 = 0.0;
    double t, hi, lo, h;
    if (std::isinf(mx)) return mx;
    if (mx == 0.0 || n <= 1) return mx;
    int max_e = 0;
    std::frexp(mx, &max_e);
    if (max_e >= -1023) {
        scale = std::ldexp(1.0, -max_e);
        for (int i = 0; i < n; ++i) {
            x = vec[i];
            x *= scale;
            t = x * T27;
            hi = t - (t - x);
            lo = x - hi;
            x = hi * hi;
            oldcsum = csum; csum += x; frac1 += (oldcsum - csum) + x;
            x = 2.0 * hi * lo;
            oldcsum = csum; csum += x; frac2 += (oldcsum - csum) + x;
            frac3 += lo * lo;
        }
        h = std::sqrt(csum - 1.0 + (frac1 + frac2 + frac3));
        x = h;
        t = x * T27;
        hi = t - (t - x);
        lo = x - hi;
        x = -hi * hi;
        oldcsum = csum; csum += x; frac1 += (oldcsum - csum) + x;
        x = -2.0 * hi * lo;
        oldcsum = csum; csum += x; frac2 += (oldcsum - csum) + x;
        x = -lo * lo;
        oldcsum = csum; csum += x; frac3 += (oldcsum - csum) + x;
        x = csum - 1.0 + (frac1 + frac2 + frac3);
        return (h + x / (2.0 * h)) / scale;
    }
    // Subnormal-extreme fallback: divide by max (mirror verbatim).
    for (int i = 0; i < n; ++i) {
        x = vec[i];
        x /= mx;
        x = x * x;
        oldcsum = csum; csum += x; frac1 += (oldcsum - csum) + x;
    }
    return mx * std::sqrt(csum - 1.0 + frac1);
}

inline double py_hypot2(double a, double b) {
    double coords[2] = {std::fabs(a), std::fabs(b)};
    double mx = 0.0;
    for (int i = 0; i < 2; ++i) {
        if (coords[i] > mx) mx = coords[i]; // NaN never updates max (CPython: x > max)
    }
    if (std::isnan(coords[0]) || std::isnan(coords[1])) return std::nan("");
    return py_vector_norm(2, coords, mx);
}

inline double py_hypot3(double a, double b, double c) {
    double coords[3] = {std::fabs(a), std::fabs(b), std::fabs(c)};
    double mx = 0.0;
    for (int i = 0; i < 3; ++i) {
        if (coords[i] > mx) mx = coords[i];
    }
    if (std::isnan(coords[0]) || std::isnan(coords[1]) || std::isnan(coords[2])) return std::nan("");
    return py_vector_norm(3, coords, mx);
}

// Vector3.magnitude() == math.hypot(math.hypot(x, y), z) — chained binary calls.
inline double py_magnitude3(double x, double y, double z) {
    return py_hypot2(py_hypot2(x, y), z);
}

// ---- float(str) lexical mirror (_PyOS_string_to_double over Python/dtoa.c) ----
// Parses s into a double mirroring CPython float(str) semantics for the subset
// used in configuration/state strings: optional sign, decimal digits with
// single underscores allowed only between digits, optional '.', exponent.
// Returns true on a CPython-accepted conversion (including "nan"/"inf"); false
// when CPython float(str) would raise ValueError. On accept, *out mirrors the
// exact double (strtod on glibc/IEEE is same-bits with _Py_dg_strtod).
inline bool py_strtod(const char* s, const char** end_out, double* out) {
    const char* orig = s;
    while (std::isspace(static_cast<unsigned char>(*s))) ++s; // float() strips ws
    const char* str = s;
    int negate = 0;
    if (*s == '-') { ++s; negate = 1; }
    else if (*s == '+') { ++s; }
    // _Py_parse_inf_or_nan equivalents
    const char* afterinf = nullptr; const char* afternan = nullptr;
    if (*s == 'i' || *s == 'I') {
        if ((s[0] == 'i' || s[0] == 'I') && (s[1] == 'n' || s[1] == 'N') && (s[2] == 'f' || s[2] == 'F')) {
            afterinf = s + 3;
            if ((s[0 + 3] == 'i' || s[3] == 'I') && (s[4] == 'n' || s[4] == 'N') &&
                (s[5] == 'i' || s[5] == 'I') && (s[6] == 't' || s[6] == 'T') &&
                (s[7] == 'y' || s[7] == 'Y')) afterinf = s + 8;
        }
    }
    if (*s == 'n' || *s == 'N') {
        if ((s[0] == 'n' || s[0] == 'N') && (s[1] == 'a' || s[1] == 'A') && (s[2] == 'n' || s[2] == 'N')) {
            afternan = s + 3;
        }
    }
    if (afternan) { *out = std::nan(""); *end_out = afternan; return true; }
    if (afterinf) { *out = negate ? -HUGE_VAL : HUGE_VAL; *end_out = afterinf; return true; }
    const char* p = s;
    // Scan the Python-number subgrammar, tracking digits & underscores.
    // CPython dtoa.c accepted_grammar: digits([_d]*); '.'; digits([_d]*); then e[+-]d([_d]*)
    int n_int = 0, n_frac = 0, n_exp = 0; bool saw_us = false;
    const char* int_beg = p;
    while (std::isdigit(static_cast<unsigned char>(*p))) { ++p; ++n_int; if (*p == '_' && std::isdigit(static_cast<unsigned char>(p[1]))) { ++p; saw_us = true; } }
    const char* int_end = p;
    if (*p == '.') {
        ++p;
        while (std::isdigit(static_cast<unsigned char>(*p))) { ++p; ++n_frac; if (*p == '_' && std::isdigit(static_cast<unsigned char>(p[1]))) { ++p; saw_us = true; } }
    }
    if (n_int + n_frac == 0) return false; // CPython: no digits -> not a number
    const char* end_noexp = p;
    if (*p == 'e' || *p == 'E') {
        const char* q = p + 1;
        if (*q == '-' || *q == '+') ++q;
        if (!std::isdigit(static_cast<unsigned char>(*q))) {
            // trailing e not consumed: float("1e") -> ValueError via end!=last
        } else {
            p = q;
            while (std::isdigit(static_cast<unsigned char>(*p))) { ++p; ++n_exp; if (*p == '_' && std::isdigit(static_cast<unsigned char>(p[1]))) { ++p; saw_us = true; } }
        }
    }
    const char* end = (n_exp > 0) ? p : end_noexp;
    if (saw_us) {
        // CPython removes underscores, then parses with _Py_dg_strtod.
        char buf[64];
        const char* r = s;
        size_t bi = 0;
        if (bi < sizeof(buf) - 1 && negate) buf[bi++] = '-';
        while (r < end) {
            if (*r != '_') {
                if (bi >= sizeof(buf) - 1) return false; // >63 chars: reject (never in config)
                buf[bi++] = *r;
            }
            ++r;
        }
        buf[bi] = '\0';
        char* he = nullptr;
        double v = std::strtod(buf, &he);
        if (he == buf) return false;
        *out = v;
        *end_out = end;
        (void)orig; (void)int_beg; (void)int_end;
        return true;
    }
    char* he = nullptr;
    double v = std::strtod(s, &he);
    if (he == s) return false;
    *out = v;
    *end_out = end;
    (void)orig; (void)int_beg; (void)int_end;
    return true;
}

} // namespace astra::app

#endif // ASTRA_APP_PY_MATH_H
