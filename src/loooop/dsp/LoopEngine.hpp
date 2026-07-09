#pragma once
#include <vector>
#include <cstddef>
#include <array>
#include <cstdint>
#include <atomic>

// Platform-agnostic loop recorder/player. One shared record buffer read by
// NUM_HEADS independent interpolating playheads. Inspired by Cutlasses Gloop.
class LoopEngine {
public:
    static constexpr int NUM_HEADS = 4;

    struct HeadOut { float l = 0.f, r = 0.f; };

    explicit LoopEngine(int numHeads = NUM_HEADS)
        : numHeads_(numHeads < 1 ? 1 : (numHeads > NUM_HEADS ? NUM_HEADS : numHeads)) {}
    int numHeads() const { return numHeads_; }

    void reset(float sampleRate, float maxSeconds = 60.f);
    // Retune to a new sample rate WITHOUT destroying a recorded loop: the
    // loop plays back repitched. Only reallocates (full reset) when there is
    // nothing to lose (no loop, not recording) — never audio-adjacent with
    // content in the buffer (see the clear() comment for why that matters).
    void setSampleRate(float sampleRate);
    void process(float inL, float inR, std::array<HeadOut, NUM_HEADS>& heads);
    float process(float in);      // mono convenience: inL=inR=in, returns summed head L

    void toggleRecord();          // momentary toggle: record <-> stop/overdub
    void clear();                 // erase loop
    void setOverdub(bool on) { overdubEnabled_ = on; }   // gate post-loop record toggles
    void setCrossfade(bool on) { crossfade_ = on; }      // declick each head's loop seam

    void setSpeed(int head, float x);        // 1=normal, <0=reverse
    void setPosition(int head, float c01);   // sub-loop centre, 0..1
    void setSize(int head, float s01);       // sub-loop size, 0..1
    void setLevel(int head, float g);        // output gain, 0..1
    void setJitter(int head, float j01);     // window wander amount, 0..1

    // Trigger-jack operations (hosts translate jack edges into these calls)
    void restartHead(int head);           // snap to window start (direction-aware)
    void setOneShot(int head, bool on);   // on: stop + arm (silent); off: resume looping
    void triggerOneShot(int head);        // (re)start a one-shot pass
    void jumpHead(int head, float t01);   // pos = winStart + t01 * (winLen - 1)

    // introspection (tests)
    std::size_t loopLength() const { return loopLen_; }
    bool isRecording() const { return recording_; }
    bool hasLoop() const { return loopLen_ > 0; }

    // Waveform display peaks: per-channel min/max per bin over the record
    // buffer (ch 0 = L, 1 = R), maintained incrementally on the audio thread.
    static constexpr int PEAK_BINS = 4096;
    const std::array<float, PEAK_BINS>& peakMins(int ch = 0) const { return ch ? peakMinR_ : peakMinL_; }
    const std::array<float, PEAK_BINS>& peakMaxs(int ch = 0) const { return ch ? peakMaxR_ : peakMaxL_; }
    std::uint32_t peakBinSize() const { return peakBinSize_; }

    // GUI-thread introspection for the display. All fields cross the
    // audio->GUI boundary via single-word atomics; the double playhead pos is
    // mirrored into an atomic<float> because a raw 64-bit read can tear on
    // 32-bit ARM. Peaks (above) and peakBinSize() are plain (non-atomic): the
    // audio thread writes them, the GUI reads them unlatched. peakBinSize_ only
    // changes in reset(), and an aligned 32-bit read can't tear; the renderer
    // clamps any resulting stale bin index. A torn peak-bin read is at worst a
    // one-frame, one-pixel artifact and is accepted.
    struct DisplaySnapshot {
        std::uint32_t loopLen;      // frozen loop length, samples; 0 = no loop yet
        std::uint32_t recordedLen;  // samples written so far in the initial record pass
        bool recording;
        std::array<float, NUM_HEADS> headPos01;              // per-head position, 0..1
        std::array<float, NUM_HEADS> winStart01, winEnd01;   // per-head window, 0..1
    };
    DisplaySnapshot displaySnapshot() const;
    std::uint32_t waveformRevision() const {
        return waveformRevision_.load(std::memory_order_acquire);
    }

private:
    static constexpr double MINIMUM_LOOP_MILLISECONDS = 1.0;

    struct PlayHead {
        double pos = 0.0;
        float speed = 1.f, level = 1.f, centre = 0.5f, size = 1.f;
        bool oneShot = false, playing = true;
        float jitter = 0.f, jitterOff = 0.f, jitterNext = 0.f;
    };

    void windowBounds(const PlayHead& h, double& winStart, double& winLen) const;
    void windowBounds(const PlayHead& h, float jitterOff,
                      double& winStart, double& winLen) const;
    float readInterpolated(const PlayHead& h, const std::vector<float>& buf,
                           double winStart, double winLen) const;
    float readRaw(double p, const std::vector<float>& buf) const;
    void readHead(const PlayHead& h, double winStart, double winLen,
                  float& outL, float& outR) const;
    // Crossfade length in output samples for this head/window, capped to half the
    // window's output-period; 0 disables (window too short, or crossfade off).
    int fadeLen(const PlayHead& h, double winLen) const;
    void rollJitter(PlayHead& h);
    void commitJitter(PlayHead& h);
    void advanceHead(PlayHead& h, int idx, double winStart, double winLen);
    void writePeak(std::size_t idx, float l, float r);
    // Waveform-cache invalidation: bumped (release) after any change to the
    // peak arrays so display hosts re-render the static waveform only when
    // recorded audio actually changed. Only the audio thread writes; the
    // counter is kept non-atomic and published with a single atomic store.
    void bumpWaveformRevision() {
        waveformRevision_.store(++waveformRevisionCounter_, std::memory_order_release);
    }

    int numHeads_ = NUM_HEADS;
    std::vector<float> bufL_, bufR_;
    std::size_t maxSamples_ = 0;
    std::size_t writeIdx_ = 0;
    std::size_t loopLen_ = 0;
    bool recording_ = false;
    bool overdubEnabled_ = true;
    bool crossfade_ = true;
    std::uint32_t xfadeSamples_ = 0;   // ~5 ms at the current sample rate; set in reset()
    double minWinLen_ = 48.0;   // ceil(sampleRate · 1 ms); set in reset()/setSampleRate()
    float sampleRate_ = 48000.f;
    float maxSeconds_ = 60.f;
    std::uint32_t rng_ = 0x9E3779B9u;
    PlayHead heads_[NUM_HEADS];

    std::array<float, PEAK_BINS> peakMinL_{}, peakMaxL_{}, peakMinR_{}, peakMaxR_{};
    std::uint32_t peakBinSize_ = 1;
    std::uint32_t lastPeakBin_ = UINT32_MAX;   // forces a bin reset on first write

    // display mirrors (audio thread stores, GUI thread loads)
    std::atomic<std::uint32_t> dispLoopLen_{0};
    std::atomic<std::uint32_t> dispRecLen_{0};
    std::atomic<bool> dispRecording_{false};
    std::atomic<std::uint32_t> waveformRevision_{0};
    std::uint32_t waveformRevisionCounter_ = 0;
    std::array<std::atomic<float>, NUM_HEADS> dispPos01_{};
    std::array<std::atomic<float>, NUM_HEADS> dispWinStart01_{};
    std::array<std::atomic<float>, NUM_HEADS> dispWinEnd01_{};
};
