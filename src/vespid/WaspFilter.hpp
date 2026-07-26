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
#include "fastsinh.hpp"
#include <cmath>
#include <algorithm>

namespace wasp {

// Per-mode circuit constants. Values are the fitted set from
// fitted_constants.md (matching wasp_ref.py MODES, except British makeup:
// 2.5 here vs 1.0 in the ref — see the note above kBritish. Golden
// comparisons are unaffected; they measure pre-makeup states, so this core
// still reproduces golden.json). wcComp = 1/sqrt(lambda*kR2),
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
    // Derived reciprocals, constexpr-computed in the initializers below.
    // The MetaModule SDK compiles without -ffast-math, so every division by
    // a per-mode constant in the hot path is a real (unpipelined, ~10-14
    // cycle) VDIV.F32 on the Cortex-A7 unless precomputed here — see
    // cpu-optimization-2026-07-24.md §3.4.
    float invNInv;     // 1/nInv
    float a0OverVHi;   // invA0/vHi
    float a0OverVLo;   // invA0/vLo
};

constexpr ModeConfig kGerman = {
    /*c*/0.296f, /*kR2*/3.70f, /*nInv*/6.70f, /*wcComp*/0.60954726f,
    /*vHi*/3.031f, /*vLo*/8.500f, /*invA0*/17.88f, /*makeup*/2.0f,
    /*inGain*/1.0f,
    /*invNInv*/1.f/6.70f, /*a0OverVHi*/17.88f/3.031f, /*a0OverVLo*/17.88f/8.500f };
// British makeup 2.5x (was 1.0 through 2026-07-19): level-matches the two
// modes at drive 0 — measured makeup'd LP RMS 3.89 V (British) vs 3.76 V
// (German) at the drive-0 operating points, +0.3 dB. At full drive
// British stays ~4.6 dB quieter: its rails (+/-1.7/3.1 V) simply hold
// less signal than German's (+/-3.0/8.5 V). That residual is the
// hardware speaking; Output level covers it when it matters.
constexpr ModeConfig kBritish = {
    /*c*/0.155f, /*kR2*/1.00f, /*nInv*/4.00f, /*wcComp*/1.08196598f,
    /*vHi*/1.708f, /*vLo*/3.105f, /*invA0*/23.44f, /*makeup*/2.5f,
    /*inGain*/0.25f,
    /*invNInv*/1.f/4.00f, /*a0OverVHi*/23.44f/1.708f, /*a0OverVLo*/23.44f/3.105f };

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

    // Switching mode carries all state (no reset) — matches spec. The cached
    // secant pivots th/tb were computed with the old mode's c, so refresh
    // them here (once per user action, not per sample).
    void setMode(const ModeConfig& m) {
        if (mode == &m) return;
        mode = &m;
        th = tanhXdX(m.c * hpPrev);
        tb = tanhXdX(m.c * bpPrev);
    }

    void reset() {
        sBP = sLP = z1 = hinState = 0.f;
        hpPrev = bpPrev = ydPrev = vgPrev = 0.f;
        th = tb = 1.f;        // tanhXdX(0)
        diodePrev = 0.f;      // kKD*sinh(0)
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
    Out process(float inVolts, float g, const H1Coeffs& h1, float kC2) {
        const ModeConfig& m = *mode;

        // Input HPF (22 Hz at fsInt) — C1/level-pot AC coupling.
        float hin = inVolts - hinState;
        hinState += hinA * hin;

        // ---- fixed-pivot (mystran) linear pass ----------------------------
        // S(v) = tanh(c*v)/c ~= tanhXdX(c*vPivot)*v (secant through last arg).
        // th/tb are the cached pivots from the previous commit block (they
        // equal tanhXdX(c*hpPrev)/tanhXdX(c*bpPrev) by construction), and
        // diodePrev is the previous commit's kKD*sinh(dArg) — dArg was built
        // from the same yd that became ydPrev, so reusing it here is exact.
        // See cpu-optimization-2026-07-24.md §3.1/§3.3.
        // Diode secant fd(yd) ~= sd*yd; sd=1 with diodes off (|yd| small).
        float sd = 1.f;
        if (std::fabs(ydPrev) > 0.2f)
            sd = 1.f + diodePrev / ydPrev;
        // Inverter value and slope share one tanhApprox evaluation (§3.2):
        // F0 = asymmetric-tanh inverter value at vgPrev, A = its slope
        // = -invA0*(1 - t*t).
        float F0, A;
        if (vgPrev <= 0.f) {
            float t = tanhApprox(-m.a0OverVHi * vgPrev);
            F0 =  m.vHi * t;
            A  = -m.invA0 * (1.f - t * t);
        } else {
            float t = tanhApprox(m.a0OverVLo * vgPrev);
            F0 = -m.vLo * t;
            A  = -m.invA0 * (1.f - t * t);
        }
        float gth = g * th, gtb = g * tb;
        // sum s = P + Q*hp (bp linearized in hp; lp/yd/S(bp) chain off bp).
        float Q = gth * (sd * h1.beta0 + gtb * m.kR2 + kC2 * tb);
        float P = sd * (h1.beta0 * sBP + z1) + m.kR2 * (sLP + gtb * sBP)
                  + kC2 * tb * sBP + hin;
        // hp = F0 + A*((P + Q*hp + hp)/nInv - vgPrev)  ->  solve for hp.
        float denom = 1.f - A * (Q + 1.f) * m.invNInv;
        float hp = (F0 - A * vgPrev + A * P * m.invNInv) / std::max(denom, 0.05f);

        // The inverter output physically cannot exceed its rails, so hp is
        // bounded to the inverter's range [-vLo, vHi]. Clamping here is both correct
        // physics and solver robustness: under extreme overdrive the pivot
        // linearization can overshoot far past the rails and the single
        // fixed-pivot solve cannot recover — this keeps the node bounded
        // without disturbing the in-range fixed point.
        hp = std::clamp(hp, -m.vLo, m.vHi);

        // ---- commit: recompute chain at solved hp, update states ----------
        // S(v) = tanh(c*v)/c is formed as v*tanhXdX(c*v): algebraically the
        // same in both tanhXdX regions (|u| > 3: v/|u| = sign(v)/c, the ±1
        // clamp), and the ratio IS next sample's secant pivot, so the two
        // /m.c divides and both pivot recomputes disappear (§3.3).
        float uh = m.c * hp;
        float rh = tanhXdX(uh);
        float Sh = hp * rh;
        float bp = sBP + g * Sh;
        float ub = m.c * bp;
        float rb = tanhXdX(ub);
        float Sb = bp * rb;
        float lp = sLP + g * Sb;
        float yd = h1.beta0 * bp + z1;
        float dArg = std::clamp(yd * kInvVD, -30.f, 30.f);
        float dio = kKD * sinhFast(dArg);
        float vg = (hin + (yd + dio) + m.kR2 * lp + kC2 * Sb + hp)
                   * m.invNInv;

        sBP = railClamp(2.f * bp - sBP, m.vHi, m.vLo);
        sLP = railClamp(2.f * lp - sLP, m.vHi, m.vLo);
        z1  = h1.beta1 * bp - h1.alpha1 * yd;
        hpPrev = hp; bpPrev = bp; ydPrev = yd; vgPrev = vg;
        th = rh; tb = rb; diodePrev = dio;

        rawLp = lp; rawBp = bp; rawHp = hp;
        return { dcLp.process(lp) * m.makeup,
                 dcBp.process(bp) * m.makeup,
                 dcHp.process(hp) * m.makeup };
    }

private:
    // Diode pair: Rf=R3=27k, Is=2.52 nA, eta=1.752, VT=25.852 mV.
    static constexpr float kKD = 27e3f * 2.f * 2.52e-9f;   // R3*2*Is = 1.3608e-4
    static constexpr float kVD = 1.752f * 0.025852f;       // eta*VT   = 0.045293
    static constexpr float kInvVD = 1.f / kVD;

    const ModeConfig* mode = &kGerman;
    float fsInt = 192000.f, hinA = 2.f * float(M_PI) * 22.f / 192000.f;
    float sBP = 0.f, sLP = 0.f, z1 = 0.f, hinState = 0.f;
    float hpPrev = 0.f, bpPrev = 0.f, ydPrev = 0.f, vgPrev = 0.f;
    // Commit-block caches reused as next sample's secant pivots (§3.1/§3.3):
    // th = tanhXdX(c*hpPrev), tb = tanhXdX(c*bpPrev), diodePrev =
    // kKD*sinh(clamp(ydPrev/kVD)). Seeded by reset(); refreshed by setMode().
    float th = 1.f, tb = 1.f, diodePrev = 0.f;
    float rawLp = 0.f, rawBp = 0.f, rawHp = 0.f;
    DcBlocker dcLp, dcBp, dcHp;
};

} // namespace wasp
