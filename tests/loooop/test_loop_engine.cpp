#include "../../src/loooop/dsp/LoopEngine.hpp"
#include <cstdio>
#include <cmath>
#include <cstdlib>

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
    check(near(e.process(0.f), 1.5f), "half_speed: out[1]==1.5");
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
    // pos 1.5 -> read interp(buffer[1]=2, buffer[2]=3) = 2.5
    check(near(e.process(0.f), 2.5f), "subloop: out[0]==2.5 (window start)");
    // advance by 1 -> pos 2.5 -> interp(buffer[2]=3, buffer[3]=4) = 3.5
    check(near(e.process(0.f), 3.5f), "subloop: out[1]==3.5");
    // advance -> pos 3.5 >= winEnd 3.5 -> wraps to 1.5 -> 2.5 again
    check(near(e.process(0.f), 2.5f), "subloop: out[2]==2.5 (wrapped in window)");
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

static void test_peaks_record() {
    LoopEngine e; e.reset(10.f, 100.f);   // maxSamples=1000 -> peakBinSize == 1
    e.toggleRecord();
    e.process(0.5f); e.process(-0.25f); e.process(1.f); e.process(0.f);
    e.toggleRecord();
    check(e.peakBinSize() == 1,           "peaks: bin size == 1");
    check(near(e.peakMaxs(0)[0], 0.5f),   "peaks: L bin0 max == 0.5");
    check(near(e.peakMins(0)[0], 0.5f),   "peaks: L bin0 min == 0.5 (single sample)");
    check(near(e.peakMins(0)[1], -0.25f), "peaks: L bin1 min == -0.25");
    check(near(e.peakMaxs(0)[2], 1.f),    "peaks: L bin2 max == 1");
    check(near(e.peakMins(1)[1], -0.25f), "peaks: mono process mirrors into R");
}

static void test_peaks_overdub_and_clear() {
    LoopEngine e; e.reset(10.f, 100.f);
    e.toggleRecord();
    for (int i = 0; i < 4; ++i) e.process(1.f);
    e.toggleRecord();                    // loop = 4 samples of 1.0
    e.toggleRecord();                    // start overdub (write restarts at loop start)
    e.process(-2.f);                     // buffer[0] = 1 + (-2) = -1
    e.toggleRecord();
    check(near(e.peakMins(0)[0], -1.f),  "peaks: overdub resets L bin0 min");
    check(near(e.peakMaxs(0)[0], -1.f),  "peaks: overdub resets L bin0 max");
    check(near(e.peakMins(1)[0], -1.f),  "peaks: overdub resets R bin0 min");
    e.clear();
    check(near(e.peakMaxs(0)[0], 0.f),   "peaks: clear zeroes L bins");
    check(near(e.peakMins(1)[2], 0.f),   "peaks: clear zeroes R bins");
}

static void test_peaks_stereo() {
    LoopEngine e; e.reset(10.f, 100.f);
    std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
    e.toggleRecord();
    e.process(0.5f, -0.5f, hs);
    e.process(-0.25f, 0.25f, hs);
    e.process(1.f, -1.f, hs);
    e.process(0.f, 0.f, hs);
    e.toggleRecord();
    check(near(e.peakMaxs(0)[2], 1.f),    "stereo peaks: L bin2 max == 1");
    check(near(e.peakMins(1)[2], -1.f),   "stereo peaks: R bin2 min == -1");
    check(near(e.peakMins(0)[1], -0.25f), "stereo peaks: L bin1 min == -0.25");
    check(near(e.peakMaxs(1)[1], 0.25f),  "stereo peaks: R bin1 max == 0.25");
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

    e.process(0.f); e.process(0.f);          // head advances to pos 2 of 4
    auto s3 = e.displaySnapshot();
    check(near(s3.headPos01[0], 0.5f),       "snap: head pos 0.5 after 2 samples");
    check(near(s3.winStart01[0], 0.f),       "snap: full window start == 0");
    check(near(s3.winEnd01[0], 1.f),         "snap: full window end == 1");

    e.setSize(0, 0.5f); e.setPosition(0, 0.5f);
    e.process(0.f);                          // one sample so mirrors update
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
    e.process(0.f);                         // advance all heads once
    auto s = e.displaySnapshot();
    check(near(s.headPos01[0], 1.f/8.f),  "snap4: head0 pos 1/8");
    check(near(s.headPos01[1], 2.f/8.f),  "snap4: head1 pos 2/8 (double speed)");
    check(near(s.headPos01[3], 1.f/8.f),  "snap4: head3 pos 1/8");
    check(near(s.winStart01[0], 0.f) && near(s.winEnd01[0], 1.f),
                                          "snap4: head0 full window");
    check(near(s.winStart01[2], 0.25f),   "snap4: head2 window start 0.25");
    check(near(s.winEnd01[2], 0.75f),     "snap4: head2 window end 0.75");
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
    LoopEngine e; record_ramp(e, 4);      // soloHead0: heads 1-3 at level 0
    std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
    e.setLevel(1, 0.5f);
    e.process(0.f, 0.f, hs);
    check(near(hs[0].l, 1.f),  "headouts: head0 at level 1");
    check(near(hs[1].l, 0.5f), "headouts: head1 scaled by its level");
    check(near(hs[2].l, 0.f),  "headouts: muted head silent");
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
    for (int i = 0; i < 16; ++i) at48k.process(static_cast<float>(i + 1));
    at48k.toggleRecord();
    at48k.setSize(0, 0.f);
    float first = at48k.process(0.f);
    auto snap48 = at48k.displaySnapshot();
    float second = at48k.process(0.f);
    float win48 = (snap48.winEnd01[0] - snap48.winStart01[0]) * 16.f;
    check(near(win48, 3.f), "min_size: 48k window is 3 samples");
    check(!near(first, second), "min_size: playhead advances and output changes");

    LoopEngine at96k(1);
    at96k.reset(96000.f, 1.f);
    at96k.setCrossfade(false);
    at96k.toggleRecord();
    for (int i = 0; i < 16; ++i) at96k.process(static_cast<float>(i + 1));
    at96k.toggleRecord();
    at96k.setSize(0, 0.f);
    at96k.process(0.f);
    auto snap96 = at96k.displaySnapshot();
    float win96 = (snap96.winEnd01[0] - snap96.winStart01[0]) * 16.f;
    check(near(win96, 5.f), "min_size: 96k window is 5 samples");

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

int main() {
    test_minimum_audible_window();
    test_crossfade_declicks_seam();
    test_single_head_engine();
    test_jitter_off_stable();
    test_jitter_moves_window();
    test_restart_head();
    test_one_shot();
    test_one_shot_reverse();
    test_jump_head();
    test_one_shot_survives_clear();
    test_triggers_no_loop();
    test_stereo_record_play();
    test_mono_convenience_matches_stereo();
    test_per_head_outs();
    test_record_play_1x();
    test_half_speed();
    test_double_speed();
    test_reverse();
    test_subloop_window();
    test_overdub_sums();
    test_clear();
    test_buffer_ceiling_autoend();
    test_peaks_record();
    test_peaks_overdub_and_clear();
    test_peaks_stereo();
    test_display_snapshot();
    test_four_heads_mix();
    test_per_head_params_isolated();
    test_display_snapshot_four_heads();
    test_overdub_gate();
    if (g_failures) { std::printf("\n%d failure(s)\n", g_failures); return 1; }
    std::printf("\nAll tests passed\n");
    return 0;
}
