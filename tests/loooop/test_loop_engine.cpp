#include "../../src/loooop/dsp/LoopEngine.hpp"
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

static int g_failures = 0;
static void check(bool cond, const char* name) {
    if (!cond) { std::printf("FAIL: %s\n", name); ++g_failures; }
    else       { std::printf("ok:   %s\n", name); }
}
static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

static void soloHead0(LoopEngine& e) {
    for (int h = 1; h < LoopEngine::NUM_HEADS; ++h) e.setLevel(h, 0.f);
}

static void test_record_play_1x() {
    LoopEngine e;
    e.reset(10.f, 100.f);          // maxSamples = 1000
    soloHead0(e);
    const float in[] = {1.f, 2.f, 3.f, 4.f};
    e.toggleRecord();              // start recording (overwrite)
    for (float x : in) e.process(x);
    e.toggleRecord();             // stop -> loop length frozen at 4
    check(e.loopLength() == 4, "record_play_1x: loop length == 4");
    check(!e.isRecording(),     "record_play_1x: not recording after stop");
    // default head: speed 1, size 1, centre 0.5, level 1 -> plays 1,2,3,4,1,...
    check(near(e.process(0.f), 1.f), "record_play_1x: out[0]==1");
    check(near(e.process(0.f), 2.f), "record_play_1x: out[1]==2");
    check(near(e.process(0.f), 3.f), "record_play_1x: out[2]==3");
    check(near(e.process(0.f), 4.f), "record_play_1x: out[3]==4");
    check(near(e.process(0.f), 1.f), "record_play_1x: out[4]==1 (wrapped)");
}

static void record_ramp(LoopEngine& e, int n) {
    e.reset(10.f, 100.f);
    soloHead0(e);
    e.toggleRecord();
    for (int i = 0; i < n; ++i) e.process(static_cast<float>(i + 1)); // 1,2,3,4
    e.toggleRecord();
}

static void test_half_speed() {
    LoopEngine e; record_ramp(e, 4);
    e.setSpeed(0, 0.5f);
    // pos 0 ->0.5 ->1.0 ->1.5 : reads 1, 1.5, 2, 2.5
    check(near(e.process(0.f), 1.0f), "half_speed: out[0]==1.0");
    // Catmull-Rom: taps (buf[3],buf[0],buf[1],buf[2]) = (4,1,2,3), t=0.5 -> 1.25
    check(near(e.process(0.f), 1.25f), "half_speed: out[1]==1.25 (cubic, seam tap wraps)");
    check(near(e.process(0.f), 2.0f), "half_speed: out[2]==2.0");
    check(near(e.process(0.f), 2.5f), "half_speed: out[3]==2.5");
}

static void test_double_speed() {
    LoopEngine e; record_ramp(e, 4);
    e.setSpeed(0, 2.0f);
    // pos 0 ->2 ->(4 wraps to 0) ->2 : reads 1, 3, 1, 3
    check(near(e.process(0.f), 1.f), "double_speed: out[0]==1");
    check(near(e.process(0.f), 3.f), "double_speed: out[1]==3");
    check(near(e.process(0.f), 1.f), "double_speed: out[2]==1 (wrapped)");
    check(near(e.process(0.f), 3.f), "double_speed: out[3]==3");
}

static void test_reverse() {
    LoopEngine e; record_ramp(e, 4);
    e.setSpeed(0, -1.0f);
    // pos 0 -> (-1 wraps to 3) -> 2 -> 1 : reads 1, 4, 3, 2, 1
    check(near(e.process(0.f), 1.f), "reverse: out[0]==1");
    check(near(e.process(0.f), 4.f), "reverse: out[1]==4");
    check(near(e.process(0.f), 3.f), "reverse: out[2]==3");
    check(near(e.process(0.f), 2.f), "reverse: out[3]==2");
    check(near(e.process(0.f), 1.f), "reverse: out[4]==1");
}

static void test_subloop_window() {
    LoopEngine e;
    e.reset(10.f, 100.f);
    soloHead0(e);
    e.toggleRecord();
    for (int i = 0; i < 8; ++i) e.process(static_cast<float>(i + 1)); // 1..8
    e.toggleRecord();                          // loopLen == 8
    // size 0.25 -> winLen 2 ; centre 0.3125 -> centre sample 2.5 -> winStart 1.5
    // interpolation over [1.5, 3.5): the head starts snapped to winStart 1.5
    e.setSize(0, 0.25f);
    e.setPosition(0, 0.3125f);
    // Cubic: k=-1 tap at 0 wraps to 2 (window period 2), taps (3,2,3,4) -> 2.375
    check(near(e.process(0.f), 2.375f), "subloop: out[0]==2.375 (window start, cubic)");
    // advance by 1 -> pos 2.5 -> taps (4,3,4,3) -> 3.5 (unchanged from linear)
    check(near(e.process(0.f), 3.5f), "subloop: out[1]==3.5");
    // advance -> pos 3.5 >= winEnd 3.5 -> wraps to 1.5 -> 2.375 again
    check(near(e.process(0.f), 2.375f), "subloop: out[2]==2.375 (wrapped in window)");
}

// When the read position's next tap (i1) spills past a fractional window
// start on wrap, the wrap target must interpolate at the fractional winStart
// (lerp between floor(winStart) and floor(winStart)+1) rather than truncating
// it to floor(winStart). Crossfade is off so the seam-crossfade blend (which
// previews via readRaw, unaffected by this bug) doesn't mask the result.
static void test_fractional_winstart_wrap_target() {
    LoopEngine e;
    e.reset(10.f, 100.f);
    soloHead0(e);
    e.toggleRecord();
    for (int i = 0; i < 10; ++i) e.process(static_cast<float>(i + 1)); // buf = 1..10
    e.toggleRecord();                          // loopLen == 10
    e.setCrossfade(false);
    // size 0.55 -> winLen 5.5 ; centre 0.5 -> centre sample 5.0 -> winStart 2.25
    // (fractional) ; winEnd 7.75.
    e.setSize(0, 0.55f);
    e.setPosition(0, 0.5f);
    // pos snaps to winStart=2.25 on the first read, then advances by 1 each
    // sample: 2.25, 3.25, 4.25, 5.25, 6.25, 7.25 (last read before the wrap
    // at 7.75 — i1=8 spills past winEnd here).
    float out = 0.f;
    for (int i = 0; i < 6; ++i) out = e.process(0.f);
    // Cubic taps (7, 8, readRaw(2.5)=3.5, readRaw(3.5)=4.5), t=0.25 -> 7.1328125.
    // The wrapped taps read at winStart + (tap − winEnd) — position-preserving —
    // so a truncated (floor'd) winStart would give readRaw(2.0)=3.0 / readRaw(3.0)=4.0
    // and a different value; the fractional-winStart property this test guards survives.
    check(near(out, 7.1328125f), "fractional winStart: wrap taps interpolate at frac positions");
}

static void test_cubic_exact_on_interior_ramp() {
    // Catmull-Rom reproduces linear ramps exactly when all 4 taps are interior.
    LoopEngine e; e.reset(10.f, 100.f); soloHead0(e);
    e.toggleRecord();
    for (int i = 0; i < 8; ++i) e.process(static_cast<float>(i + 1));  // 1..8
    e.toggleRecord();
    e.setCrossfade(false);
    e.setSpeed(0, 0.5f);
    e.jumpHead(0, 0.f);
    e.process(0.f);                       // pos 0 (seam-adjacent; skip)
    float a = e.process(0.f);             // pos 0.5 (k=-1 wraps; skip exactness)
    (void)a;
    check(near(e.process(0.f), 2.0f), "cubic ramp: pos 1.0 exact");
    check(near(e.process(0.f), 2.5f), "cubic ramp: pos 1.5 exact (interior taps)");
    check(near(e.process(0.f), 3.0f), "cubic ramp: pos 2.0 exact");
    check(near(e.process(0.f), 3.5f), "cubic ramp: pos 2.5 exact (interior taps)");
}

static void test_cubic_beats_linear_on_sine() {
    // At speed 0.5 a sampled sine reconstructed with Catmull-Rom must be
    // closer to the analytic sine than the linear baseline (computed here
    // from the same buffer).
    const int N = 512;                    // 8 whole cycles -> continuous seam
    const double w = 2.0 * M_PI * 8.0 / N;
    LoopEngine e(1); e.reset(48000.f, 1.f); e.setCrossfade(false);
    e.toggleRecord();
    static float buf[N];
    for (int i = 0; i < N; ++i) {
        buf[i] = (float)std::sin(w * i);
        e.process(buf[i]);
    }
    e.toggleRecord();
    e.setSpeed(0, 0.5f);
    e.jumpHead(0, 0.f);
    double errCubic = 0.0, errLinear = 0.0;
    for (int k = 0; k < 2 * N; ++k) {
        const double pos = 0.5 * k - std::floor(0.5 * k / N) * N;   // pos of THIS read
        float out = e.process(0.f);
        double ideal = std::sin(w * pos);
        errCubic += (out - ideal) * (out - ideal);
        int i0 = (int)pos; int i1 = (i0 + 1) % N; double fr = pos - i0;
        double lin = (1.0 - fr) * buf[i0] + fr * buf[i1];
        errLinear += (lin - ideal) * (lin - ideal);
    }
    char msg[96];
    std::snprintf(msg, sizeof(msg),
        "cubic sine: rmsErr %.3g < 0.5 * linear %.3g", std::sqrt(errCubic), std::sqrt(errLinear));
    check(errCubic < 0.25 * errLinear, msg);   // RMS at least 2x better
}

static void test_cubic_tiny_loop_taps_bounded() {
    // 1- and 2-sample loops: the k=-1/+2 taps must wrap/clamp inside the
    // window, never index outside [0, loopLen). Output stays within the
    // recorded sample range.
    for (int n : {1, 2}) {
        LoopEngine e(1); e.reset(10.f, 100.f); e.setCrossfade(false);
        e.toggleRecord();
        for (int i = 0; i < n; ++i) e.process(i ? -1.f : 1.f);
        e.toggleRecord();
        e.setSpeed(0, 0.7f);              // fractional positions
        bool ok = true;
        for (int i = 0; i < 64; ++i) {
            float v = e.process(0.f);
            if (!std::isfinite(v) || v < -2.5f || v > 2.5f) ok = false;
        }
        check(ok, n == 1 ? "cubic tiny loop: 1-sample loop bounded"
                         : "cubic tiny loop: 2-sample loop bounded");
    }
}

static void test_cubic_reverse_matches_forward_at_position() {
    // Tap selection depends only on position, not direction: reading the same
    // fractional position forward and reverse gives the same value.
    LoopEngine f(1), r(1);
    for (LoopEngine* e : {&f, &r}) {
        e->reset(10.f, 100.f);
        e->setCrossfade(false);
        e->toggleRecord();
        for (int i = 0; i < 8; ++i) e->process((float)((i * 37) % 11));  // non-ramp content
        e->toggleRecord();
    }
    f.setSpeed(0, 0.5f);  f.jumpHead(0, 2.f / 7.f);    // pos 2.0, then 2.5, 3.0...
    r.setSpeed(0, -0.5f); r.jumpHead(0, 3.f / 7.f);    // pos 3.0, then 2.5, 2.0...
    f.process(0.f); f.process(0.f);                    // consume pos 2.0 -> next read 2.5...
    float fwd = f.process(0.f);                        // reads pos 3.0
    r.process(0.f);                                    // reads pos 3.0
    float rev  = r.process(0.f);                       // reads pos 2.5
    float fwd25 = 0.f;
    { LoopEngine g(1); g.reset(10.f, 100.f); g.setCrossfade(false);
      g.toggleRecord();
      for (int i = 0; i < 8; ++i) g.process((float)((i * 37) % 11));
      g.toggleRecord();
      g.setSpeed(0, 0.5f); g.jumpHead(0, 2.f / 7.f);
      g.process(0.f);
      fwd25 = g.process(0.f); }                        // reads pos 2.5
    check(near(rev, fwd25), "cubic reverse: same position -> same value as forward");
    (void)fwd;
}

static void test_overdub_sums() {
    LoopEngine e;
    e.reset(10.f, 100.f);
    soloHead0(e);
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(1.f);   // buffer = 1,1,1,1
    e.toggleRecord();                              // loopLen 4
    e.setLevel(0, 0.f);                            // mute playback so overdub input is isolated
    e.toggleRecord();                              // start overdub
    for (int i = 0; i < 4; ++i) e.process(1.f);    // buffer -> 2,2,2,2
    e.toggleRecord();                              // stop overdub
    e.setLevel(0, 1.f);                            // unmute
    check(near(e.process(0.f), 2.f), "overdub: out[0]==2");
    check(near(e.process(0.f), 2.f), "overdub: out[1]==2");
}

static void test_clear() {
    LoopEngine e;
    e.reset(10.f, 100.f);
    soloHead0(e);
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(5.f);
    e.toggleRecord();
    e.clear();
    check(!e.hasLoop(), "clear: no loop after clear");
    check(near(e.process(0.f), 0.f), "clear: silence after clear");
}

static void test_buffer_ceiling_autoend() {
    LoopEngine e;
    e.reset(10.f, 1.f);                 // maxSamples == 10
    e.toggleRecord();
    for (int i = 0; i < 15; ++i) e.process(1.f);   // exceed the buffer
    check(e.loopLength() == 10, "ceiling: loop length == maxSamples");
    check(!e.isRecording(),     "ceiling: recording auto-ended at ceiling");
}

static void test_sample_data() {
    LoopEngine e; e.reset(10.f, 100.f);
    e.toggleRecord();
    e.process(0.5f); e.process(-0.25f); e.process(1.f); e.process(0.f);
    e.toggleRecord();                     // loop = {0.5, -0.25, 1, 0}, mono -> mirrored
    check(e.loopLength() == 4,            "sampleData: loop length == 4");
    check(near(e.sampleData(0)[0], 0.5f), "sampleData: L[0] == 0.5");
    check(near(e.sampleData(0)[1], -0.25f), "sampleData: L[1] == -0.25");
    check(near(e.sampleData(0)[2], 1.f),  "sampleData: L[2] == 1");
    check(near(e.sampleData(1)[1], -0.25f), "sampleData: mono mirrors into R");
    // Overdub sums into the existing buffer at loop start.
    e.toggleRecord();                     // start overdub
    e.process(-2.f);                      // buf[0] = 0.5 + (-2) = -1.5
    e.toggleRecord();
    check(near(e.sampleData(0)[0], -1.5f), "sampleData: overdub sums into buffer");
}

static void test_display_snapshot() {
    LoopEngine e; e.reset(10.f, 100.f);
    auto s0 = e.displaySnapshot();
    check(s0.loopLen == 0 && !s0.recording,  "snap: blank initially");

    e.toggleRecord();
    e.process(0.1f); e.process(0.2f); e.process(0.3f);
    auto s1 = e.displaySnapshot();
    check(s1.recording && s1.loopLen == 0,   "snap: recording state visible");
    check(s1.recordedLen == 3,               "snap: recordedLen grows during record");

    e.process(0.4f);
    e.toggleRecord();                        // loop length frozen at 4
    auto s2 = e.displaySnapshot();
    check(s2.loopLen == 4 && !s2.recording,  "snap: loop frozen at 4");
    check(near(s2.headPos01[0], 0.f),        "snap: head starts at 0");

    // Display mirrors are throttled to every 64th sample; tick past
    // the throttle boundary before reading a snapshot instead of reading
    // right after a couple of process() calls. 61 more samples lands on
    // the 65th process() call since reset() (call #1 landed on a tick too,
    // but during the still-unfrozen record pass, so it had no effect) --
    // the head has advanced 61 samples by then, 61 mod 4 == 1.
    for (int i = 0; i < 61; ++i) e.process(0.f);
    auto s3 = e.displaySnapshot();
    check(near(s3.headPos01[0], 0.25f),      "snap: head pos 0.25 after settling past the display tick");
    check(near(s3.winStart01[0], 0.f),       "snap: full window start == 0");
    check(near(s3.winEnd01[0], 1.f),         "snap: full window end == 1");

    e.setSize(0, 0.5f); e.setPosition(0, 0.5f);
    for (int i = 0; i < 64; ++i) e.process(0.f);   // settle past the next display-mirror tick
    auto s4 = e.displaySnapshot();
    check(near(s4.winStart01[0], 0.25f),     "snap: half window start == 0.25");
    check(near(s4.winEnd01[0], 0.75f),       "snap: half window end == 0.75");

    e.clear();
    auto s5 = e.displaySnapshot();
    check(s5.loopLen == 0 && !s5.recording,  "snap: clear resets snapshot");
}

static void test_four_heads_mix() {
    LoopEngine e; record_ramp(e, 4);        // loop 1,2,3,4; record_ramp solos head 0
    for (int h = 1; h < LoopEngine::NUM_HEADS; ++h) e.setLevel(h, 1.f);  // un-solo
    // all four heads at defaults are phase-locked: out = 4 * sample
    check(near(e.process(0.f), 4.f),  "four_heads: defaults sum to 4x sample");
    check(near(e.process(0.f), 8.f),  "four_heads: still locked on sample 2");
    e.setLevel(1, 0.f); e.setLevel(2, 0.f); e.setLevel(3, 0.f);
    check(near(e.process(0.f), 3.f),  "four_heads: heads 1-3 muted -> single head");
}

static void test_per_head_params_isolated() {
    LoopEngine e; record_ramp(e, 4);        // loop 1,2,3,4; head 0 soloed
    e.setLevel(0, 0.f); e.setLevel(1, 1.f); // switch solo to head 1
    e.setSpeed(1, -1.f);                    // reverse head 1 only
    // same expectations as test_reverse, but via head 1
    check(near(e.process(0.f), 1.f), "isolation: head1 out[0]==1");
    check(near(e.process(0.f), 4.f), "isolation: head1 reverse out[1]==4");
    check(near(e.process(0.f), 3.f), "isolation: head1 reverse out[2]==3");
}

static void test_display_snapshot_four_heads() {
    LoopEngine e; e.reset(10.f, 100.f);
    e.toggleRecord();
    for (int i = 0; i < 8; ++i) e.process(0.1f);
    e.toggleRecord();                       // loop of 8, all heads at pos 0
    e.setSpeed(1, 2.f);                     // head 1 double speed
    e.setSize(2, 0.5f); e.setPosition(2, 0.5f);   // head 2 half window [2,6)
    // Display mirrors are throttled to every 64th sample; settle
    // past the throttle boundary (57 more samples reaches the 65th
    // process() call since reset(), the next tick after the record pass'
    // own tick at call #1, which had no effect while the loop was still
    // unfrozen) instead of reading right after a single process() call.
    // 64 is a multiple of the 8-sample loop length (and head 1's 4-sample
    // period, and head 2's 4-sample period once parked in its window), so
    // every head is back at the exact phase a single-step read would have
    // shown.
    for (int i = 0; i < 57; ++i) e.process(0.f);
    auto s = e.displaySnapshot();
    check(near(s.headPos01[0], 1.f/8.f),  "snap4: head0 pos 1/8");
    check(near(s.headPos01[1], 2.f/8.f),  "snap4: head1 pos 2/8 (double speed)");
    check(near(s.headPos01[3], 1.f/8.f),  "snap4: head3 pos 1/8");
    check(near(s.winStart01[0], 0.f) && near(s.winEnd01[0], 1.f),
                                          "snap4: head0 full window");
    check(near(s.winStart01[2], 0.25f),   "snap4: head2 window start 0.25");
    check(near(s.winEnd01[2], 0.75f),     "snap4: head2 window end 0.75");
}

// waveformRevision() must change on any recorded-buffer mutation (initial
// write, overdub write, clear, reset) and stay stable across playback-only
// ticks — display hosts key their waveform cache on this counter, so a
// false-negative bump would show stale audio and a false-positive would
// defeat the cache.
static void test_waveform_revision_tracks_write_changes_only() {
    LoopEngine e(1);
    e.reset(48000.f, 1.f);
    const auto afterReset = e.waveformRevision();
    e.toggleRecord();
    e.process(0.25f);
    const auto afterWrite = e.waveformRevision();
    check(afterWrite != afterReset, "wave revision: recording write invalidates waveform");
    e.toggleRecord();
    // The freeze itself must invalidate: grid bars (drawn only once a frozen
    // loop exists) have to appear the moment recording stops, not on the
    // next write.
    const auto afterFreeze = e.waveformRevision();
    check(afterFreeze != afterWrite, "wave revision: loop freeze invalidates waveform");
    for (int i = 0; i < 100; ++i) e.process(0.f);
    check(e.waveformRevision() == afterFreeze, "wave revision: playback does not invalidate waveform");
    e.toggleRecord();
    e.process(0.25f);
    check(e.waveformRevision() != afterFreeze, "wave revision: overdub invalidates waveform");
    const auto beforeClear = e.waveformRevision();
    e.clear();
    check(e.waveformRevision() != beforeClear, "wave revision: clear invalidates waveform");
    const auto beforeSecondReset = e.waveformRevision();
    e.reset(48000.f, 1.f);
    check(e.waveformRevision() != beforeSecondReset, "wave revision: repeated reset invalidates waveform");
}

static void test_overdub_gate() {
    LoopEngine e; record_ramp(e, 4);      // loop exists, not recording, head 0 soloed
    e.setOverdub(false);
    e.toggleRecord();                     // must be ignored: no overdub started
    check(!e.isRecording(),          "odgate: toggle ignored when overdub off");
    check(near(e.process(0.f), 1.f), "odgate: playback unaffected");
    e.setOverdub(true);
    e.toggleRecord();
    check(e.isRecording(),           "odgate: overdub allowed again when on");
    e.toggleRecord();                     // stop

    LoopEngine e2; e2.reset(10.f, 100.f);
    e2.setOverdub(false);
    e2.toggleRecord();                    // initial record must not be gated
    check(e2.isRecording(),          "odgate: initial record unaffected");
    e2.process(1.f);
    e2.toggleRecord();                    // stopping must always work
    check(!e2.isRecording() && e2.loopLength() == 1, "odgate: stop always works");
}

static void test_stereo_record_play() {
    LoopEngine e; e.reset(10.f, 100.f); soloHead0(e);
    std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
    e.toggleRecord();
    e.process(1.f, 5.f, hs);
    e.process(2.f, 6.f, hs);
    e.toggleRecord();
    e.process(0.f, 0.f, hs);
    check(near(hs[0].l, 1.f) && near(hs[0].r, 5.f), "stereo: distinct L/R sample 0");
    e.process(0.f, 0.f, hs);
    check(near(hs[0].l, 2.f) && near(hs[0].r, 6.f), "stereo: distinct L/R sample 1");
}

static void test_mono_convenience_matches_stereo() {
    LoopEngine e; record_ramp(e, 4);      // mono overload records both channels
    std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
    e.process(0.f, 0.f, hs);
    check(near(hs[0].l, 1.f) && near(hs[0].r, 1.f), "mono: both channels identical");
}

static void test_per_head_outs() {
    // Level is mix-only: head outs carry the full-level signal regardless of
    // Level, and the smoothed mix gain is exposed for the hosts.
    LoopEngine e; record_ramp(e, 4);      // soloHead0: heads 1-3 at level 0
    std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
    e.setLevel(1, 0.5f);
    e.process(0.f, 0.f, hs);
    check(near(hs[0].l, 1.f), "headouts: head0 out full-level");
    check(near(hs[1].l, 1.f), "headouts: Level does not scale the head out");
    check(near(hs[2].l, 1.f), "headouts: Level-0 head still on its own out");
    check(near(e.smoothedLevel(0), 1.f),  "headouts: head0 mix gain 1");
    check(near(e.smoothedLevel(1), 0.5f), "headouts: mix gain follows Level");
    check(near(e.smoothedLevel(2), 0.f),  "headouts: Level-0 head muted in mix");
}

static void test_restart_head() {
    LoopEngine e; record_ramp(e, 4);
    e.process(0.f); e.process(0.f);        // head advanced to pos 2
    e.restartHead(0);
    check(near(e.process(0.f), 1.f), "restart: snaps to window start");
    LoopEngine r; record_ramp(r, 4);
    r.setSpeed(0, -1.f);
    r.restartHead(0);
    check(near(r.process(0.f), 4.f), "restart: reverse snaps to window end");
}

static void test_one_shot() {
    LoopEngine e; record_ramp(e, 4);
    e.setOneShot(0, true);
    check(near(e.process(0.f), 0.f), "oneshot: armed head is silent");
    e.triggerOneShot(0);
    check(near(e.process(0.f), 1.f), "oneshot: plays from window start");
    check(near(e.process(0.f), 2.f), "oneshot: out[1]==2");
    check(near(e.process(0.f), 3.f), "oneshot: out[2]==3");
    check(near(e.process(0.f), 4.f), "oneshot: out[3]==4");
    check(near(e.process(0.f), 0.f), "oneshot: stops after one pass");
    check(near(e.process(0.f), 0.f), "oneshot: stays stopped");
    e.triggerOneShot(0);
    check(near(e.process(0.f), 1.f), "oneshot: retrigger plays again");
    e.triggerOneShot(0);
    check(near(e.process(0.f), 1.f), "oneshot: mid-flight retrigger restarts");
    e.setOneShot(0, false);
    check(near(e.process(0.f), 2.f), "oneshot: disable resumes looping");
}

static void test_one_shot_reverse() {
    LoopEngine e; record_ramp(e, 4);
    e.setSpeed(0, -1.f);
    e.setOneShot(0, true);
    e.triggerOneShot(0);
    check(near(e.process(0.f), 4.f), "oneshot_rev: starts at window end");
    check(near(e.process(0.f), 3.f), "oneshot_rev: out[1]==3");
    check(near(e.process(0.f), 2.f), "oneshot_rev: out[2]==2");
    check(near(e.process(0.f), 1.f), "oneshot_rev: out[3]==1");
    check(near(e.process(0.f), 0.f), "oneshot_rev: stops after one pass");
}

static void test_one_shot_fade_out() {
    LoopEngine e(1); e.reset(48000.f, 1.f);            // crossfade on (default)
    e.toggleRecord();
    for (int i = 0; i < 2000; ++i) e.process(1.f);      // constant loop
    e.toggleRecord();
    e.setOneShot(0, true);
    e.triggerOneShot(0);
    static float out[2000];
    for (int i = 0; i < 2000; ++i) out[i] = e.process(0.f);
    check(near(out[1000], 1.f, 0.01f), "osfade: full level mid-pass");
    check(std::fabs(out[1999]) < 0.05f, "osfade: last sample faded to ~0");
    bool mono = true, smooth = true;
    for (int i = 1761; i < 2000; ++i) {
        if (out[i] > out[i-1] + 1e-3f) mono = false;
        if (std::fabs(out[i] - out[i-1]) > 0.05f) smooth = false;
    }
    check(mono,   "osfade: fade is monotonic");
    check(smooth, "osfade: no step at the end");
    check(near(e.process(0.f), 0.f), "osfade: silent after the pass");
}

static void test_one_shot_fade_out_reverse() {
    LoopEngine e(1); e.reset(48000.f, 1.f);
    e.toggleRecord();
    for (int i = 0; i < 2000; ++i) e.process(1.f);
    e.toggleRecord();
    e.setSpeed(0, -1.f);
    e.setOneShot(0, true);
    e.triggerOneShot(0);
    static float out[2000];
    for (int i = 0; i < 2000; ++i) out[i] = e.process(0.f);
    check(near(out[1000], 1.f, 0.01f),  "osfade_rev: full level mid-pass");
    check(std::fabs(out[1999]) < 0.05f, "osfade_rev: fades to ~0 at window start");
    bool smooth = true;
    for (int i = 1761; i < 2000; ++i)
        if (std::fabs(out[i] - out[i-1]) > 0.05f) smooth = false;
    check(smooth, "osfade_rev: no step at the end");
}

static void test_one_shot_retrigger_mid_fade() {
    LoopEngine e(1); e.reset(48000.f, 1.f);
    e.toggleRecord();
    for (int i = 0; i < 2000; ++i) e.process(1.f);
    e.toggleRecord();
    e.setOneShot(0, true);
    e.triggerOneShot(0);
    for (int i = 0; i < 1880; ++i) e.process(0.f);      // ~120 samples into the fade
    float before = e.process(0.f);
    e.triggerOneShot(0);                                 // retrigger during the fade
    float maxDelta = 0.f, prev = before;
    for (int i = 0; i < 100; ++i) {
        float v = e.process(0.f);
        maxDelta = std::max(maxDelta, std::fabs(v - prev));
        prev = v;
    }
    check(before < 0.7f,             "osretrig: was mid-fade before retrigger");
    check(maxDelta < 0.1f,           "osretrig: no gain snap (ramps back in)");
    check(near(prev, 1.f, 0.05f),    "osretrig: back to full level after ~1 ms ramp");
}

// Q: a retrigger ramp value left behind by an ended pass must not
// attenuate the next trigger (the !playing path never reset osRamp).
static void test_one_shot_stale_ramp_cleared_on_fresh_trigger() {
    auto runSequence = [](bool retriggerMidFade) {
        LoopEngine e(1);
        e.reset(48000.f, 1.f);
        e.toggleRecord();
        for (int i = 0; i < 2000; ++i) e.process(1.f);
        e.toggleRecord();
        e.setOneShot(0, true);
        e.setSpeed(0, 50.f);              // pass ends in ~40 samples, inside the ~48-sample ramp
        e.triggerOneShot(0);
        for (int i = 0; i < 36; ++i) e.process(0.f);   // ~90% through the pass, into the fade
        if (retriggerMidFade)
            e.triggerOneShot(0);          // sets osRamp = fade gain < 1
        for (int i = 0; i < 200; ++i) e.process(0.f);  // pass ends; head stops
        // Fresh trigger from the armed, non-playing state:
        e.triggerOneShot(0);
        float first = e.process(0.f);
        return first;
    };
    float withStaleRamp = runSequence(true);
    float clean         = runSequence(false);
    check(near(withStaleRamp, clean, 1e-4f),
          "stale_osramp: fresh trigger opens at the same level as a clean trigger");
}

static void test_one_shot_short_fast_window_ramp_and_fade() {
    LoopEngine e(1);
    e.reset(48000.f, 1.f);
    e.toggleRecord();
    for (int i = 0; i < 2000; ++i) e.process(1.f);
    e.toggleRecord();
    e.setOneShot(0, true);
    e.setSpeed(0, 50.f);
    bool allFinite = true;
    bool haveFirst = false;
    float maxDelta = 0.f, prev = 0.f;
    for (int pass = 0; pass < 8; ++pass) {
        e.triggerOneShot(0);
        for (int i = 0; i < 20; ++i) {     // retrigger every 20 samples, mid-pass
            float v = e.process(0.f);
            if (!std::isfinite(v)) allFinite = false;
            // Skip the very first sample: it jumps from true silence (no prior
            // audio at all) to full level, which isn't a gain snap to detect.
            if (haveFirst) maxDelta = std::max(maxDelta, std::fabs(v - prev));
            haveFirst = true;
            prev = v;
        }
    }
    check(allFinite, "os_short_window: output stays finite under rapid retrigger");
    check(maxDelta < 0.15f, "os_short_window: no hard gain snaps");
}

static void test_jump_head() {
    LoopEngine e; record_ramp(e, 4);       // winLen 4 -> pos = t * 3
    e.jumpHead(0, 2.f / 3.f);
    check(near(e.process(0.f), 3.f), "jump: t=2/3 lands on sample 2");
    e.jumpHead(0, 1.f);
    check(near(e.process(0.f), 4.f), "jump: t=1 lands on last sample");
    e.jumpHead(0, -0.5f);
    check(near(e.process(0.f), 1.f), "jump: negative t clamps to window start");
    e.setSize(0, 0.5f);                    // window [1,3): samples 2,3
    e.jumpHead(0, 0.f);
    check(near(e.process(0.f), 2.f), "jump: sub-window start");
}

static void test_one_shot_survives_clear() {
    LoopEngine e; record_ramp(e, 4);
    e.setOneShot(0, true);
    e.triggerOneShot(0);
    check(near(e.process(0.f), 1.f), "oneshot_clear: playing before clear");
    e.clear();
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(static_cast<float>(i + 1));
    e.toggleRecord();
    check(near(e.process(0.f), 0.f), "oneshot_clear: re-armed after clear");
    e.triggerOneShot(0);
    check(near(e.process(0.f), 1.f), "oneshot_clear: trigger works on new loop");
}

static void test_display_snapshot_armed() {
    LoopEngine e; record_ramp(e, 4);
    check(e.displaySnapshot().playing[0], "armsnap: looping head reports playing");
    e.setOneShot(0, true);
    check(!e.displaySnapshot().playing[0], "armsnap: armed one-shot reports not playing");
    e.triggerOneShot(0);
    check(e.displaySnapshot().playing[0], "armsnap: triggered one-shot reports playing");
    for (int i = 0; i < 5; ++i) e.process(0.f);   // pass ends
    check(!e.displaySnapshot().playing[0], "armsnap: finished pass reports not playing");
    e.setOneShot(0, false);
    check(e.displaySnapshot().playing[0], "armsnap: leaving one-shot resumes playing");
    e.setOneShot(0, true);
    e.triggerOneShot(0);
    e.clear();
    check(!e.displaySnapshot().playing[0], "armsnap: clear re-arms one-shot silent");
    check(e.displaySnapshot().playing[1], "armsnap: other heads unaffected");
}

static void test_triggers_no_loop() {
    LoopEngine e; e.reset(10.f, 100.f); soloHead0(e);
    e.restartHead(0);
    e.jumpHead(0, 0.5f);
    e.setOneShot(0, true);
    e.triggerOneShot(0);
    check(near(e.process(0.f), 0.f), "trig_noloop: all no-ops without a loop");
}

static void test_jitter_off_stable() {
    LoopEngine e; record_ramp(e, 8);
    e.setSize(0, 0.5f);                    // window can move
    // Display mirrors are throttled to every 64th sample: settle
    // past one tick here so the measurement loop below starts reading the
    // post-setSize window, not a stale pre-change value (which would read
    // as spurious "movement" the first time the throttle catches up).
    for (int i = 0; i < 64; ++i) e.process(0.f);
    bool moved = false; float first = -1.f;
    for (int i = 0; i < 64; ++i) {
        e.process(0.f);
        float ws = e.displaySnapshot().winStart01[0];
        if (first < 0.f) first = ws;
        else if (!near(ws, first)) moved = true;
    }
    check(!moved, "jitter: j=0 window never moves");
}

static void test_jitter_moves_window() {
    LoopEngine e; record_ramp(e, 8);
    e.setSize(0, 0.25f);                   // winLen 2: wraps every 2 samples
    e.setJitter(0, 1.f);
    bool moved = false, inBounds = true; float first = -1.f;
    for (int i = 0; i < 64; ++i) {
        e.process(0.f);
        auto s = e.displaySnapshot();
        float ws = s.winStart01[0];
        if (first < 0.f) first = ws;
        else if (!near(ws, first)) moved = true;
        if (ws < -1e-4f || s.winEnd01[0] > 1.0001f) inBounds = false;
    }
    check(moved,    "jitter: j=1 window moves across wraps");
    check(inBounds, "jitter: window stays inside the loop");
}

static void test_single_head_engine() {
    LoopEngine e(1);
    check(e.numHeads() == 1, "single_head: numHeads()==1");
    e.reset(10.f, 100.f);
    check(e.numHeads() == 1, "single_head: reset preserves head count");
    e.toggleRecord();
    const float in[] = {1.f, 2.f, 3.f, 4.f};
    for (float x : in) e.process(x);
    e.toggleRecord();
    // one head at defaults (speed 1, size 1, level 1) plays 1,2,3,4,...
    check(near(e.process(0.f), 1.f), "single_head: out[0]==1");
    check(near(e.process(0.f), 2.f), "single_head: out[1]==2");
    // inactive head slots stay silent and their setters are ignored
    e.setLevel(1, 1.f);
    e.setSpeed(2, 2.f);
    std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
    e.process(0.f, 0.f, hs);
    check(hs[1].l == 0.f && hs[2].l == 0.f && hs[3].l == 0.f,
          "single_head: inactive heads silent");
    LoopEngine d;
    check(d.numHeads() == LoopEngine::NUM_HEADS, "default engine still 4 heads");
}

static float maxSeamDelta(LoopEngine& e, int playSamples) {
    float prev = e.process(0.f), mx = 0.f;
    for (int i = 1; i < playSamples; ++i) {
        float v = e.process(0.f);
        float d = std::fabs(v - prev);
        if (d > mx) mx = d;
        prev = v;
    }
    return mx;
}

// A full-loop ramp jumps ~1.0 at the seam (buf[N-1]~1 -> buf[0]=0). The
// crossfade must spread that step over its fade window. Runs at 48 kHz so the
// ~5 ms fade is a real 240 samples (it rounds to 0 at the 10 Hz test rate used
// elsewhere, which is why those seam-exact tests are unaffected).
static void test_crossfade_declicks_seam() {
    const int N = 2000;
    LoopEngine on; on.reset(48000.f, 1.f); soloHead0(on);
    on.toggleRecord();
    for (int i = 0; i < N; ++i) on.process((float)i / (float)(N - 1));
    on.toggleRecord();
    float onDelta = maxSeamDelta(on, N * 3);          // default: crossfade on
    check(onDelta < 0.1f, "crossfade: seam delta small when on (default)");

    LoopEngine off; off.reset(48000.f, 1.f); soloHead0(off);
    off.setCrossfade(false);
    off.toggleRecord();
    for (int i = 0; i < N; ++i) off.process((float)i / (float)(N - 1));
    off.toggleRecord();
    float offDelta = maxSeamDelta(off, N * 3);
    check(offDelta > 0.9f,               "crossfade: seam delta large when off");
    check(onDelta < offDelta * 0.2f,     "crossfade: on far smoother than off");
}

static void test_minimum_audible_window() {
    LoopEngine at48k(1);
    at48k.reset(48000.f, 1.f);
    at48k.setCrossfade(false);
    at48k.toggleRecord();
    for (int i = 0; i < 256; ++i) at48k.process(static_cast<float>(i + 1));
    at48k.toggleRecord();
    at48k.setSize(0, 0.f);
    float first = at48k.process(0.f);
    auto snap48 = at48k.displaySnapshot();
    float second = at48k.process(0.f);
    float win48 = (snap48.winEnd01[0] - snap48.winStart01[0]) * 256.f;
    check(near(win48, 48.f), "min_size: 48k window is 1 ms");
    check(!near(first, second), "min_size: playhead advances and output changes");

    LoopEngine at96k(1);
    at96k.reset(96000.f, 1.f);
    at96k.setCrossfade(false);
    at96k.toggleRecord();
    for (int i = 0; i < 256; ++i) at96k.process(static_cast<float>(i + 1));
    at96k.toggleRecord();
    at96k.setSize(0, 0.f);
    at96k.process(0.f);
    auto snap96 = at96k.displaySnapshot();
    float win96 = (snap96.winEnd01[0] - snap96.winStart01[0]) * 256.f;
    check(near(win96, 96.f), "min_size: 96k window is 1 ms");

    LoopEngine shortLoop(1);
    shortLoop.reset(96000.f, 1.f);
    shortLoop.setCrossfade(false);
    shortLoop.toggleRecord();
    shortLoop.process(1.f);
    shortLoop.process(2.f);
    shortLoop.toggleRecord();
    shortLoop.setSize(0, 0.f);
    shortLoop.process(0.f);
    auto shortSnap = shortLoop.displaySnapshot();
    float shortWin = shortSnap.winEnd01[0] - shortSnap.winStart01[0];
    check(near(shortWin, 1.f), "min_size: short loop uses full available length");
}

static void test_jitter_crossfade_continuity() {
    LoopEngine e;
    e.reset(48000.f, 2.f);
    soloHead0(e);
    e.toggleRecord();
    const int N = 24000;                       // 0.5 s loop
    for (int i = 0; i < N; ++i)
        e.process((float)std::sin(2.0 * M_PI * 100.0 * i / 48000.0));
    e.toggleRecord();
    e.setJitter(0, 1.f);
    e.setSize(0, 0.25f);                       // 6000-sample window, ~240-sample fade
    e.setCrossfade(true);
    float prev = e.process(0.f);
    float maxDelta = 0.f;
    for (int i = 0; i < 96000; ++i) {          // 2 s -> ~16 jittered wraps
        float cur = e.process(0.f);
        float d = std::fabs(cur - prev);
        if (d > maxDelta) maxDelta = d;
        prev = cur;
    }
    // Continuous playback slope of a 100 Hz unit sine at 48 kHz is ~0.013
    // per sample; the smoothstep fade over ~240 samples adds at most ~0.013
    // per sample even between uncorrelated windows. The jitter bug produced
    // full-scale steps (~2.0) at every wrap.
    char msg[64];
    std::snprintf(msg, sizeof(msg), "jitter_crossfade: maxDelta=%.4f < 0.1", maxDelta);
    check(maxDelta < 0.1f, msg);
}

static void test_sample_rate_change_preserves_loop() {
    LoopEngine e;
    e.reset(10.f, 100.f);
    soloHead0(e);
    e.toggleRecord();
    for (float x : {1.f, 2.f, 3.f, 4.f}) e.process(x);
    e.toggleRecord();
    e.setSampleRate(20.f);   // retune only: loop and buffer must survive
    check(e.loopLength() == 4, "sr_change: loop length preserved");
    check(near(e.process(0.f), 1.f), "sr_change: out[0]==1");
    check(near(e.process(0.f), 2.f), "sr_change: out[1]==2");
    check(near(e.process(0.f), 3.f), "sr_change: out[2]==3");
    check(near(e.process(0.f), 4.f), "sr_change: out[3]==4");
}

static void test_sample_rate_change_multi_head_nondefault_speed() {
    LoopEngine e;                       // default 4 heads
    e.reset(10.f, 100.f);
    e.toggleRecord();
    for (float x : {1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f}) e.process(x);
    e.toggleRecord();
    // Non-default per-head state that must survive the retune:
    e.setSpeed(0, 2.f);
    e.setSpeed(1, -1.f);
    e.setLevel(2, 0.f);
    e.setLevel(3, 0.f);
    e.setSampleRate(20.f);
    check(e.loopLength() == 8, "sr_multi: loop length preserved");
    // Head 0 at speed 2 reads every other sample; head 1 reads in reverse.
    // Just assert content survived and output is finite and nonzero:
    bool finite = true; float energy = 0.f;
    for (int i = 0; i < 16; ++i) {
        float v = e.process(0.f);
        if (!std::isfinite(v)) finite = false;
        energy += std::fabs(v);
    }
    check(finite, "sr_multi: output finite after retune");
    check(energy > 0.1f, "sr_multi: loop content audible after retune");
}

static void test_sample_rate_change_redundant_same_rate() {
    LoopEngine e;
    e.reset(10.f, 100.f);
    soloHead0(e);
    e.toggleRecord();
    for (float x : {1.f, 2.f, 3.f, 4.f}) e.process(x);
    e.toggleRecord();
    e.setSampleRate(10.f);   // same rate — must be a no-op for the loop
    e.setSampleRate(10.f);   // and again
    check(e.loopLength() == 4, "sr_same: loop survives redundant same-rate calls");
    check(near(e.process(0.f), 1.f), "sr_same: out[0]==1");
    check(near(e.process(0.f), 2.f), "sr_same: out[1]==2");
}

static void test_sample_rate_change_mid_recording() {
    LoopEngine e;
    e.reset(10.f, 100.f);
    soloHead0(e);
    e.toggleRecord();
    e.process(1.f); e.process(2.f);
    e.setSampleRate(20.f);
    check(e.isRecording(), "sr_change mid-rec: still recording");
    e.process(3.f); e.process(4.f);
    e.toggleRecord();
    check(e.loopLength() == 4, "sr_change mid-rec: length 4");
    check(near(e.process(0.f), 1.f), "sr_change mid-rec: content preserved");
}

static void test_nan_input_recorded_as_zero() {
    LoopEngine e;
    e.reset(10.f, 100.f);
    soloHead0(e);
    e.toggleRecord();
    e.process(1.f); e.process(NAN); e.process(3.f); e.process(4.f);
    e.toggleRecord();
    check(near(e.process(0.f), 1.f), "nan guard: out[0]==1");
    check(near(e.process(0.f), 0.f), "nan guard: NaN recorded as 0");
    check(near(e.process(0.f), 3.f), "nan guard: out[2]==3");
    check(near(e.process(0.f), 4.f), "nan guard: out[3]==4");
}

static void test_level_smoothing() {
    LoopEngine e(1); e.reset(48000.f, 1.f); e.setCrossfade(false);
    e.toggleRecord();
    for (int i = 0; i < 1000; ++i) e.process(1.f);   // constant loop
    e.toggleRecord();
    e.setLevel(0, 0.f);
    for (int i = 0; i < 3000; ++i) e.process(0.f);   // settle at 0
    e.setLevel(0, 1.f);                               // step 0 -> 1
    float first = e.process(0.f);
    check(first > 0.f && first < 0.05f, "smooth: no full level step in one sample");
    float last = 0.f;
    for (int i = 0; i < 3000; ++i) last = e.process(0.f);
    check(near(last, 1.f, 0.01f), "smooth: settles at target");
}

static void test_sample_rate_change_empty_reallocates() {
    LoopEngine e;
    e.reset(10.f, 1.f);      // maxSamples = 10
    soloHead0(e);
    e.setSampleRate(20.f);   // no loop -> full reset, maxSamples = 20
    e.toggleRecord();
    for (int i = 0; i < 25; ++i) e.process(1.f);
    check(e.loopLength() == 20, "sr_change empty: ceiling at new rate");
    check(!e.isRecording(),    "sr_change empty: auto-stopped at new ceiling");
}

// ---------------------------------------------------------------------------
// Out-of-memory resilience. The looper's buffers are the only large allocation
// in the plugin (~23 MB at 48 kHz / 60 s), and on MetaModule they can genuinely
// fail. reset() must stay all-or-nothing and keep throwing (the firmware
// catches it at module-construction time); setSampleRate() must never throw
// (the firmware does NOT wrap PatchPlayer::set_samplerate, so a throw there
// reaches std::terminate).
//
// No allocator mocking: a big enough request makes std::vector really throw.
// The size has to exceed the address space, not just RAM — a merely huge
// request (hundreds of GB) succeeds lazily on macOS and then gets the process
// SIGKILLed while it zero-fills. 48 kHz * 2e10 s = 9.6e14 samples ~ 3.8 PB per
// channel is past any 47-bit user VA, so malloc fails outright and cleanly.
// Only the product matters, so both reset args are fixed in one helper.
// ---------------------------------------------------------------------------
static bool resetUnallocatable(LoopEngine& e) {
    try { e.reset(48000.f, 2e10f); }
    catch (const std::bad_alloc&) { return true; }
    return false;
}

static void test_reset_alloc_failure_throws_and_preserves_loop() {
    LoopEngine e;
    e.reset(10.f, 100.f);
    soloHead0(e);
    e.toggleRecord();
    for (float x : {1.f, 2.f, 3.f, 4.f}) e.process(x);
    e.toggleRecord();                       // loop = 1,2,3,4

    check(resetUnallocatable(e), "reset oom: bad_alloc propagates out of reset()");

    // All-or-nothing: the engine is exactly as it was — same loop, same
    // buffer, same rate — so it plays back bit-identically.
    check(e.loopLength() == 4, "reset oom: loop length preserved");
    check(e.hasLoop(),         "reset oom: loop still present");
    check(!e.isRecording(),    "reset oom: record state untouched");
    check(near(e.process(0.f), 1.f), "reset oom: out[0]==1");
    check(near(e.process(0.f), 2.f), "reset oom: out[1]==2");
    check(near(e.process(0.f), 3.f), "reset oom: out[2]==3");
    check(near(e.process(0.f), 4.f), "reset oom: out[3]==4");
    check(near(e.process(0.f), 1.f), "reset oom: out[4]==1 (wrapped)");
    // reset() is the construction path: it reports failure by throwing, not
    // via the shortfall flag (which means "running with a smaller buffer").
    check(!e.bufferShortfall(), "reset oom: no shortfall flag from reset()");
}

static void test_reset_alloc_failure_preserves_capacity() {
    LoopEngine e;
    e.reset(10.f, 1.f);                     // capacity 10 samples
    soloHead0(e);
    check(resetUnallocatable(e), "reset oom capacity: bad_alloc propagates");
    check(e.maxLoopSamples() == 10, "reset oom capacity: maxLoopSamples unchanged");
    e.toggleRecord();
    for (int i = 0; i < 15; ++i) e.process(1.f);
    check(e.loopLength() == 10, "reset oom capacity: old ceiling still enforced");
    check(!e.isRecording(),     "reset oom capacity: auto-ended at old ceiling");
}

static void test_reset_success_reinitializes_state() {
    LoopEngine e;
    e.reset(10.f, 100.f);
    soloHead0(e);
    e.toggleRecord();
    for (float x : {1.f, 2.f, 3.f, 4.f}) e.process(x);
    e.toggleRecord();
    e.setSpeed(0, -2.f); e.setSize(0, 0.25f); e.setPosition(0, 0.1f);
    const std::uint32_t rev = e.waveformRevision();

    e.reset(10.f, 1.f);                     // successful reset, smaller capacity
    check(!e.hasLoop() && e.loopLength() == 0, "reset ok: loop cleared");
    check(!e.isRecording(),                    "reset ok: not recording");
    check(e.maxLoopSamples() == 10,            "reset ok: capacity resized");
    check(!e.bufferShortfall(),                "reset ok: no shortfall flag");
    auto s = e.displaySnapshot();
    check(s.loopLen == 0 && s.recordedLen == 0 && !s.recording,
                                               "reset ok: display mirrors cleared");
    check(e.waveformRevision() != rev,         "reset ok: waveform revision bumped");
    // Head params are back to defaults (speed 1, size 1, centre 0.5).
    soloHead0(e);
    e.toggleRecord();
    for (float x : {5.f, 6.f, 7.f, 8.f}) e.process(x);
    e.toggleRecord();
    check(near(e.process(0.f), 5.f), "reset ok: out[0]==5 (head defaults restored)");
    check(near(e.process(0.f), 6.f), "reset ok: out[1]==6");
    check(near(e.process(0.f), 7.f), "reset ok: out[2]==7");
}

static void test_sample_rate_change_alloc_failure_is_nonfatal() {
    // One head: at the rate used below the ~2 ms level smoother is frozen, so
    // soloHead0's Level-0 on the other heads would never take effect.
    LoopEngine e(1);
    // 1 Hz keeps the crossfade at 0 samples (like the suite's 10 Hz rate) while
    // leaving maxSeconds_ huge, so the *rate change* is what can't be satisfied:
    // 1e11 Hz * 1e4 s * 4 bytes ~ 4 PB per channel.
    e.reset(1.f, 1e4f);                     // capacity 10000 samples
    check(e.maxLoopSamples() == 10000, "sr oom: capacity 10000 before the change");

    bool threw = false;
    try { e.setSampleRate(1e11f); }
    catch (...) { threw = true; }
    check(!threw, "sr oom: setSampleRate never throws");
    check(e.bufferShortfall(), "sr oom: shortfall flag raised");
    check(e.maxLoopSamples() == 10000,
          "sr oom: maxLoopSamples still matches the real buffer");

    // Sticky-until-consumed, so the host notifies exactly once.
    check(e.consumeBufferShortfall(),  "sr oom: consume reports the shortfall");
    check(!e.bufferShortfall(),        "sr oom: flag cleared after consume");
    check(!e.consumeBufferShortfall(), "sr oom: second consume reports nothing");

    // Still a working looper, just with the shorter maximum loop time.
    // The 5 ms crossfade is ~5e8 samples at this absurd rate, so turn it off
    // to compare exact sample values.
    e.setCrossfade(false);
    e.toggleRecord();
    for (int i = 0; i < 12000; ++i) e.process(static_cast<float>(i % 5) + 1.f);
    check(e.loopLength() == 10000, "sr oom: records up to the surviving capacity");
    check(!e.isRecording(),        "sr oom: auto-ended at the surviving ceiling");
    e.restartHead(0);
    check(near(e.process(0.f), 1.f), "sr oom: plays back out[0]==1");
    check(near(e.process(0.f), 2.f), "sr oom: plays back out[1]==2");
    check(near(e.process(0.f), 3.f), "sr oom: plays back out[2]==3");
}

static void test_sample_rate_change_alloc_failure_retries() {
    LoopEngine e(1);
    e.reset(1.f, 1e4f);
    bool threw = false;
    try {
        e.setSampleRate(1e11f);
        e.consumeBufferShortfall();
        e.setSampleRate(1e11f);   // same (still unsatisfiable) rate: retry, no throw
    } catch (...) { threw = true; }
    check(!threw, "sr oom retry: repeated failing rate change never throws");
    check(e.bufferShortfall(), "sr oom retry: shortfall re-raised on the retry");
    // Falling back to a rate that fits clears the shortfall.
    e.setSampleRate(1.f);
    check(!e.bufferShortfall(), "sr oom retry: a successful rate change clears it");
    check(e.maxLoopSamples() == 10000, "sr oom retry: capacity restored for 1 Hz");
}

static void test_sample_rate_change_success_sets_no_shortfall() {
    LoopEngine e;
    e.reset(10.f, 1.f);      // maxSamples = 10
    soloHead0(e);
    check(!e.bufferShortfall(), "sr ok: no shortfall after reset");
    e.setSampleRate(20.f);   // no loop -> full reset, maxSamples = 20
    check(!e.bufferShortfall(),        "sr ok: successful rate change sets no flag");
    check(!e.consumeBufferShortfall(), "sr ok: nothing to consume");
    check(e.maxLoopSamples() == 20,    "sr ok: capacity sized for the new rate");
    e.setSampleRate(20.f);   // redundant call: early-out, still no flag
    check(!e.bufferShortfall(), "sr ok: redundant same-rate call sets no flag");
    e.toggleRecord();
    for (int i = 0; i < 25; ++i) e.process(1.f);
    check(e.loopLength() == 20, "sr ok: ceiling at the new rate (unchanged)");
}

static void test_overdub_ramps_declick() {
    LoopEngine e(1); e.reset(48000.f, 1.f); e.setCrossfade(false);  // xfade = 240
    e.toggleRecord();
    for (int i = 0; i < 1000; ++i) e.process(0.f);   // silent 1000-sample loop
    e.toggleRecord();
    e.toggleRecord();                                 // overdub start: gain ramps 0 -> 1
    for (int i = 0; i < 500; ++i) e.process(1.f);
    e.toggleRecord();                                 // stop: gain ramps 1 -> 0
    check(e.isRecording(), "ramps: still recording right after stop toggle");
    for (int i = 0; i < 239; ++i) e.process(1.f);
    check(e.isRecording(), "ramps: still recording 239 samples into stop ramp");
    for (int i = 0; i < 3; ++i) e.process(1.f);
    check(!e.isRecording(), "ramps: recording ends when the ramp completes");
    // The buffer holds the write-gain envelope (input was constant 1).
    e.restartHead(0);
    static float buf[1000];
    for (int i = 0; i < 1000; ++i) buf[i] = e.process(0.f);
    check(near(buf[0],   0.f,  0.01f),  "ramps: first overdub sample at gain 0");
    check(near(buf[120], 0.5f, 0.01f),  "ramps: up-ramp midpoint");
    check(near(buf[300], 1.f,  0.001f), "ramps: full gain after up-ramp");
    check(near(buf[620], 0.5f, 0.01f),  "ramps: down-ramp midpoint");
    check(near(buf[745], 0.f,  0.01f),  "ramps: zero at ramp end");
    bool mono = true;
    for (int i = 501; i < 745; ++i) if (buf[i] > buf[i-1] + 1e-4f) mono = false;
    check(mono, "ramps: stop ramp is monotonic");
}

static void test_stop_ramp_rearm() {
    LoopEngine e(1); e.reset(48000.f, 1.f); e.setCrossfade(false);
    e.toggleRecord();
    for (int i = 0; i < 1000; ++i) e.process(0.f);
    e.toggleRecord();
    e.toggleRecord();
    for (int i = 0; i < 500; ++i) e.process(1.f);    // gain settled at 1
    e.toggleRecord();                                 // stop ramp begins
    for (int i = 0; i < 120; ++i) e.process(1.f);     // gain ~0.5
    e.toggleRecord();                                 // re-arm: ramp back up from 0.5
    check(e.isRecording(), "rearm: still recording");
    for (int i = 0; i < 130; ++i) e.process(1.f);     // gain back to 1 by ~write 740
    e.toggleRecord();
    for (int i = 0; i < 250; ++i) e.process(1.f);     // final stop completes
    check(!e.isRecording(), "rearm: final stop completes");
    e.restartHead(0);
    static float buf[1000];
    for (int i = 0; i < 1000; ++i) buf[i] = e.process(0.f);
    check(near(buf[618], 0.5f, 0.02f), "rearm: dip bottoms out ~0.5, no snap to 0");
    // gain reached 1 at write ~740; writes 740..749 land before the final stop toggle
    check(near(buf[745], 1.f,  0.01f), "rearm: recovered to full gain");
    bool noSnap = true;
    for (int i = 501; i < 999; ++i)
        if (std::fabs(buf[i] - buf[i-1]) > 0.006f) noSnap = false;   // step is 1/240
    check(noSnap, "rearm: per-sample write-gain delta never exceeds the ramp step");
}

static void test_clear_during_stop_ramp() {
    LoopEngine e(1);
    e.reset(48000.f, 1.f);
    e.toggleRecord();
    for (int i = 0; i < 1000; ++i) e.process(1.f);
    e.toggleRecord();                       // loop closed
    e.toggleRecord();                       // start overdub (up-ramp)
    for (int i = 0; i < 400; ++i) e.process(0.5f);
    e.toggleRecord();                       // request stop -> down-ramp pending
    for (int i = 0; i < 100; ++i) e.process(0.5f);   // mid down-ramp
    e.clear();                              // clear during the pending stop-ramp
    check(!e.isRecording(), "clear_stopramp: not recording after clear");
    check(e.loopLength() == 0, "clear_stopramp: loop erased");
    bool finite = true;
    for (int i = 0; i < 500; ++i)
        if (!std::isfinite(e.process(0.f))) finite = false;
    check(finite, "clear_stopramp: output finite after clear");
    // Engine must be able to record a fresh loop cleanly:
    e.toggleRecord();
    for (int i = 0; i < 500; ++i) e.process(1.f);
    e.toggleRecord();
    check(e.loopLength() == 500, "clear_stopramp: fresh loop records normally");
    check(near(e.process(0.f), 1.f), "clear_stopramp: fresh loop plays back");
}

static void test_reset_during_stop_ramp() {
    LoopEngine e(1);
    e.reset(48000.f, 1.f);
    e.toggleRecord();
    for (int i = 0; i < 1000; ++i) e.process(1.f);
    e.toggleRecord();
    e.toggleRecord();                       // overdub
    for (int i = 0; i < 400; ++i) e.process(0.5f);
    e.toggleRecord();                       // stop pending
    for (int i = 0; i < 100; ++i) e.process(0.5f);
    e.reset(48000.f, 1.f);                  // full reset mid down-ramp
    check(!e.isRecording(), "reset_stopramp: not recording after reset");
    check(e.loopLength() == 0, "reset_stopramp: loop gone after reset");
    e.toggleRecord();
    for (int i = 0; i < 300; ++i) e.process(1.f);
    e.toggleRecord();
    check(e.loopLength() == 300, "reset_stopramp: records normally after reset");
}

static void test_record_off_mid_up_ramp() {
    LoopEngine e(1);
    e.reset(48000.f, 1.f);
    e.toggleRecord();
    for (int i = 0; i < 1000; ++i) e.process(1.f);
    e.toggleRecord();                       // loop closed: all 1.0
    e.toggleRecord();                       // overdub: up-ramp starts (240 samples)
    for (int i = 0; i < 100; ++i) e.process(1.f);   // mid up-ramp
    e.toggleRecord();                       // stop while still ramping in
    bool finite = true;
    for (int i = 0; i < 2000; ++i)
        if (!std::isfinite(e.process(0.f))) finite = false;
    check(finite, "up_ramp_off: output finite");
    check(!e.isRecording(), "up_ramp_off: recording ended");
    // The partially-ramped overdub wrote at most ~2x content briefly; loop
    // content must stay bounded (constant-1 loop + up-to-unity ramped add of 1.0):
    float peak = 0.f;
    for (int i = 0; i < 1000; ++i) peak = std::max(peak, std::fabs(e.process(0.f)));
    check(peak <= 2.f + 1e-3f, "up_ramp_off: overdubbed content bounded");
}

static void test_ramp_layer_combined_expression() {
    // Mid-ramp the write must follow buf = old + g·(fb·old − old) + g·in.
    LoopEngine e(1); e.reset(48000.f, 1.f); e.setCrossfade(false);
    e.toggleRecord();
    for (int i = 0; i < 1000; ++i) e.process(1.f);   // loop of constant 1
    e.toggleRecord();
    e.setWriteMode(LoopEngine::WriteMode::Layer);
    e.toggleRecord();
    for (int i = 0; i < 500; ++i) e.process(0.5f);
    e.toggleRecord();
    for (int i = 0; i < 250; ++i) e.process(0.5f);   // stop ramp completes
    e.restartHead(0);
    static float buf[1000];
    for (int i = 0; i < 1000; ++i) buf[i] = e.process(0.f);
    auto expect = [](float g) {
        const float fb = LoopEngine::LAYER_FEEDBACK;   // old = 1, in = 0.5
        return 1.f + g * (fb - 1.f) + g * 0.5f;
    };
    check(near(buf[120], expect(0.5f), 0.005f), "combined: mid-up-ramp expression");
    check(near(buf[300], expect(1.f),  0.005f), "combined: full-gain expression");
    check(near(buf[620], expect(0.5f), 0.005f), "combined: mid-stop-ramp expression");
}

static void test_write_mode_replace() {
    LoopEngine e; e.reset(10.f, 100.f); soloHead0(e);
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(1.f);    // loop = 1,1,1,1
    e.toggleRecord();
    e.setWriteMode(LoopEngine::WriteMode::Replace);
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(2.f);    // full destructive pass
    e.toggleRecord();
    check(near(e.process(0.f), 2.f), "replace: out[0]==2 (old content gone)");
    check(near(e.process(0.f), 2.f), "replace: out[1]==2");
}

static void test_write_mode_layer() {
    LoopEngine e; e.reset(10.f, 100.f); soloHead0(e);
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(1.f);
    e.toggleRecord();
    e.setWriteMode(LoopEngine::WriteMode::Layer);
    e.setLevel(0, 0.f);                            // mute so overdub input is isolated
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(0.5f);   // buf -> 1*FB + 0.5
    e.toggleRecord();
    e.setLevel(0, 1.f);
    const float expected = LoopEngine::LAYER_FEEDBACK + 0.5f;
    check(near(e.process(0.f), expected), "layer: buf == old*FB + new");
    // an idle loop never fades: play 20 more samples, value unchanged
    float v = 0.f; for (int i = 0; i < 19; ++i) v = e.process(0.f);
    check(near(v, expected), "layer: idle playback does not decay");
}

static void test_write_mode_decay_at_low_rate_matches_layer() {
    // At the 10 Hz test rate the Decay LP coefficient saturates to 1
    // (passthrough), so Decay == Layer sample-exactly there.
    LoopEngine e; e.reset(10.f, 100.f); soloHead0(e);
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(1.f);
    e.toggleRecord();
    e.setWriteMode(LoopEngine::WriteMode::Decay);
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(0.5f);
    e.toggleRecord();
    check(near(e.process(0.f), LoopEngine::LAYER_FEEDBACK + 0.5f),
          "decay@10Hz: identical to layer (LP is passthrough)");
}

static void test_write_mode_decay_rolls_off_highs() {
    // Same per-pass feedback as Layer, plus a one-pole LP in the write path:
    // a Nyquist-rate square must decay much faster in Decay than in Layer.
    // Loop is 4800 samples; only the region past sample 1000 is measured so
    // the Task-2 write-gain ramps (240 samples at each edge) can't touch it.
    auto passRms = [](LoopEngine::WriteMode m) {
        LoopEngine e(1); e.reset(48000.f, 1.f); e.setCrossfade(false);
        e.toggleRecord();
        for (int i = 0; i < 4800; ++i) e.process((i & 1) ? -1.f : 1.f);
        e.toggleRecord();
        e.setWriteMode(m);
        e.toggleRecord();
        for (int i = 0; i < 4800; ++i) e.process(0.f);   // one silent overdub pass
        e.toggleRecord();
        for (int i = 0; i < 300; ++i) e.process(0.f);    // let any stop ramp finish
        e.restartHead(0);
        double acc = 0.0; int n = 0;
        for (int i = 0; i < 4800; ++i) {
            float v = e.process(0.f);
            if (i >= 1000) { acc += double(v) * v; ++n; }
        }
        return std::sqrt(acc / n);
    };
    float rLayer = (float)passRms(LoopEngine::WriteMode::Layer);
    float rDecay = (float)passRms(LoopEngine::WriteMode::Decay);
    check(near(rLayer, LoopEngine::LAYER_FEEDBACK, 0.02f),
          "decay_hf: layer pass keeps FB*amplitude at Nyquist");
    check(rDecay < 0.5f * rLayer, "decay_hf: Decay kills HF much faster than Layer");
}

// Switching write mode to Decay mid-pass must seed the tone filter from
// current buffer content, not stale state (previously only toggleRecord seeded).
static void test_write_mode_decay_midpass_switch_seeds_lp() {
    auto record = [](LoopEngine& e) {
        e.reset(48000.f, 1.f); e.setCrossfade(false);
        soloHead0(e);
        e.toggleRecord();
        for (int i = 0; i < 1000; ++i) e.process(1.f);
        e.toggleRecord();
    };
    LoopEngine a(1), b(1);
    record(a); record(b);
    a.setWriteMode(LoopEngine::WriteMode::Decay);
    b.setWriteMode(LoopEngine::WriteMode::Layer);
    a.toggleRecord(); b.toggleRecord();          // start overdub pass on both
    for (int i = 0; i < 400; ++i) { a.process(0.f); b.process(0.f); }  // past the 240-sample up-ramp
    b.setWriteMode(LoopEngine::WriteMode::Decay);  // mid-pass switch
    // Post-switch writes hit not-yet-rewritten indices (old == original 1.0
    // in both engines) with the same odGain; only the LP state can differ.
    for (int i = 0; i < 100; ++i) { a.process(0.f); b.process(0.f); }
    a.toggleRecord(); b.toggleRecord();          // stop; ramps run out
    for (int i = 0; i < 400; ++i) { a.process(0.f); b.process(0.f); }
    // Both heads read the same buffer positions from here: restart them to
    // window start so playback index lines up with buffer index directly
    // (crossfade is off, so no seam blending to account for).
    a.restartHead(0); b.restartHead(0);
    bool matched = true;
    for (int i = 0; i < 1000; ++i) {
        float va = a.process(0.f), vb = b.process(0.f);
        if (i >= 400 && i < 500 && std::fabs(va - vb) > 1e-3f) matched = false;
    }
    check(matched, "decay_midpass: post-switch writes match a pass-start-seeded engine");
}

static void test_write_mode_buffer_tracks_decay() {
    LoopEngine e; e.reset(10.f, 100.f); soloHead0(e);
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(1.f);
    e.toggleRecord();
    e.setWriteMode(LoopEngine::WriteMode::Layer);
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(0.f);          // silent pass: buf *= FB
    e.toggleRecord();
    check(near(e.sampleData(0)[0], LoopEngine::LAYER_FEEDBACK),
          "write mode: buffer tracks decayed content");
}

static void test_grid_size_snaps_to_segments() {
    LoopEngine e; record_ramp(e, 16);
    e.setGrid(4);                    // seg = 4 samples
    e.setSize(0, 0.3f);              // 4.8 samples -> rounds to 1 segment (4)
    // centre 0.5 -> continuous start 8-2=6 -> k = lround(6/4) = 2 -> window [8,12)
    check(near(e.process(0.f), 9.f),  "grid_size: out[0]==9");
    check(near(e.process(0.f), 10.f), "grid_size: out[1]==10");
    check(near(e.process(0.f), 11.f), "grid_size: out[2]==11");
    check(near(e.process(0.f), 12.f), "grid_size: out[3]==12");
    check(near(e.process(0.f), 9.f),  "grid_size: out[4]==9 (wrapped)");
    // Window is static (no jitter); settle a full throttle period (display
    // mirrors tick every 64th sample) so the snapshot below reflects
    // the current window instead of a stale pre-setSize value.
    for (int i = 0; i < 64; ++i) e.process(0.f);
    const auto s = e.displaySnapshot();
    check(s.grid == 4, "grid_size: snapshot reports grid");
    check(near(s.winStart01[0], 0.5f) && near(s.winEnd01[0], 0.75f),
          "grid_size: snapshot window on segment bounds");
}

static void test_grid_position_snaps_to_boundaries() {
    LoopEngine e; record_ramp(e, 16);
    e.setGrid(4);
    e.setSize(0, 0.25f);             // exactly 1 segment
    e.setPosition(0, 0.1f);          // continuous start -0.4 -> k=0 -> [0,4)
    check(near(e.process(0.f), 1.f), "grid_pos: low position snaps to segment 0");
    e.setPosition(0, 0.4f);          // continuous start 4.4 -> k=1 -> [4,8)
    check(near(e.process(0.f), 5.f), "grid_pos: position snaps to segment 1");
}

static void test_grid_window_clamped_inside_loop() {
    LoopEngine e; record_ramp(e, 16);
    e.setGrid(4);
    e.setSize(0, 0.6f);              // 9.6 -> 2 segments (8 samples)
    e.setPosition(0, 1.f);           // start 12 -> k clamped to 2 -> [8,16)
    check(near(e.process(0.f), 9.f), "grid_clamp: window pinned inside loop");
    // Settle past the display-mirror throttle (gated every 64th
    // sample) before reading the snapshot; the window is static here.
    for (int i = 0; i < 64; ++i) e.process(0.f);
    const auto s = e.displaySnapshot();
    check(near(s.winStart01[0], 0.5f) && near(s.winEnd01[0], 1.f),
          "grid_clamp: snapshot [0.5,1.0]");
}

static void test_grid_min_one_segment() {
    LoopEngine e; record_ramp(e, 16);
    e.setGrid(4);
    e.setSize(0, 0.01f);             // under one segment -> grows to 1 segment
    // Settle past the display-mirror throttle (gated every 64th
    // sample) before reading the snapshot; the window is static here.
    for (int i = 0; i < 64; ++i) e.process(0.f);
    const auto s = e.displaySnapshot();
    check(near(s.winEnd01[0] - s.winStart01[0], 0.25f),
          "grid_min: window grows to one segment");
}

static void test_grid_full_size_plays_whole_loop() {
    LoopEngine e; record_ramp(e, 16);
    e.setGrid(8);
    e.setPosition(0, 0.9f);          // size 1 -> all 8 segments, position moot
    check(near(e.process(0.f), 1.f), "grid_full: out[0]==1");
    for (int i = 1; i < 16; ++i) e.process(0.f);
    check(near(e.process(0.f), 1.f), "grid_full: wraps at 16");
}

static void test_grid_off_matches_ungridded() {
    LoopEngine a; record_ramp(a, 16);
    LoopEngine b; record_ramp(b, 16);
    b.setGrid(4); b.setGrid(0);      // enable then disable
    a.setSize(0, 0.3f); a.setPosition(0, 0.37f);
    b.setSize(0, 0.3f); b.setPosition(0, 0.37f);
    bool same = true;
    for (int i = 0; i < 40; ++i) same = same && near(a.process(0.f), b.process(0.f));
    check(same, "grid_off: disabled grid matches ungridded engine");
    check(a.displaySnapshot().grid == 0 && b.displaySnapshot().grid == 0,
          "grid_off: snapshot reports off");
}

static void test_grid_invalid_values_mean_off() {
    LoopEngine e; record_ramp(e, 16);
    e.setGrid(1);                    // <2 segments is meaningless -> off
    e.setSize(0, 0.3f); e.setPosition(0, 0.37f);
    e.process(0.f);
    check(e.displaySnapshot().grid == 0, "grid_invalid: 1 segment reads as off");
}

static void test_grid_exclude_head() {
    // Excluded head matches an ungridded engine sample-for-sample.
    LoopEngine a; record_ramp(a, 16);
    LoopEngine b; record_ramp(b, 16);
    b.setGrid(4); b.setGridExclude(0, true);
    a.setSize(0, 0.3f); a.setPosition(0, 0.37f);
    b.setSize(0, 0.3f); b.setPosition(0, 0.37f);
    bool same = true;
    for (int i = 0; i < 40; ++i) same = same && near(a.process(0.f), b.process(0.f));
    check(same, "grid_excl: excluded head matches ungridded engine");
    check(b.displaySnapshot().grid == 4, "grid_excl: grid still reported for display");

    // Other heads still snap while head 0 is excluded (per-head gate).
    LoopEngine c; record_ramp(c, 16);
    c.setGrid(4); c.setGridExclude(0, true);
    c.setSize(1, 0.3f); c.setPosition(1, 0.37f);   // would snap: 1 segment at [4,8)
    // Settle past the display-mirror throttle (gated every 64th
    // sample) before reading the snapshot; the window is static here.
    for (int i = 0; i < 64; ++i) c.process(0.f);
    const auto s = c.displaySnapshot();
    check(near(s.winStart01[1], 0.25f) && near(s.winEnd01[1], 0.5f),
          "grid_excl: non-excluded head still snaps");

    // Re-including restores snapping.
    c.setGridExclude(0, false);
    c.setSize(0, 0.3f); c.setPosition(0, 0.37f);
    for (int i = 0; i < 64; ++i) c.process(0.f);   // settle past another throttle tick
    const auto s2 = c.displaySnapshot();
    check(near(s2.winStart01[0], 0.25f) && near(s2.winEnd01[0], 0.5f),
          "grid_excl: re-included head snaps again");
}

static void test_grid_jitter_lands_on_boundaries() {
    LoopEngine e; record_ramp(e, 16);
    e.setGrid(4);
    e.setSize(0, 0.25f);
    e.setJitter(0, 1.f);
    bool onGrid = true;
    for (int i = 0; i < 200; ++i) {
        e.process(0.f);
        const auto s = e.displaySnapshot();
        const float k = s.winStart01[0] * 4.f;
        onGrid = onGrid && near(k, std::round(k), 1e-3f);
    }
    check(onGrid, "grid_jitter: jittered windows stay on segment boundaries");
}

static void test_grid_respects_min_window() {
    LoopEngine e; record_ramp(e, 3);     // seg = 0.75 < the 1-sample minimum
    e.setGrid(4);
    e.setSize(0, 0.01f);
    e.process(0.f);
    const auto s = e.displaySnapshot();
    check(s.winEnd01[0] - s.winStart01[0] >= 1.f / 3.f - 1e-4f,
          "grid_minwin: window grew to cover the minimum window");
}

static void test_grid_32_segments() {
    LoopEngine e; record_ramp(e, 64);
    e.setGrid(32);                    // seg = 2 samples
    e.setSize(0, 0.02f);              // 1.28 samples -> rounds to 1 segment (2)
    e.setPosition(0, 0.6f);           // start 38.4-1=37.4 -> k=lround(18.7)=19 -> [38,40)
    check(near(e.process(0.f), 39.f), "grid32: out[0]==39");
    check(near(e.process(0.f), 40.f), "grid32: out[1]==40");
    check(near(e.process(0.f), 39.f), "grid32: out[2]==39 (wrapped)");
    const auto s = e.displaySnapshot();
    check(s.grid == 32, "grid32: snapshot reports grid");
    check(near(s.winStart01[0], 38.f / 64.f) && near(s.winEnd01[0], 40.f / 64.f),
          "grid32: snapshot window is one 1/32 segment on boundaries");
}

// "Trigger when recording = Starts overdubbing": the record toggle that closes
// the initial pass freezes the loop AND keeps recording as an overdub pass.
static void test_continue_overdub_on_close() {
    LoopEngine e; e.reset(48000.f);
    soloHead0(e);
    e.setWriteMode(LoopEngine::WriteMode::Replace);   // overdub enabled (not Lock)
    e.toggleRecord();                                  // start initial pass
    std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
    for (int i = 0; i < 100; ++i) e.process(0.5f, 0.5f, hs);
    e.toggleRecord(true);                              // close with continueOverdub
    check(e.hasLoop(), "continue_overdub: loop frozen on close");
    check(e.loopLength() == 100, "continue_overdub: loop length = frames recorded");
    check(e.isRecording(), "continue_overdub: still recording after close");
}

// With Overdub = Lock (overdub disabled), the setting is overridden: closing the
// initial pass stops recording regardless of continueOverdub.
static void test_continue_overdub_lock_stops() {
    LoopEngine e; e.reset(48000.f);
    soloHead0(e);
    e.setOverdub(false);                               // Lock: overdub disabled
    e.toggleRecord();
    std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
    for (int i = 0; i < 100; ++i) e.process(0.5f, 0.5f, hs);
    e.toggleRecord(true);
    check(e.hasLoop(), "lock_stops: loop frozen on close");
    check(!e.isRecording(), "lock_stops: recording stopped despite continueOverdub");
}

// An armed one-shot head (silent, waiting for a trigger) must still reflect
// Size/Position changes on the display window.
static void test_armed_oneshot_window_tracks_size() {
    LoopEngine e; e.reset(48000.f);
    soloHead0(e);
    e.toggleRecord();
    std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
    for (int i = 0; i < 480; ++i) e.process(0.2f, 0.2f, hs);
    e.toggleRecord();                 // freeze loop, head 0 loops
    e.setOneShot(0, true);            // arm: head 0 goes silent
    e.setSize(0, 0.5f);
    e.setPosition(0, 0.5f);
    // The parked-head display block is throttled to every 64th
    // sample too; settle past a tick instead of reading right after one
    // idle sample.
    for (int i = 0; i < 64; ++i) e.process(0.f, 0.f, hs);
    auto s1 = e.displaySnapshot();
    const float halfWin = s1.winEnd01[0] - s1.winStart01[0];
    e.setSize(0, 1.0f);               // grow the window
    for (int i = 0; i < 64; ++i) e.process(0.f, 0.f, hs);   // settle past another tick
    auto s2 = e.displaySnapshot();
    const float fullWin = s2.winEnd01[0] - s2.winStart01[0];
    check(fullWin > halfWin + 0.1f, "armed one-shot: window grows with Size");
    check(!s2.playing[0], "armed one-shot: head still not playing (silent)");
}

// The per-sample bump in the record paths is throttled to ~every
// 2048 samples (REV_THROTTLE_MASK) so the release store (a full memory
// barrier on ARMv7) and the GUI's waveform re-scan don't run every sample.
// Transition bumps (pass start, pass stop) still make the display converge
// immediately, so a start->write->stop sequence must show the revision
// change right away at both ends while the mid-pass churn stays throttled.
static void test_waveform_revision_throttled_during_recording() {
    LoopEngine e(1);
    e.reset(48000.f, 1.f);
    const auto beforeRecord = e.waveformRevision();
    e.toggleRecord();                       // start: unconditional transition bump
    const auto afterStart = e.waveformRevision();
    check(afterStart != beforeRecord, "throttle: starting a pass bumps immediately");
    const int N = 10000;
    for (int i = 0; i < N; ++i) e.process(0.1f);
    const auto afterWrites = e.waveformRevision();
    // Delta since the pass started must be far below N: with a 2048-sample
    // throttle period, 10000 samples produce only a handful of bumps.
    const std::uint32_t delta = afterWrites - afterStart;
    char msg[80];
    std::snprintf(msg, sizeof(msg), "throttle: revision delta %u over %d samples < 100", delta, N);
    check(delta < 100, msg);
    e.toggleRecord();                       // stop: unconditional transition bump
    const auto afterStop = e.waveformRevision();
    check(afterStop != afterWrites, "throttle: ending the pass bumps immediately");
}

// --- readInterpolatedLR pinning test -------------------------------------
// Bit-exact pin for the readInterpolatedLR interior fast path. Hashes below were captured against the UNMODIFIED per-channel
// readInterpolated path (two separate calls in readHead) before the
// shared-index rework; the reworked code must reproduce them exactly. If a
// hash moves, the rework changed behavior -- fix the code, do not
// regenerate the hash. Scenarios cover: full window, a fractional
// grid-style window (grid=12 segments -> seg=341.333..., matching a
// winStart=341.333/winLen=1365.333 window), the minimum window (tiny), and
// windows pinned to the buffer start/end -- each at a fractional forward
// (0.73) and reverse (-1.31) speed so the head wraps repeatedly over 8192
// samples, exercising both the interior fast path and the near-edge
// fallback (and the seam crossfade, which reads through readRaw).
static std::uint64_t fnv1aFloats(const std::vector<float>& v) {
    std::uint64_t h = 14695981039346656037ull;
    for (float f : v) {
        std::uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        for (int b = 0; b < 4; ++b) {
            h ^= static_cast<std::uint64_t>((bits >> (b * 8)) & 0xFFu);
            h *= 1099511628211ull;
        }
    }
    return h;
}

// Deterministic pseudo-random stereo fill (LCG, not the engine's own jitter
// RNG), then freeze the loop at exactly 4096 samples.
static void pinRecordedEngine(LoopEngine& e) {
    e.reset(48000.f, 1.f);
    e.toggleRecord();
    std::uint32_t lcgL = 12345u, lcgR = 987654321u;
    for (int i = 0; i < 4096; ++i) {
        lcgL = lcgL * 1664525u + 1013904223u;
        lcgR = lcgR * 1664525u + 1013904223u;
        const float inL = static_cast<float>(lcgL >> 8) * (1.f / 16777216.f) * 2.f - 1.f;
        const float inR = static_cast<float>(lcgR >> 8) * (1.f / 16777216.f) * 2.f - 1.f;
        std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
        e.process(inL, inR, hs);
    }
    e.toggleRecord();   // freeze: loopLength() == 4096
}

struct PinScenario {
    const char* name;
    float size, pos; int grid; float speed;
    std::uint64_t expected;
};

static void runPinScenario(const PinScenario& s) {
    LoopEngine e;
    pinRecordedEngine(e);
    e.setGrid(s.grid);
    e.setSize(0, s.size);
    e.setPosition(0, s.pos);
    e.setSpeed(0, s.speed);
    std::vector<float> out;
    out.reserve(8192 * 2);
    for (int i = 0; i < 8192; ++i) {
        std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
        e.process(0.f, 0.f, hs);
        out.push_back(hs[0].l);
        out.push_back(hs[0].r);
    }
    const std::uint64_t got = fnv1aFloats(out);
    if (got != s.expected)
        std::printf("  hash %s: got 0x%016llx expected 0x%016llx\n",
                     s.name, (unsigned long long)got, (unsigned long long)s.expected);
    check(got == s.expected, s.name);
}

static void test_readhead_pinning() {
    static const PinScenario scenarios[] = {
        // name                  size       pos     grid  speed    expected (captured against unmodified code, Step 2)
        {"pin_full_fwd",         1.f,       0.5f,   0,    0.73f,  0xa27d25802bb7e9deull},
        {"pin_full_rev",         1.f,       0.5f,   0,   -1.31f,  0x1db41b575959ffd8ull},
        {"pin_grid_fwd",         1.f/3.f,   0.25f,  12,   0.73f,  0xf814c063ebf03856ull},
        {"pin_grid_rev",         1.f/3.f,   0.25f,  12,  -1.31f,  0xad2e5d08132432d2ull},
        {"pin_tiny_fwd",         0.001f,    0.5f,   0,    0.73f,  0x5ce25cdaeee761a1ull},
        {"pin_tiny_rev",         0.001f,    0.5f,   0,   -1.31f,  0x52b808064ca238d3ull},
        {"pin_edge_start_fwd",   0.1f,      0.0f,   0,    0.73f,  0x28e7e95f89772920ull},
        {"pin_edge_start_rev",   0.1f,      0.0f,   0,   -1.31f,  0x42d0d324330a2cb4ull},
        {"pin_edge_end_fwd",     0.1f,      1.0f,   0,    0.73f,  0x1105faa6b22cdee4ull},
        {"pin_edge_end_rev",     0.1f,      1.0f,   0,   -1.31f,  0xf05ce8104b5667c4ull},
    };
    for (const auto& s : scenarios) runPinScenario(s);
}

int main() {
    test_continue_overdub_on_close();
    test_continue_overdub_lock_stops();
    test_armed_oneshot_window_tracks_size();
    test_minimum_audible_window();
    test_crossfade_declicks_seam();
    test_single_head_engine();
    test_jitter_off_stable();
    test_jitter_moves_window();
    test_restart_head();
    test_one_shot();
    test_one_shot_reverse();
    test_one_shot_fade_out();
    test_one_shot_fade_out_reverse();
    test_one_shot_retrigger_mid_fade();
    test_one_shot_stale_ramp_cleared_on_fresh_trigger();
    test_one_shot_short_fast_window_ramp_and_fade();
    test_jump_head();
    test_one_shot_survives_clear();
    test_display_snapshot_armed();
    test_triggers_no_loop();
    test_stereo_record_play();
    test_mono_convenience_matches_stereo();
    test_per_head_outs();
    test_record_play_1x();
    test_grid_size_snaps_to_segments();
    test_grid_position_snaps_to_boundaries();
    test_grid_window_clamped_inside_loop();
    test_grid_min_one_segment();
    test_grid_full_size_plays_whole_loop();
    test_grid_off_matches_ungridded();
    test_grid_exclude_head();
    test_grid_invalid_values_mean_off();
    test_grid_jitter_lands_on_boundaries();
    test_grid_respects_min_window();
    test_grid_32_segments();
    test_half_speed();
    test_double_speed();
    test_reverse();
    test_subloop_window();
    test_fractional_winstart_wrap_target();
    test_cubic_exact_on_interior_ramp();
    test_cubic_beats_linear_on_sine();
    test_cubic_tiny_loop_taps_bounded();
    test_cubic_reverse_matches_forward_at_position();
    test_overdub_sums();
    test_clear();
    test_buffer_ceiling_autoend();
    test_sample_data();
    test_display_snapshot();
    test_four_heads_mix();
    test_per_head_params_isolated();
    test_display_snapshot_four_heads();
    test_waveform_revision_tracks_write_changes_only();
    test_waveform_revision_throttled_during_recording();
    test_overdub_gate();
    test_overdub_ramps_declick();
    test_stop_ramp_rearm();
    test_clear_during_stop_ramp();
    test_reset_during_stop_ramp();
    test_record_off_mid_up_ramp();
    test_ramp_layer_combined_expression();
    test_write_mode_replace();
    test_write_mode_layer();
    test_write_mode_decay_at_low_rate_matches_layer();
    test_write_mode_decay_rolls_off_highs();
    test_write_mode_decay_midpass_switch_seeds_lp();
    test_write_mode_buffer_tracks_decay();
    test_jitter_crossfade_continuity();
    test_sample_rate_change_preserves_loop();
    test_sample_rate_change_multi_head_nondefault_speed();
    test_sample_rate_change_redundant_same_rate();
    test_sample_rate_change_mid_recording();
    test_sample_rate_change_empty_reallocates();
    test_reset_alloc_failure_throws_and_preserves_loop();
    test_reset_alloc_failure_preserves_capacity();
    test_reset_success_reinitializes_state();
    test_sample_rate_change_alloc_failure_is_nonfatal();
    test_sample_rate_change_alloc_failure_retries();
    test_sample_rate_change_success_sets_no_shortfall();
    test_nan_input_recorded_as_zero();
    test_level_smoothing();
    test_readhead_pinning();
    if (g_failures) { std::printf("\n%d failure(s)\n", g_failures); return 1; }
    std::printf("\nAll tests passed\n");
    return 0;
}
