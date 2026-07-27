#pragma once
// engine.hpp — per-voice state for Vespid polyphony (MF-20 pattern).
#include "WaspFilter.hpp"
#include "wasp_dsp_utils.hpp"
#include "../mf20/dsp_utils.hpp"
#include <algorithm>

namespace wasp {

// Output-select mask for Channel::process — one bit per module output jack.
// Only masked signals pay for decimation and DC blocking (2026-07-26 CPU
// pass): a Mix-only patch runs one decimator chain instead of three.
enum : int {
    kOutLp  = 1,
    kOutBp  = 2,
    kOutHp  = 4,
    kOutMix = 8,
};

// One audio channel: oversampling wrapper around WaspFilter.
//
// Decimating from the oversampled rate needs one independent delay line PER
// output signal (lp, bp, hp — and mix, which is blended at the oversampled
// rate — are distinct signals once they leave the nonlinear core), so each
// "down" stage below is per-signal. `up`/`up4a` stay singular since there is
// only one input signal to upsample.
//
// Output makeup gain and the ~8 Hz DC blockers are LTI, so they commute with
// the (linear) decimators and run HERE at host rate — one eval per masked
// output — instead of 3 evals per oversampled subsample inside the core.
struct Channel {
    WaspFilter filt;
    HalfbandUp   up;                                   // host -> 2x (os >= 2)
    HalfbandDown downLp, downBp, downHp, downMix;      // 2x -> host (os >= 2)
    // Inner 4x stages use the short 15-tap pair (see kHalfbandTapsInner).
    HalfbandUpInner   up4a;                            // 2x -> 4x (os == 4)
    HalfbandDownInner downLp4a, downBp4a, downHp4a, downMix4a;  // 4x -> 2x (os == 4)
    DcBlocker dcLp, dcBp, dcHp, dcMix;                 // host rate
    int activeMask = 0;   // chains whose histories are currently live

    struct Out { float lp, bp, hp, mix; };

    // os: 1, 2 or 4. Returns host-rate, makeup'd + DC-blocked outputs for
    // every signal selected in `mask` (unselected fields read 0). g/h1/kC2
    // are per-host-sample constants (already slewed by the caller) reused
    // for every sub-sample. `m` is the Mix blend (0 = LP, 1 = HP), constant
    // across the sub-samples of one host sample; `makeup` is the mode's
    // output gain.
    Out process(float in, int os, float g, const H1Coeffs& h1, float kC2,
                int mask, float m, float makeup) {
        // A signal whose jack was just connected starts with a clean chain:
        // its decimator/DC histories stopped updating while unpatched, so
        // stale samples must not leak out (the ~24-host-sample FIR warmup is
        // a 0.5 ms fade-in, inaudible on a patching action).
        if (int fresh = mask & ~activeMask) {
            if (fresh & kOutLp)  { downLp.reset();  downLp4a.reset();  dcLp.reset();  }
            if (fresh & kOutBp)  { downBp.reset();  downBp4a.reset();  dcBp.reset();  }
            if (fresh & kOutHp)  { downHp.reset();  downHp4a.reset();  dcHp.reset();  }
            if (fresh & kOutMix) { downMix.reset(); downMix4a.reset(); dcMix.reset(); }
        }
        activeMask = mask;

        if (os <= 1) {
            WaspFilter::Out o = filt.process(in, g, h1, kC2);
            return finish(o.lp, o.bp, o.hp, (1.f - m) * o.lp + m * o.hp,
                          mask, makeup);
        }

        float buf2[2];
        up.process(in, buf2);

        if (os == 2) {
            WaspFilter::Out o0 = filt.process(buf2[0], g, h1, kC2);
            WaspFilter::Out o1 = filt.process(buf2[1], g, h1, kC2);
            float lp = 0.f, bp = 0.f, hp = 0.f, mix = 0.f;
            if (mask & kOutLp)  lp  = downLp.process(o0.lp, o1.lp);
            if (mask & kOutBp)  bp  = downBp.process(o0.bp, o1.bp);
            if (mask & kOutHp)  hp  = downHp.process(o0.hp, o1.hp);
            if (mask & kOutMix) mix = downMix.process(
                (1.f - m) * o0.lp + m * o0.hp, (1.f - m) * o1.lp + m * o1.hp);
            return finish(lp, bp, hp, mix, mask, makeup);
        }

        // os == 4: cascade a second halfband stage (2x -> 4x), four filter
        // calls at 4x, then decimate back down in two stages (4x->2x->host).
        float buf4[4];
        up4a.process(buf2[0], &buf4[0]);
        up4a.process(buf2[1], &buf4[2]);

        WaspFilter::Out o[4];
        for (int i = 0; i < 4; i++)
            o[i] = filt.process(buf4[i], g, h1, kC2);

        float lp = 0.f, bp = 0.f, hp = 0.f, mix = 0.f;
        if (mask & kOutLp)
            lp = downLp.process(downLp4a.process(o[0].lp, o[1].lp),
                                downLp4a.process(o[2].lp, o[3].lp));
        if (mask & kOutBp)
            bp = downBp.process(downBp4a.process(o[0].bp, o[1].bp),
                                downBp4a.process(o[2].bp, o[3].bp));
        if (mask & kOutHp)
            hp = downHp.process(downHp4a.process(o[0].hp, o[1].hp),
                                downHp4a.process(o[2].hp, o[3].hp));
        if (mask & kOutMix) {
            float b0 = (1.f - m) * o[0].lp + m * o[0].hp;
            float b1 = (1.f - m) * o[1].lp + m * o[1].hp;
            float b2 = (1.f - m) * o[2].lp + m * o[2].hp;
            float b3 = (1.f - m) * o[3].lp + m * o[3].hp;
            mix = downMix.process(downMix4a.process(b0, b1),
                                  downMix4a.process(b2, b3));
        }
        return finish(lp, bp, hp, mix, mask, makeup);
    }

    // Host-rate DC blockers run at the module's sample rate, independent of
    // the oversampling factor.
    void setHostSampleRate(float fsHost) {
        dcLp.setSampleRate(fsHost);
        dcBp.setSampleRate(fsHost);
        dcHp.setSampleRate(fsHost);
        dcMix.setSampleRate(fsHost);
    }

    // Full reset: filter core state + all resampler/output histories.
    void reset() {
        filt.reset();
        resetResamplers();
        dcLp.reset(); dcBp.reset(); dcHp.reset(); dcMix.reset();
    }

    // Reset only the oversampling resampler histories (filter core state
    // carries across an os/host-rate change, per the design doc).
    void resetResamplers() {
        up.reset();
        downLp.reset(); downBp.reset(); downHp.reset(); downMix.reset();
        up4a.reset();
        downLp4a.reset(); downBp4a.reset(); downHp4a.reset(); downMix4a.reset();
    }

private:
    Out finish(float lp, float bp, float hp, float mix, int mask, float makeup) {
        Out out{0.f, 0.f, 0.f, 0.f};
        if (mask & kOutLp)  out.lp  = dcLp.process(lp)   * makeup;
        if (mask & kOutBp)  out.bp  = dcBp.process(bp)   * makeup;
        if (mask & kOutHp)  out.hp  = dcHp.process(hp)   * makeup;
        if (mask & kOutMix) out.mix = dcMix.process(mix) * makeup;
        return out;
    }
};

struct VoiceEngine {
    Channel l, r;

    OnePoleSmoother gSlew     { 0.049f };
    OnePoleSmoother kC2Slew   { 0.f };
    OnePoleSmoother rhoSlew   { 0.f };
    // Drive at rest in the module's default mode (British) is 0.5x: the 2x
    // knob-floor pre-gain times British mode's 0.25 hardware level staging
    // (kBritish.inGain — see Vespid.cpp modulate()). Seed the smoother there
    // so a fresh voice doesn't sweep in; a voice reset while in German mode
    // just re-slews to 2x over ~5 ms.
    OnePoleSmoother driveSlew { 0.5f };
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

    float gTarget = 0.049f, kC2Target = 0.f, rhoTarget = 0.f, driveTarget = 0.5f;
    float beta0Target = 0.f, beta1Target = 0.f, alpha1Target = 0.f;

    // Internal (oversampled) rate the voice currently runs at; mirrors the
    // WaspFilter default so construction-time seeding is consistent.
    float fsInt = 192000.f;

    VoiceEngine() { seedH1(); }

    void setSampleRates(float fsHost, float fsIntNew) {
        fsInt = fsIntNew;
        l.filt.setSampleRate(fsInt);
        r.filt.setSampleRate(fsInt);
        l.setHostSampleRate(fsHost);
        r.setHostSampleRate(fsHost);
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
        driveSlew.reset(driveTarget = 0.5f);
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

    void setSampleRates(float fsHost, float fsInt) {
        for (auto& e : engines) e.setSampleRates(fsHost, fsInt);
    }

    void resetAll() {
        for (auto& e : engines) e.reset();
    }
};

} // namespace wasp
