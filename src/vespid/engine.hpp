#pragma once
// engine.hpp — per-voice state for Vespid polyphony (MF-20 pattern).
#include "WaspFilter.hpp"
#include "wasp_dsp_utils.hpp"
#include "../mf20/dsp_utils.hpp"
#include <algorithm>

namespace wasp {

// One audio channel: oversampling wrapper around WaspFilter.
//
// Deviation from the original per-channel plan: decimating from the
// oversampled rate needs one independent delay line PER output signal (lp,
// bp, hp are three distinct signals once they leave the nonlinear core), so
// each "down" stage below is really three HalfbandDown instances rather than
// one. `up`/`up4a` stay singular since there is only one input signal to
// upsample.
struct Channel {
    WaspFilter filt;
    HalfbandUp   up;                                  // host -> 2x (os >= 2)
    HalfbandDown downLp, downBp, downHp;               // 2x -> host (os >= 2), or 2x -> host for the 4x cascade's outer stage
    HalfbandUp   up4a;                                 // 2x -> 4x (os == 4)
    HalfbandDown downLp4a, downBp4a, downHp4a;         // 4x -> 2x (os == 4)

    // os: 1, 2 or 4. Returns host-rate Out. g/h1/kC2 are per-host-sample
    // constants (already slewed by the caller) reused for every sub-sample.
    WaspFilter::Out process(float in, int os, float g, const H1Coeffs& h1,
                             float kC2, bool highAcc) {
        if (os <= 1)
            return filt.process(in, g, h1, kC2, highAcc);

        float buf2[2];
        up.process(in, buf2);

        if (os == 2) {
            WaspFilter::Out o0 = filt.process(buf2[0], g, h1, kC2, highAcc);
            WaspFilter::Out o1 = filt.process(buf2[1], g, h1, kC2, highAcc);
            return { downLp.process(o0.lp, o1.lp),
                     downBp.process(o0.bp, o1.bp),
                     downHp.process(o0.hp, o1.hp) };
        }

        // os == 4: cascade a second halfband stage (2x -> 4x), four filter
        // calls at 4x, then decimate back down in two stages (4x->2x->host).
        float buf4[4];
        up4a.process(buf2[0], &buf4[0]);
        up4a.process(buf2[1], &buf4[2]);

        WaspFilter::Out o[4];
        for (int i = 0; i < 4; i++)
            o[i] = filt.process(buf4[i], g, h1, kC2, highAcc);

        float midLp0 = downLp4a.process(o[0].lp, o[1].lp);
        float midLp1 = downLp4a.process(o[2].lp, o[3].lp);
        float midBp0 = downBp4a.process(o[0].bp, o[1].bp);
        float midBp1 = downBp4a.process(o[2].bp, o[3].bp);
        float midHp0 = downHp4a.process(o[0].hp, o[1].hp);
        float midHp1 = downHp4a.process(o[2].hp, o[3].hp);

        return { downLp.process(midLp0, midLp1),
                 downBp.process(midBp0, midBp1),
                 downHp.process(midHp0, midHp1) };
    }

    // Full reset: filter core state + all resampler histories.
    void reset() {
        filt.reset();
        resetResamplers();
    }

    // Reset only the oversampling resampler histories (filter core state
    // carries across an os/host-rate change, per the design doc).
    void resetResamplers() {
        up.reset();
        downLp.reset(); downBp.reset(); downHp.reset();
        up4a.reset();
        downLp4a.reset(); downBp4a.reset(); downHp4a.reset();
    }
};

struct VoiceEngine {
    Channel l, r;

    OnePoleSmoother gSlew     { 0.049f };
    OnePoleSmoother kC2Slew   { 0.f };
    OnePoleSmoother rhoSlew   { 0.f };
    OnePoleSmoother driveSlew { 1.f };
    // H1 coefficients are division-heavy (computeH1 is only called at
    // modulate rate); these three smoothers slew the *coefficients*
    // per-sample so resonance sweeps stay zipper-free without paying for
    // computeH1 on every audio sample. Seeded at the true rho=0 values for
    // the current internal rate (see reset()) so a voice reset doesn't fade
    // the resonance network in from zero — matching how gSlew seeds near its
    // real default rather than at 0.
    OnePoleSmoother beta0Slew  { 0.f };
    OnePoleSmoother beta1Slew  { 0.f };
    OnePoleSmoother alpha1Slew { 0.f };

    float gTarget = 0.049f, kC2Target = 0.f, rhoTarget = 0.f, driveTarget = 1.f;
    float beta0Target = 0.f, beta1Target = 0.f, alpha1Target = 0.f;

    // Internal (oversampled) rate the voice currently runs at; mirrors the
    // WaspFilter default so construction-time seeding is consistent.
    float fsInt = 192000.f;

    VoiceEngine() { seedH1(); }

    void setSampleRate(float fsIntNew) {
        fsInt = fsIntNew;
        l.filt.setSampleRate(fsInt);
        r.filt.setSampleRate(fsInt);
    }

    // Seed the H1 smoothers (and targets) at the exact rho=0 coefficients
    // for the current internal rate.
    void seedH1() {
        H1Coeffs h1 = computeH1(0.f, fsInt);
        beta0Slew.reset(beta0Target = h1.beta0);
        beta1Slew.reset(beta1Target = h1.beta1);
        alpha1Slew.reset(alpha1Target = h1.alpha1);
    }

    void reset() {
        l.reset();
        r.reset();
        gSlew.reset(gTarget = 0.049f);
        kC2Slew.reset(kC2Target = 0.f);
        rhoSlew.reset(rhoTarget = 0.f);
        driveSlew.reset(driveTarget = 1.f);
        seedH1();
    }

    // Reset only the resampler histories (os/host-rate change) — filter
    // core and smoother state carry through.
    void resetResamplers() {
        l.resetResamplers();
        r.resetResamplers();
    }

    // NaN/inf recovery, MF-20 style: called once per modulate block. Only
    // the offending channel is reset — filter core AND its resampler
    // histories (a NaN in the core has propagated into the FIR delay lines
    // by the time it is detected). The voice's smoothers stay: their inputs
    // are clamped params, so they are finite, and keeping them avoids a
    // spurious parameter sweep after recovery.
    void sanitize() {
        if (!l.filt.stateFinite()) l.reset();
        if (!r.filt.stateFinite()) r.reset();
    }
};

// All 16 voices live by value (no heap, no null checks, better cache
// behavior on MetaModule); setVoices() only changes activeVoices, so the
// audio thread never allocates and every voice always carries the pool's
// current sample rate.
struct EnginePool {
    VoiceEngine engines[16];
    int activeVoices = 1;

    void setVoices(int n) {
        n = std::clamp(n, 1, 16);
        // Voices (re)entering the active range start clean — a voice that
        // rang at high resonance and went inactive must not re-emit its old
        // state.
        for (int i = activeVoices; i < n; i++) engines[i].reset();
        activeVoices = n;
    }

    void setSampleRate(float fsInt) {
        for (auto& e : engines) e.setSampleRate(fsInt);
    }

    void resetAll() {
        for (auto& e : engines) e.reset();
    }
};

} // namespace wasp
