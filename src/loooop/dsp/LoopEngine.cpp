#include "LoopEngine.hpp"
#include <algorithm>
#include <cmath>

static inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

void LoopEngine::reset(float sampleRate, float maxSeconds) {
    sampleRate_ = sampleRate;
    // ~5 ms declick crossfade. Rounds to 0 at very low sample rates (e.g. the
    // 10 Hz test rate), which disables the crossfade and preserves seam-exact
    // behavior there.
    xfadeSamples_ = static_cast<std::uint32_t>(0.005f * sampleRate + 0.5f);
    maxSamples_ = static_cast<std::size_t>(sampleRate * maxSeconds);
    bufL_.assign(maxSamples_, 0.f); bufR_.assign(maxSamples_, 0.f);   // pre-allocate once; never resized in audio
    peakBinSize_ = static_cast<std::uint32_t>((maxSamples_ + PEAK_BINS - 1) / PEAK_BINS);
    if (peakBinSize_ == 0) peakBinSize_ = 1;
    peakMinL_.fill(0.f); peakMaxL_.fill(0.f);
    peakMinR_.fill(0.f); peakMaxR_.fill(0.f);
    lastPeakBin_ = UINT32_MAX;
    writeIdx_ = 0;
    loopLen_ = 0;
    recording_ = false;
    rng_ = 0x9E3779B9u;
    for (auto& h : heads_) h = PlayHead{};
    dispLoopLen_.store(0, std::memory_order_relaxed);
    dispRecLen_.store(0, std::memory_order_relaxed);
    dispRecording_.store(false, std::memory_order_relaxed);
    for (auto& a : dispPos01_)      a.store(0.f, std::memory_order_relaxed);
    for (auto& a : dispWinStart01_) a.store(0.f, std::memory_order_relaxed);
    for (auto& a : dispWinEnd01_)   a.store(1.f, std::memory_order_relaxed);
}

void LoopEngine::toggleRecord() {
    // Overdub gate: with a loop already frozen, a new record pass is an
    // overdub — ignore the toggle when overdub is disabled. Stopping an
    // in-progress recording and the initial record pass are never gated.
    if (!recording_ && loopLen_ > 0 && !overdubEnabled_) return;
    if (!recording_) {
        recording_ = true;
        writeIdx_ = 0;                  // record/overdub always starts at loop start (v1)
        lastPeakBin_ = UINT32_MAX;          // first write re-seeds its bin
        dispRecording_.store(true, std::memory_order_relaxed);
        dispRecLen_.store(0, std::memory_order_relaxed);
    } else {
        recording_ = false;
        if (loopLen_ == 0) loopLen_ = writeIdx_;   // freeze initial loop length
        dispRecording_.store(false, std::memory_order_relaxed);
        dispLoopLen_.store(static_cast<std::uint32_t>(loopLen_), std::memory_order_relaxed);
    }
}

void LoopEngine::clear() {
    // No need to zero bufL_/bufR_: with loopLen_ == 0 the buffer is never read
    // before it's overwritten (initial record pass overwrites each sample; the
    // overdub-sum path only runs once loopLen_ > 0). Zeroing the full ~60 s
    // buffer here is a ~23 MB memset that blew the per-sample audio deadline on
    // MetaModule and crashed the patch.
    loopLen_ = 0;
    writeIdx_ = 0;
    recording_ = false;
    for (auto& h : heads_) { h.pos = 0.0; h.playing = !h.oneShot; }   // re-arm one-shots
    peakMinL_.fill(0.f); peakMaxL_.fill(0.f);
    peakMinR_.fill(0.f); peakMaxR_.fill(0.f);
    lastPeakBin_ = UINT32_MAX;
    dispLoopLen_.store(0, std::memory_order_relaxed);
    dispRecLen_.store(0, std::memory_order_relaxed);
    dispRecording_.store(false, std::memory_order_relaxed);
    for (auto& a : dispPos01_)      a.store(0.f, std::memory_order_relaxed);
    for (auto& a : dispWinStart01_) a.store(0.f, std::memory_order_relaxed);
    for (auto& a : dispWinEnd01_)   a.store(1.f, std::memory_order_relaxed);
}

void LoopEngine::setSpeed(int head, float x)     { if (head >= 0 && head < numHeads_) heads_[head].speed = x; }
void LoopEngine::setPosition(int head, float c01){ if (head >= 0 && head < numHeads_) heads_[head].centre = clamp01(c01); }
void LoopEngine::setSize(int head, float s01)    { if (head >= 0 && head < numHeads_) heads_[head].size = clamp01(s01); }
void LoopEngine::setLevel(int head, float g)     { if (head >= 0 && head < numHeads_) heads_[head].level = clamp01(g); }

void LoopEngine::setJitter(int head, float j01) {
    if (head < 0 || head >= numHeads_) return;
    PlayHead& h = heads_[head];
    float j = clamp01(j01);
    if (j > 0.f && h.jitter == 0.f) {
        h.jitter = j;
        rollJitter(h);          // first wrap after enabling is already random
    } else {
        h.jitter = j;
        if (j == 0.f) h.jitterNext = 0.f;
    }
}

// xorshift32: deterministic (seeded in reset), audio-thread safe. Offset up to
// +/- half the loop at jitter 1; jitter 0 always yields exactly 0.
// Rolls the NEXT window's offset; commitJitter() makes it current at the
// wrap. The seam crossfade previews the next window during the fade, so the
// offset must be decided before the fade begins, not at the wrap itself.
void LoopEngine::rollJitter(PlayHead& h) {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    h.jitterNext = h.jitter * ((rng_ >> 8) * (1.f / 16777216.f) - 0.5f);
}

void LoopEngine::commitJitter(PlayHead& h) {
    h.jitterOff = h.jitterNext;
    rollJitter(h);              // pre-roll for the following wrap
}

void LoopEngine::restartHead(int head) {
    if (head < 0 || head >= numHeads_ || loopLen_ == 0) return;
    PlayHead& h = heads_[head];
    rollJitter(h);       // fresh window now…
    commitJitter(h);     // …made current, with the next one pre-rolled
    double winStart, winLen;
    windowBounds(h, winStart, winLen);
    h.pos = h.speed < 0.f ? winStart + winLen - 1.0 : winStart;
}

void LoopEngine::setOneShot(int head, bool on) {
    if (head < 0 || head >= numHeads_) return;
    PlayHead& h = heads_[head];
    if (on == h.oneShot) return;   // hosts call this every sample; act on change only
    h.oneShot = on;
    h.playing = !on;               // entering: arm (silent); leaving: resume looping
}

void LoopEngine::triggerOneShot(int head) {
    if (head < 0 || head >= numHeads_ || loopLen_ == 0) return;
    PlayHead& h = heads_[head];
    if (!h.oneShot) return;
    restartHead(head);
    h.playing = true;
}

void LoopEngine::jumpHead(int head, float t01) {
    if (head < 0 || head >= numHeads_ || loopLen_ == 0) return;
    PlayHead& h = heads_[head];
    double winStart, winLen;
    windowBounds(h, winStart, winLen);
    h.pos = winStart + static_cast<double>(clamp01(t01)) * (winLen - 1.0);
}

void LoopEngine::windowBounds(const PlayHead& h, double& winStart, double& winLen) const {
    windowBounds(h, h.jitterOff, winStart, winLen);
}

void LoopEngine::windowBounds(const PlayHead& h, float jitterOff,
                              double& winStart, double& winLen) const {
    const double L = static_cast<double>(loopLen_);
    const double minWinLen = std::ceil(
        static_cast<double>(sampleRate_) * MINIMUM_LOOP_MILLISECONDS / 1000.0);
    winLen = static_cast<double>(h.size) * L;
    if (winLen < minWinLen) winLen = minWinLen;
    if (winLen > L)   winLen = L;
    double centre = static_cast<double>(clamp01(h.centre + jitterOff)) * L;
    winStart = centre - winLen / 2.0;
    if (winStart < 0.0) winStart = 0.0;
    if (winStart + winLen > L) winStart = L - winLen;
    if (winStart < 0.0) winStart = 0.0;
}

float LoopEngine::readInterpolated(const PlayHead& h, const std::vector<float>& buf) const {
    double winStart, winLen;
    windowBounds(h, winStart, winLen);
    double p = h.pos;
    if (p < winStart || p >= winStart + winLen) p = winStart;   // honor a just-moved window on this read (advanceHead snaps h.pos after)
    std::size_t i0 = static_cast<std::size_t>(std::floor(p));
    const double frac = p - std::floor(p);
    std::size_t i1 = i0 + 1;
    if (static_cast<double>(i1) >= winStart + winLen) i1 = static_cast<std::size_t>(winStart);
    if (i0 >= loopLen_) i0 = loopLen_ ? loopLen_ - 1 : 0;
    if (i1 >= loopLen_) i1 = loopLen_ ? loopLen_ - 1 : 0;
    return static_cast<float>((1.0 - frac) * buf[i0] + frac * buf[i1]);
}

// Raw interpolated buffer read, clamped to [0, loopLen). Used by the seam
// crossfade to read the loop head ahead of the primary (window-clamped) reader.
float LoopEngine::readRaw(double p, const std::vector<float>& buf) const {
    if (loopLen_ == 0) return 0.f;
    if (p < 0.0) p = 0.0;
    const double maxp = static_cast<double>(loopLen_ - 1);
    if (p > maxp) p = maxp;
    std::size_t i0 = static_cast<std::size_t>(std::floor(p));
    const double frac = p - std::floor(p);
    std::size_t i1 = i0 + 1;
    if (i1 >= loopLen_) i1 = loopLen_ - 1;
    return static_cast<float>((1.0 - frac) * buf[i0] + frac * buf[i1]);
}

int LoopEngine::fadeLen(const PlayHead& h, double winLen) const {
    if (!crossfade_ || xfadeSamples_ == 0 || h.oneShot) return 0;
    const double sp = std::fabs(static_cast<double>(h.speed));
    if (sp < 1e-9) return 0;                        // a stationary head never wraps
    const int cap = static_cast<int>((winLen / sp) * 0.5);   // fade <= half a pass
    int F = static_cast<int>(xfadeSamples_);
    if (F > cap) F = cap;
    return F < 1 ? 0 : F;
}

// Interpolated read for one head with the seam crossfade applied. The primary
// (window-clamped) reader supplies the tail; within the last F output-samples
// before the loop point it is equal-power-crossfaded with the loop head read
// ahead from the window start (direction-aware). advanceHead() resumes just past
// that previewed head region so nothing is double-played.
void LoopEngine::readHead(const PlayHead& h, float& outL, float& outR) const {
    outL = readInterpolated(h, bufL_);
    outR = readInterpolated(h, bufR_);
    double winStart, winLen;
    windowBounds(h, winStart, winLen);
    const int F = fadeLen(h, winLen);
    if (F < 1) return;
    const double sp = std::fabs(static_cast<double>(h.speed));
    const double outToSeam = (h.speed >= 0.f)
        ? (winStart + winLen - h.pos) / sp
        : (h.pos - winStart) / sp;
    if (outToSeam < 0.0 || outToSeam >= static_cast<double>(F)) return;
    double prog = outToSeam / static_cast<double>(F);   // 1 at fade start -> 0 at seam
    if (prog < 0.0) prog = 0.0;
    if (prog > 1.0) prog = 1.0;
    const double headAdvance = (static_cast<double>(F) - outToSeam) * sp;
    // Preview from the NEXT window (jitterNext) — that is where advanceHead()
    // will resume at the wrap.
    double ns, nl;
    windowBounds(h, h.jitterNext, ns, nl);
    const double headPos = (h.speed >= 0.f)
        ? ns + headAdvance
        : ns + nl - 1.0 - headAdvance;
    // Smoothstep (Hermite) crossfade: zero slope at both fade boundaries, so the
    // gains don't step at the edges. An equal-power (sqrt) curve has an infinite
    // derivative at the seam and leaves an audible ~0.05 residual click; this
    // trades that for a sub-millisecond mid-fade dip that is inaudible.
    const float t = 1.f - static_cast<float>(prog);   // 0 at fade start -> 1 at seam
    const float gi = t * t * (3.f - 2.f * t);         // incoming head, smoothstep
    const float go = 1.f - gi;                        // outgoing tail
    outL = go * outL + gi * readRaw(headPos, bufL_);
    outR = go * outR + gi * readRaw(headPos, bufR_);
}

void LoopEngine::advanceHead(PlayHead& h, int idx) {
    double winStart, winLen;
    windowBounds(h, winStart, winLen);
    const double winEnd = winStart + winLen;
    if (h.pos < winStart || h.pos >= winEnd) h.pos = winStart;   // snap in if params moved
    h.pos += h.speed;
    if (h.pos >= winEnd) {                          // crossed forward
        if (h.oneShot) { h.playing = false; h.pos = winEnd - 1.0; }
        else {
            const int F = fadeLen(h, winLen);
            const double overshoot = h.pos - winEnd;
            commitJitter(h);
            if (F < 1) {
                h.pos -= winLen;                    // no crossfade: exact wrap
            } else {                                // resume past the previewed head
                double ns, nl; windowBounds(h, ns, nl);
                h.pos = ns + static_cast<double>(F) * std::fabs(h.speed) + overshoot;
                if (h.pos >= ns + nl) h.pos = ns;
            }
        }
    } else if (h.pos < winStart) {                  // crossed reverse
        if (h.oneShot) { h.playing = false; h.pos = winStart; }
        else {
            const int F = fadeLen(h, winLen);
            const double overshoot = winStart - h.pos;
            commitJitter(h);
            if (F < 1) {
                h.pos += winLen;                    // no crossfade: exact wrap
            } else {
                double ns, nl; windowBounds(h, ns, nl);
                h.pos = ns + nl - 1.0 - static_cast<double>(F) * std::fabs(h.speed) - overshoot;
                if (h.pos < ns) h.pos = ns + nl - 1.0;
            }
        }
    }

    const float invL = 1.f / static_cast<float>(loopLen_);   // loopLen_ > 0 whenever heads run
    dispPos01_[idx].store(static_cast<float>(h.pos) * invL, std::memory_order_relaxed);
    dispWinStart01_[idx].store(static_cast<float>(winStart) * invL, std::memory_order_relaxed);
    dispWinEnd01_[idx].store(static_cast<float>(winStart + winLen) * invL, std::memory_order_relaxed);
}

void LoopEngine::process(float inL, float inR, std::array<HeadOut, NUM_HEADS>& heads) {
    if (recording_) {
        if (loopLen_ == 0) {                 // initial pass: overwrite
            bufL_[writeIdx_] = inL;
            bufR_[writeIdx_] = inR;
            writePeak(writeIdx_, inL, inR);
            ++writeIdx_;
            dispRecLen_.store(static_cast<std::uint32_t>(writeIdx_), std::memory_order_relaxed);
            if (writeIdx_ >= maxSamples_) {  // buffer ceiling -> auto-end
                loopLen_ = maxSamples_;
                recording_ = false;
                writeIdx_ = 0;
                dispRecording_.store(false, std::memory_order_relaxed);
                dispLoopLen_.store(static_cast<std::uint32_t>(loopLen_), std::memory_order_relaxed);
            }
        } else {                             // overdub: sum, wrap at loop length
            bufL_[writeIdx_] += inL;
            bufR_[writeIdx_] += inR;
            writePeak(writeIdx_, bufL_[writeIdx_], bufR_[writeIdx_]);
            ++writeIdx_;
            if (writeIdx_ >= loopLen_) writeIdx_ = 0;
        }
    }
    for (auto& o : heads) o = HeadOut{};
    if (loopLen_ > 0) {
        for (int i = 0; i < numHeads_; ++i) {
            PlayHead& h = heads_[i];
            if (!h.playing) continue;
            float l, r; readHead(h, l, r);
            heads[i].l = l * h.level;
            heads[i].r = r * h.level;
            advanceHead(h, i);
        }
    }
}

float LoopEngine::process(float in) {
    std::array<HeadOut, NUM_HEADS> hs;
    process(in, in, hs);
    float out = 0.f;
    for (const auto& o : hs) out += o.l;
    return out;
}

// One compare-and-update per channel per written sample. Entering a bin
// re-seeds it so overdubbed audio replaces the old extent rather than only
// widening it.
void LoopEngine::writePeak(std::size_t idx, float l, float r) {
    std::uint32_t bin = static_cast<std::uint32_t>(idx / peakBinSize_);
    if (bin >= PEAK_BINS) bin = PEAK_BINS - 1;
    if (bin != lastPeakBin_) {
        peakMinL_[bin] = l; peakMaxL_[bin] = l;
        peakMinR_[bin] = r; peakMaxR_[bin] = r;
        lastPeakBin_ = bin;
    } else {
        if (l < peakMinL_[bin]) peakMinL_[bin] = l;
        if (l > peakMaxL_[bin]) peakMaxL_[bin] = l;
        if (r < peakMinR_[bin]) peakMinR_[bin] = r;
        if (r > peakMaxR_[bin]) peakMaxR_[bin] = r;
    }
}

LoopEngine::DisplaySnapshot LoopEngine::displaySnapshot() const {
    DisplaySnapshot s;
    s.loopLen = dispLoopLen_.load(std::memory_order_relaxed);
    s.recordedLen = dispRecLen_.load(std::memory_order_relaxed);
    s.recording = dispRecording_.load(std::memory_order_relaxed);
    for (int i = 0; i < NUM_HEADS; ++i) {
        s.headPos01[i] = dispPos01_[i].load(std::memory_order_relaxed);
        s.winStart01[i] = dispWinStart01_[i].load(std::memory_order_relaxed);
        s.winEnd01[i] = dispWinEnd01_[i].load(std::memory_order_relaxed);
    }
    return s;
}
