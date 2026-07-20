#pragma once
// WaspFilter.hpp — EDP Wasp state-variable VCF nonlinear core (header-only).
//
// Circuit model, Rev-1 physics, and solver: see
//   docs/superpowers/specs/2026-07-15-vespid-dsp-design.md
// Fitted constants (authoritative): tests/vespid_ref/fitted_constants.md
// Behavioral golden reference:       tests/vespid_ref/wasp_ref.py
//
// One WaspFilter per audio channel. No Rack includes. The caller pre-warps the
// cutoff and computes kC2eff per the Rev-1 self-oscillation mechanism:
//     fcInt = clamp(fc*wcComp, 0.25, 0.45*fsInt)
//     g     = tan(pi*fcInt/fsInt)                       (per-sample slewed)
//     wcInt = 2*pi*fcInt
//     kC2   = R3*C2*wcInt - kR2*wcInt/(2*pi*fPole)       (MAY be negative)
//     h1    = computeH1(rho, fsInt)
// and passes them to process(). kC2 negative is the self-oscillation mechanism
// (the inverter's finite-bandwidth phase lag folded into the damping term) and
// must NOT be clamped.

#include "wasp_dsp_utils.hpp"
#include <cmath>
#include <algorithm>

namespace wasp {

// Per-mode circuit constants. Values are the fitted set from
// fitted_constants.md (matching wasp_ref.py MODES exactly, so this core
// reproduces golden.json). wcComp = 1/sqrt(lambda*kR2),
// lambda = invA0/(nInv+invA0), precomputed:
//   German:  lambda = 17.88/24.58 = 0.7274207, wcComp = 0.60954726
//   British: lambda = 23.44/27.44 = 0.8542274, wcComp = 1.08196598
struct ModeConfig {
    float c;        // OTA tanh scale (1/V)
    float kR2;      // R3/R2eff LP-feedback ratio
    float nInv;     // summing-node divider factor
    float wcComp;   // knob-true cutoff compensation (fcInt = fc*wcComp)
    float vHi, vLo; // rail headroom (V, positive numbers); also rail-clamp bounds
    float invA0;    // inverter open-loop gain magnitude
    float makeup;   // output makeup gain
    // Caller-applied (like wcComp), NOT used inside the core: hardware level
    // staging, folded into the module's drive gain. Anchors a full-scale
    // host signal (5 V peak) at each unit's nominal-hot source level: the
    // Doepfer A-124 is Euro-native (5 V, 1:1); the EDP Wasp was fed by its
    // own 0-5 V logic-swing oscillators (+/-2.5 V AC-coupled, level pot
    // <= unity), so 0.25 x the module's 2x drive floor = 0.5x.
    // See 2026-07-19-vespid-input-calibration-design.md.
    float inGain;
};

constexpr ModeConfig kGerman = {
    /*c*/0.296f, /*kR2*/3.70f, /*nInv*/6.70f, /*wcComp*/0.60954726f,
    /*vHi*/3.031f, /*vLo*/8.500f, /*invA0*/17.88f, /*makeup*/2.0f,
    /*inGain*/1.0f };
// British makeup 2.5x (was 1.0 through 2026-07-19): level-matches the two
// modes at drive 0 — measured makeup'd LP RMS 3.89 V (British) vs 3.76 V
// (German) at the drive-0 operating points, +0.3 dB. At full drive
// British stays ~4.6 dB quieter: its rails (+/-1.7/3.1 V) simply hold
// less signal than German's (+/-3.0/8.5 V). That residual is the
// hardware speaking; Output level covers it when it matters.
constexpr ModeConfig kBritish = {
    /*c*/0.155f, /*kR2*/1.00f, /*nInv*/4.00f, /*wcComp*/1.08196598f,
    /*vHi*/1.708f, /*vLo*/3.105f, /*invA0*/23.44f, /*makeup*/2.5f,
    /*inGain*/0.25f };

class WaspFilter {
public:
    struct Out { float lp, bp, hp; };

    // fsInternal is the oversampled rate the core runs at.
    void setSampleRate(float fsInternal) {
        fsInt = fsInternal;
        hinA  = 2.f * float(M_PI) * 22.f / fsInt;   // 22 Hz input HPF coeff
        dcLp.setSampleRate(fsInt);
        dcBp.setSampleRate(fsInt);
        dcHp.setSampleRate(fsInt);
    }

    // Switching mode carries all state (no reset) — matches spec.
    void setMode(const ModeConfig& m) { mode = &m; }

    void reset() {
        sBP = sLP = z1 = hinState = 0.f;
        hpPrev = bpPrev = ydPrev = vgPrev = 0.f;
        rawLp = rawBp = rawHp = 0.f;
        dcLp.reset(); dcBp.reset(); dcHp.reset();
    }

    bool stateFinite() const {
        return std::isfinite(sBP) && std::isfinite(sLP) && std::isfinite(z1) &&
               std::isfinite(hinState) && std::isfinite(hpPrev) &&
               std::isfinite(bpPrev) && std::isfinite(ydPrev) && std::isfinite(vgPrev);
    }

    // Raw states from the last process() call: pre-makeup, pre-DC-blocker.
    // Golden numbers are raw (small-signal on raw LP, self-osc on raw BP), so
    // tests measure these directly.
    Out raw() const { return { rawLp, rawBp, rawHp }; }

    // One oversampled sample. g/h1/kC2 as documented at the top of the file.
    // highAcc = add 2 Newton iterations on top of the fixed-pivot solve.
    Out process(float inVolts, float g, const H1Coeffs& h1, float kC2, bool highAcc) {
        const ModeConfig& m = *mode;

        // Input HPF (22 Hz at fsInt) — C1/level-pot AC coupling.
        float hin = inVolts - hinState;
        hinState += hinA * hin;

        // ---- fixed-pivot (mystran) linear pass ----------------------------
        // S(v) = tanh(c*v)/c ~= tanhXdX(c*vPivot)*v (secant through last arg).
        float th = tanhXdX(m.c * hpPrev);
        float tb = tanhXdX(m.c * bpPrev);
        // Diode secant fd(yd) ~= sd*yd; sd=1 with diodes off (|yd| small).
        float sd = 1.f;
        if (std::fabs(ydPrev) > 0.2f) {
            float arg = std::clamp(ydPrev / kVD, -30.f, 30.f);
            sd = 1.f + kKD * std::sinh(arg) / ydPrev;
        }
        float A  = finvSlope(vgPrev);   // d finv / d vg (negative)
        float F0 = finv(vgPrev);
        float gth = g * th, gtb = g * tb;
        // sum s = P + Q*hp (bp linearized in hp; lp/yd/S(bp) chain off bp).
        float Q = gth * (sd * h1.beta0 + gtb * m.kR2 + kC2 * tb);
        float P = sd * (h1.beta0 * sBP + z1) + m.kR2 * (sLP + gtb * sBP)
                  + kC2 * tb * sBP + hin;
        // hp = F0 + A*((P + Q*hp + hp)/nInv - vgPrev)  ->  solve for hp.
        float denom = 1.f - A * (Q + 1.f) / m.nInv;
        float hp = (F0 - A * vgPrev + A * P / m.nInv) / std::max(denom, 0.05f);

        // ---- optional Newton refinement (2 iterations) --------------------
        // Derivatives use the exact 1 - t*t form for all tanh terms (plan
        // recommendation); tanhXdX stays confined to the secant pivots above.
        if (highAcc) {
            for (int it = 0; it < 2; ++it) {
                float th2 = tanhApprox(m.c * hp);
                float Sh  = th2 / m.c;
                float bp  = sBP + g * Sh;
                float tb2 = tanhApprox(m.c * bp);
                float Sb  = tb2 / m.c;
                float lp  = sLP + g * Sb;
                float yd  = h1.beta0 * bp + z1;
                float dArg = std::clamp(yd / kVD, -30.f, 30.f);
                float fd  = yd + kKD * std::sinh(dArg);
                float sum = hin + fd + m.kR2 * lp + kC2 * Sb;
                float vg  = (sum + hp) / m.nInv;
                float r   = hp - finv(vg);
                // chain rule dr/dhp
                float dSh = 1.f - th2 * th2;          // d Sh / d hp
                float dbp = g * dSh;
                float dSb = (1.f - tb2 * tb2) * dbp;   // d Sb / d hp
                float dlp = g * dSb;
                float dyd = h1.beta0 * dbp;
                float dfd = dyd * (1.f + kKD * std::cosh(dArg) / kVD);
                float dsum = dfd + m.kR2 * dlp + kC2 * dSb;
                float drdhp = 1.f - finvSlope(vg) * (dsum + 1.f) / m.nInv;
                float step = r / std::max(drdhp, 0.25f);
                step = std::clamp(step, -2.f, 2.f);
                hp -= step;
            }
        }

        // The inverter output physically cannot exceed its rails, so hp is
        // bounded to finv's range [-vLo, vHi]. Clamping here is both correct
        // physics and solver robustness: under extreme overdrive the pivot
        // linearization can overshoot far past the rails and two clamped
        // Newton steps cannot recover — this keeps the node bounded without
        // disturbing the in-range fixed point.
        hp = std::clamp(hp, -m.vLo, m.vHi);

        // ---- commit: recompute chain at solved hp, update states ----------
        float Sh = tanhApprox(m.c * hp) / m.c;
        float bp = sBP + g * Sh;
        float Sb = tanhApprox(m.c * bp) / m.c;
        float lp = sLP + g * Sb;
        float yd = h1.beta0 * bp + z1;
        float dArg = std::clamp(yd / kVD, -30.f, 30.f);
        float vg = (hin + (yd + kKD * std::sinh(dArg)) + m.kR2 * lp + kC2 * Sb + hp)
                   / m.nInv;

        sBP = railClamp(2.f * bp - sBP, m.vHi, m.vLo);
        sLP = railClamp(2.f * lp - sLP, m.vHi, m.vLo);
        z1  = h1.beta1 * bp - h1.alpha1 * yd;
        hpPrev = hp; bpPrev = bp; ydPrev = yd; vgPrev = vg;

        rawLp = lp; rawBp = bp; rawHp = hp;
        return { dcLp.process(lp) * m.makeup,
                 dcBp.process(bp) * m.makeup,
                 dcHp.process(hp) * m.makeup };
    }

private:
    // Diode pair: Rf=R3=27k, Is=2.52 nA, eta=1.752, VT=25.852 mV.
    static constexpr float kKD = 27e3f * 2.f * 2.52e-9f;   // R3*2*Is = 1.3608e-4
    static constexpr float kVD = 1.752f * 0.025852f;       // eta*VT   = 0.045293

    // Inverter saturator: asymmetric tanh (fit_inverter.py).
    float finv(float vg) const {
        const ModeConfig& m = *mode;
        if (vg <= 0.f) return  m.vHi * tanhApprox(-m.invA0 * vg / m.vHi);
        return                -m.vLo * tanhApprox( m.invA0 * vg / m.vLo);
    }
    // d finv / d vg (always negative). Exact derivative of a*tanh(b*vg/a) is
    // b*sech^2 = b*(1 - t^2) with t the tanh value; here b = -invA0.
    float finvSlope(float vg) const {
        const ModeConfig& m = *mode;
        float t = (vg <= 0.f) ? tanhApprox(-m.invA0 * vg / m.vHi)
                              : tanhApprox( m.invA0 * vg / m.vLo);
        return -m.invA0 * (1.f - t * t);
    }

    const ModeConfig* mode = &kGerman;
    float fsInt = 192000.f, hinA = 2.f * float(M_PI) * 22.f / 192000.f;
    float sBP = 0.f, sLP = 0.f, z1 = 0.f, hinState = 0.f;
    float hpPrev = 0.f, bpPrev = 0.f, ydPrev = 0.f, vgPrev = 0.f;
    float rawLp = 0.f, rawBp = 0.f, rawHp = 0.f;
    DcBlocker dcLp, dcBp, dcHp;
};

} // namespace wasp
