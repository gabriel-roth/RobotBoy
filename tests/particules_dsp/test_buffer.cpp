#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <vector>
#include <cmath>
#include <limits>

#include "particules_dsp/types.h"
#include "buffer/recording_buffer.h"
#include "util/interpolation.h"

using namespace particules_dsp;
using Catch::Approx;

static constexpr float kSampleRate = 48000.0f;
static constexpr float kDuration = 0.1f;  // Short buffer for tests

TEST_CASE("RecordingBuffer: RequiredBytes returns positive value", "[buffer]") {
    size_t bytes = RecordingBuffer::RequiredBytes(kSampleRate, kDuration);
    REQUIRE(bytes > 0);
    // Should be at least num_frames * channels * sizeof(float)
    size_t expected_min = static_cast<size_t>(kSampleRate * kDuration) * 2 * sizeof(float);
    REQUIRE(bytes >= expected_min);
}

TEST_CASE("RecordingBuffer: Write and ReadLinear roundtrip", "[buffer]") {
    size_t bytes = RecordingBuffer::RequiredBytes(kSampleRate, kDuration);
    std::vector<uint8_t> mem(bytes, 0);

    RecordingBuffer buf;
    size_t num_frames = static_cast<size_t>(kSampleRate * kDuration);
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);

    // Write a known pattern
    for (size_t i = 0; i < num_frames; ++i) {
        float val = static_cast<float>(i) / static_cast<float>(num_frames);
        buf.Write(val, -val);
    }

    // Read back at integer positions
    for (size_t i = 0; i < 100; ++i) {
        float pos = static_cast<float>(i);
        float l = buf.ReadLinear(0, pos);
        float r = buf.ReadLinear(1, pos);
        float expected = static_cast<float>(i) / static_cast<float>(num_frames);
        REQUIRE(l == Approx(expected).margin(0.001f));
        REQUIRE(r == Approx(-expected).margin(0.001f));
    }
}

TEST_CASE("RecordingBuffer: ReadHermite at integer positions matches samples", "[buffer]") {
    size_t bytes = RecordingBuffer::RequiredBytes(kSampleRate, kDuration);
    std::vector<uint8_t> mem(bytes, 0);

    RecordingBuffer buf;
    size_t num_frames = static_cast<size_t>(kSampleRate * kDuration);
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);

    // Write a sine wave
    for (size_t i = 0; i < num_frames; ++i) {
        float phase = static_cast<float>(i) / static_cast<float>(num_frames) * 2.0f * 3.14159265f;
        buf.Write(std::sin(phase), std::cos(phase));
    }

    // At integer positions, Hermite should closely match the written sample
    // (not exact due to interpolation overshoot near discontinuities)
    for (size_t i = 10; i < 100; ++i) {
        float pos = static_cast<float>(i);
        float l = buf.ReadHermite(0, pos);
        float phase = static_cast<float>(i) / static_cast<float>(num_frames) * 2.0f * 3.14159265f;
        REQUIRE(l == Approx(std::sin(phase)).margin(0.01f));
    }
}

TEST_CASE("RecordingBuffer: Write wraps around", "[buffer]") {
    size_t num_frames = 100;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);

    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);

    // Write more than buffer size
    for (size_t i = 0; i < num_frames + 50; ++i) {
        buf.Write(static_cast<float>(i), 0.0f);
    }

    // Write head should have wrapped
    REQUIRE(buf.write_head() == 50);
}

TEST_CASE("RecordingBuffer: non-finite read position returns silence, not a hang",
          "[buffer]") {
    size_t num_frames = 100;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);

    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    for (size_t i = 0; i < num_frames; ++i) buf.Write(1.0f, 1.0f);

    float nan = std::numeric_limits<float>::quiet_NaN();
    float inf = std::numeric_limits<float>::infinity();

    for (float pos : {nan, inf, -inf}) {
        REQUIRE(buf.ReadLinear(0, pos) == 0.0f);
        REQUIRE(buf.ReadHermite(0, pos) == 0.0f);
        float out_l, out_r;
        buf.ReadHermiteStereo(pos, &out_l, &out_r);
        REQUIRE(out_l == 0.0f);
        REQUIRE(out_r == 0.0f);
    }
}

TEST_CASE("RecordingBuffer: huge finite read position wraps in O(1) via fmod",
          "[buffer]") {
    size_t num_frames = 100;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);

    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    for (size_t i = 0; i < num_frames; ++i) {
        float val = static_cast<float>(i) / static_cast<float>(num_frames);
        buf.Write(val, -val);
    }

    // A position many multiples of size_ past frame 5 must read the same as
    // frame 5 itself -- the old while-loop wrap would have taken ~100,000
    // iterations here; fmod does it in one step. (Kept within 2^24 so the
    // float representation of `huge` itself is exact -- larger values lose
    // enough mantissa precision that the wrapped fractional part isn't
    // meaningfully comparable to the small-position readback.)
    float huge = 5.0f + static_cast<float>(num_frames) * 100000.0f;
    REQUIRE(buf.ReadLinear(0, huge) == Approx(buf.ReadLinear(0, 5.0f)));
    REQUIRE(buf.ReadHermite(0, huge) == Approx(buf.ReadHermite(0, 5.0f)));
}

TEST_CASE("RecordingBuffer: entering freeze fades both sides of the seam to zero",
          "[buffer][freeze]") {
    size_t num_frames = 1000;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);

    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    float* data = reinterpret_cast<float*>(mem.data());

    // Fill the whole buffer with a sine (has zero crossings — the old code
    // skipped the fade entirely whenever it found one).
    for (size_t i = 0; i < num_frames; ++i) {
        float v = std::sin(2.0 * M_PI * i / 50.0) * 0.8f + 0.1f;
        buf.Write(v, v);
    }
    // Overwrite the seam region with DC so gains are directly observable.
    // write_head_ has wrapped to 0; write 500 frames of 1.0 → seam at 500.
    for (size_t i = 0; i < 500; ++i) buf.Write(1.0f, 1.0f);
    REQUIRE(buf.write_head() == 500);
    for (size_t i = 400; i < 600; ++i) { data[i * 2] = 1.0f; data[i * 2 + 1] = 1.0f; }

    buf.NotifyFreeze(true);

    // Newest side: frame 499 ≈ 0, ramp up moving away from the seam.
    REQUIRE(std::fabs(data[499 * 2]) < 1e-6f);
    REQUIRE(data[498 * 2] > 0.0f);
    REQUIRE(data[498 * 2] < data[490 * 2]);
    REQUIRE(data[490 * 2] < data[468 * 2]);
    // Oldest side: frame 500 ≈ 0, ramp up moving away.
    REQUIRE(std::fabs(data[500 * 2]) < 1e-6f);
    REQUIRE(data[501 * 2] > 0.0f);
    REQUIRE(data[501 * 2] < data[531 * 2]);
    // Frames >= kCrossfadeSamples away untouched.
    REQUIRE(data[467 * 2] == 1.0f);
    REQUIRE(data[532 * 2] == 1.0f);
}

TEST_CASE("RecordingBuffer: freeze fade near frame 0 keeps the tail mirror in sync",
          "[buffer][freeze]") {
    size_t num_frames = 1000;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);

    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    float* data = reinterpret_cast<float*>(mem.data());

    // Put the write head just past 0 so the fade wraps across the boundary.
    for (size_t i = 0; i < num_frames; ++i) buf.Write(1.0f, 1.0f);
    for (size_t i = 0; i < 2; ++i) buf.Write(1.0f, 1.0f);
    REQUIRE(buf.write_head() == 2);

    buf.NotifyFreeze(true);

    // Any faded frame < kInterpolationTail must be mirrored into the tail.
    for (int f = 0; f < kInterpolationTail; ++f) {
        REQUIRE(data[(num_frames + f) * 2] == data[f * 2]);
        REQUIRE(data[(num_frames + f) * 2 + 1] == data[f * 2 + 1]);
    }
}

TEST_CASE("RecordingBuffer: leaving freeze blends writes from old content to live input",
          "[buffer][freeze]") {
    size_t num_frames = 1000;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);

    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    float* data = reinterpret_cast<float*>(mem.data());

    for (size_t i = 0; i < 500; ++i) buf.Write(1.0f, 1.0f);
    size_t seam = buf.write_head();

    buf.NotifyFreeze(true);
    buf.NotifyFreeze(false);
    // Old content at the resume point is ~0 (the freeze fade zeroed it).
    for (size_t i = 0; i < 64; ++i) buf.Write(0.5f, 0.5f);

    // First write ≈ old content (near 0), ramping toward the incoming 0.5.
    REQUIRE(std::fabs(data[seam * 2]) < 0.05f);
    float prev = data[seam * 2];
    for (size_t i = 1; i < 32; ++i) {
        REQUIRE(data[(seam + i) * 2] >= prev - 1e-6f);
        prev = data[(seam + i) * 2];
    }
    // After the ramp, writes are stored verbatim.
    REQUIRE(data[(seam + 40) * 2] == 0.5f);
}

TEST_CASE("Interpolation: Hermite is exact for linear functions", "[interpolation]") {
    // For a linear function y = x, Hermite should interpolate exactly
    float y_1 = -1.0f, y0 = 0.0f, y1 = 1.0f, y2 = 2.0f;
    REQUIRE(InterpolateHermite(y_1, y0, y1, y2, 0.0f) == Approx(0.0f));
    REQUIRE(InterpolateHermite(y_1, y0, y1, y2, 0.25f) == Approx(0.25f));
    REQUIRE(InterpolateHermite(y_1, y0, y1, y2, 0.5f) == Approx(0.5f));
    REQUIRE(InterpolateHermite(y_1, y0, y1, y2, 0.75f) == Approx(0.75f));
}

TEST_CASE("Interpolation: Linear basic", "[interpolation]") {
    REQUIRE(InterpolateLinear(0.0f, 1.0f, 0.0f) == Approx(0.0f));
    REQUIRE(InterpolateLinear(0.0f, 1.0f, 0.5f) == Approx(0.5f));
    REQUIRE(InterpolateLinear(0.0f, 1.0f, 1.0f) == Approx(1.0f));
}

// ============================================================================
// Decimation tests
// ============================================================================

TEST_CASE("RecordingBuffer: Decimation write rate", "[buffer][decimation]") {
    size_t num_frames = 1000;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);

    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    buf.SetDecimationFactor(4);

    // Write 100 real-time samples — should advance write head by 25
    for (int i = 0; i < 100; ++i) {
        buf.Write(1.0f, 1.0f);
    }

    REQUIRE(buf.write_head() == 25);
}

TEST_CASE("RecordingBuffer: Decimation keeps last sample (sample-and-hold)", "[buffer][decimation]") {
    size_t num_frames = 1000;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);

    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    buf.SetDecimationFactor(4);

    // Write 4 samples: 0, 2, 4, 6 → sample-and-hold keeps the last = 6.0
    buf.Write(0.0f, 0.0f);
    buf.Write(2.0f, 2.0f);
    buf.Write(4.0f, 4.0f);
    buf.Write(6.0f, 6.0f);

    // Read back the one written frame (at position 0)
    float l = buf.ReadLinear(0, 0.0f);
    float r = buf.ReadLinear(1, 0.0f);
    REQUIRE(l == Approx(6.0f));
    REQUIRE(r == Approx(6.0f));
}

TEST_CASE("RecordingBuffer: Counter resets on factor change", "[buffer][decimation]") {
    size_t num_frames = 1000;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);

    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    buf.SetDecimationFactor(8);

    // Write 3 samples (partial counter)
    buf.Write(1.0f, 1.0f);
    buf.Write(1.0f, 1.0f);
    buf.Write(1.0f, 1.0f);
    REQUIRE(buf.write_head() == 0);  // Not yet committed

    // Change factor — should reset counter
    buf.SetDecimationFactor(2);

    // Now write 2 samples with new factor — should commit on the 2nd
    buf.Write(10.0f, 10.0f);
    buf.Write(20.0f, 20.0f);
    REQUIRE(buf.write_head() == 1);

    // Sample-and-hold: the committed value is the last sample (20.0)
    float l = buf.ReadLinear(0, 0.0f);
    REQUIRE(l == Approx(20.0f));
}

TEST_CASE("RecordingBuffer: Effective duration scales with decimation", "[buffer][decimation]") {
    // Physical buffer: 96000 frames (2s at 48kHz).
    // With 8x decimation, it takes 768000 real-time samples to fill = 16s.
    // Audio recorded 3 real-time seconds ago should still be readable.
    size_t num_frames = 96000;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);

    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    buf.SetDecimationFactor(8);

    // Write a marker value
    for (int i = 0; i < 8; ++i) buf.Write(0.5f, 0.5f);  // 8 samples → 1 frame

    // Write silence for 3 real-time seconds (144000 samples → 18000 buffer frames)
    for (int i = 0; i < 144000; ++i) buf.Write(0.0f, 0.0f);

    // The marker at frame 0 should still be in the buffer (only 18001 of 96000
    // frames written), proving the buffer represents >2s of real time.
    float marker = buf.ReadLinear(0, 0.0f);
    REQUIRE(marker == Approx(0.5f));
}

TEST_CASE("RecordingBuffer: Decimation factor=1 is passthrough", "[buffer][decimation]") {
    size_t num_frames = 100;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);

    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    buf.SetDecimationFactor(1);

    // Each write should advance the head by 1
    for (int i = 0; i < 10; ++i) {
        buf.Write(static_cast<float>(i), 0.0f);
    }
    REQUIRE(buf.write_head() == 10);

    // Values should be exact (no averaging)
    for (int i = 0; i < 10; ++i) {
        float l = buf.ReadLinear(0, static_cast<float>(i));
        REQUIRE(l == Approx(static_cast<float>(i)));
    }
}

// ---------------------------------------------------------------------------
// Clear mechanism tests
// ---------------------------------------------------------------------------

static constexpr size_t kClearTestFrames = 100;

// Helper: allocate a buffer of kClearTestFrames frames and fill it with
// non-zero content so clear tests can verify zeroing.
struct ClearTestBuf {
    std::vector<float> mem;
    RecordingBuffer buf;

    ClearTestBuf() : mem((kClearTestFrames + kInterpolationTail) * 2, 0.0f) {
        buf.Init(mem.data(), kClearTestFrames, 2);
        for (size_t i = 0; i < kClearTestFrames; ++i)
            buf.Write(1.0f, -1.0f);
    }
};

TEST_CASE("RecordingBuffer: ImmediateClear zeroes all content synchronously", "[buffer][clear]") {
    ClearTestBuf tb;

    // Buffer has non-zero content before clear
    REQUIRE(std::fabs(tb.buf.ReadLinear(0, 0.0f)) > 0.5f);
    REQUIRE(std::fabs(tb.buf.ReadLinear(1, 0.0f)) > 0.5f);

    tb.buf.ImmediateClear();

    // All readable positions are now zero
    for (int i = 0; i < (int)kClearTestFrames; ++i) {
        INFO("frame " << i);
        REQUIRE(tb.buf.ReadLinear(0, static_cast<float>(i)) == Approx(0.0f).margin(1e-6f));
        REQUIRE(tb.buf.ReadLinear(1, static_cast<float>(i)) == Approx(0.0f).margin(1e-6f));
    }
}

TEST_CASE("RecordingBuffer: ImmediateClear does not reset write_head", "[buffer][clear]") {
    ClearTestBuf tb;
    size_t head_before = tb.buf.write_head();  // = kClearTestFrames % kClearTestFrames = 0
    // Write a few more frames so write_head is non-zero
    tb.buf.Write(0.5f, 0.5f);
    tb.buf.Write(0.5f, 0.5f);
    size_t head_after_writes = tb.buf.write_head();
    REQUIRE(head_after_writes == 2);

    tb.buf.ImmediateClear();

    // write_head unchanged
    REQUIRE(tb.buf.write_head() == head_after_writes);
    (void)head_before;
}

TEST_CASE("RecordingBuffer: Clear resets write_head to zero", "[buffer][clear]") {
    ClearTestBuf tb;
    // After writing kClearTestFrames frames, write_head wraps to 0
    // Write a few more to make it non-zero
    tb.buf.Write(1.0f, 1.0f);
    tb.buf.Write(1.0f, 1.0f);
    REQUIRE(tb.buf.write_head() == 2);

    tb.buf.Clear();

    REQUIRE(tb.buf.write_head() == 0);
}

TEST_CASE("RecordingBuffer: Clear does not zero data immediately", "[buffer][clear]") {
    ClearTestBuf tb;

    tb.buf.Clear();

    // Data still readable (not zeroed until TickClear runs)
    REQUIRE(std::fabs(tb.buf.ReadLinear(0, 0.0f)) > 0.5f);
}

TEST_CASE("RecordingBuffer: TickClear zeroes data incrementally", "[buffer][clear]") {
    ClearTestBuf tb;
    tb.buf.Clear();

    // 2 floats = 1 stereo frame (L0, R0)
    tb.buf.TickClear(2);

    // Frame 0 is now zero
    REQUIRE(tb.buf.ReadLinear(0, 0.0f) == Approx(0.0f).margin(1e-6f));
    REQUIRE(tb.buf.ReadLinear(1, 0.0f) == Approx(0.0f).margin(1e-6f));

    // Frame 1 is still non-zero
    REQUIRE(std::fabs(tb.buf.ReadLinear(0, 1.0f)) > 0.5f);

    // Clear the rest: pass a large count; TickClear clamps to remaining
    tb.buf.TickClear((kClearTestFrames + kInterpolationTail) * 2 + 1000);

    for (int i = 0; i < (int)kClearTestFrames; ++i) {
        INFO("frame " << i);
        REQUIRE(tb.buf.ReadLinear(0, static_cast<float>(i)) == Approx(0.0f).margin(1e-6f));
    }
}

TEST_CASE("RecordingBuffer: TickClear is no-op when nothing pending", "[buffer][clear]") {
    ClearTestBuf tb;
    // No Clear() called — TickClear should do nothing, not crash
    tb.buf.TickClear(10000);
    // Buffer content was set by Init (zeroed) then written to 1.0; still 1.0
    REQUIRE(std::fabs(tb.buf.ReadLinear(0, 0.0f)) > 0.5f);
}

TEST_CASE("RecordingBuffer: ImmediateClear cancels pending deferred clear", "[buffer][clear]") {
    ClearTestBuf tb;

    // Start deferred clear
    tb.buf.Clear();
    // Immediately zero (cancels the deferred clear)
    tb.buf.ImmediateClear();

    // Buffer is zero
    REQUIRE(tb.buf.ReadLinear(0, 0.0f) == Approx(0.0f).margin(1e-6f));

    // Write a known value after ImmediateClear — if TickClear were not a no-op
    // it would zero this out. Writing proves the deferred clear is structurally cancelled.
    tb.buf.Write(0.75f, 0.75f);
    tb.buf.TickClear(10000);
    // If TickClear re-ran from cursor 0, it would zero the just-written value.
    // The value surviving proves TickClear is a true no-op.
    REQUIRE(std::fabs(tb.buf.ReadLinear(0, 0.0f)) > 0.5f);
}
