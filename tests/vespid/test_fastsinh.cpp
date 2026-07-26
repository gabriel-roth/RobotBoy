// test_fastsinh.cpp — accuracy guard for wasp::sinhFast (src/vespid/fastsinh.hpp).
// The Wasp diode term clamps its argument to [-30, 30]; sinhFast must stay
// within 2e-5 relative of libm sinh over that whole range, be exactly 0 at 0,
// and be smooth across the |x| = 0.5 Taylor/exp handoff.
#include "vespid/fastsinh.hpp"
#include <cmath>
#include <cstdio>
#include <initializer_list>

static int failures = 0;
static void report(bool ok, const char* what, const char* detail) {
    printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
           detail[0] ? " — " : "", detail);
    if (!ok) failures++;
}

int main() {
    printf("fastsinh accuracy\n");

    // 1. Max relative error over the clamp range.
    double maxRel = 0; double worstX = 0;
    for (double x = -30.0; x <= 30.0; x += 1e-4) {
        double f = (double)wasp::sinhFast((float)x);
        double r = std::sinh(x);
        double rel = std::fabs(f - r) / std::fmax(1e-30, std::fabs(r));
        if (rel > maxRel) { maxRel = rel; worstX = x; }
    }
    char buf[128];
    snprintf(buf, sizeof buf, "max rel err %.3e at x=%.4f (limit 2e-5)", maxRel, worstX);
    report(maxRel < 2e-5, "relative error over [-30, 30]", buf);

    // 2. Exact zero at zero.
    report(wasp::sinhFast(0.f) == 0.f, "sinhFast(0) == 0", "");

    // 3. Continuity across the |x| = 0.5 handoff: adjacent evaluations a
    // hair either side of the branch differ from true sinh by < 1e-5 rel.
    double lo = (double)wasp::sinhFast(0.4999f), hi = (double)wasp::sinhFast(0.5001f);
    double relLo = std::fabs(lo - std::sinh(0.4999)) / std::sinh(0.4999);
    double relHi = std::fabs(hi - std::sinh(0.5001)) / std::sinh(0.5001);
    snprintf(buf, sizeof buf, "rel err below %.2e / above %.2e", relLo, relHi);
    report(relLo < 1e-5 && relHi < 1e-5, "handoff continuity at |x|=0.5", buf);

    // 4. Odd symmetry (bit-exact by construction; guard the branches).
    bool odd = true;
    for (float x : {0.1f, 0.5f, 3.f, 29.9f})
        odd = odd && (wasp::sinhFast(-x) == -wasp::sinhFast(x));
    report(odd, "odd symmetry", "");

    // 5. sinhCoshFast: sinh matches sinhFast bit-exactly (same formulas),
    // cosh stays within 2e-5 relative of libm over the clamp range.
    bool shSame = true;
    double maxRelC = 0; double worstC = 0;
    for (double x = -30.0; x <= 30.0; x += 1e-4) {
        wasp::SinhCosh sc = wasp::sinhCoshFast((float)x);
        shSame = shSame && (sc.sinh == wasp::sinhFast((float)x));
        double rel = std::fabs((double)sc.cosh - std::cosh(x)) / std::cosh(x);
        if (rel > maxRelC) { maxRelC = rel; worstC = x; }
    }
    report(shSame, "sinhCoshFast.sinh == sinhFast", "");
    snprintf(buf, sizeof buf, "max rel err %.3e at x=%.4f (limit 2e-5)", maxRelC, worstC);
    report(maxRelC < 2e-5, "sinhCoshFast.cosh over [-30, 30]", buf);

    printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
