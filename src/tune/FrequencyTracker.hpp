////////////////////////////////////////////////////////////
//
//   Pitch tracking and scope trigger from the CV Funk Tuner
//
//   written by Cody Geary
//   Copyright 2025, MIT License
//
//   From CV Funk's Tuner.cpp; changes are marked "RobotBoy:". The module
//   and its widget are in src/Tune.cpp. See NOTICE.md.
//
////////////////////////////////////////////////////////////

#pragma once
// RobotBoy: tests/tune builds this without Rack and supplies its own float_4
#ifndef TUNE_TRACKER_NO_RACK
#include "plugin.hpp"
#endif
#include <cstring>
#include <cmath>
#include <string>

using namespace rack;
using float_4 = rack::simd::float_4;

template<typename T, size_t Size>
class CircularBuffer {
private:
    T buffer[Size];
public:
    T& operator[](size_t i) { return buffer[i % Size]; }
    const T& operator[](size_t i) const { return buffer[i % Size]; }
    static constexpr size_t size() { return Size; }
};

#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>


// ---------------------------------------------------------------------------
// DecimatedFrequencyTracker
//
// Decimates the input signal before running normalised autocorrelation.
// Simple averaging decimation is sufficient -- the mild HF rolloff does not
// affect fundamental detection.
//
// AC computation is spread evenly across every full-rate audio sample by a
// budgeted state machine.  Each sample performs at most workBudget units of
// work (1 unit = one float_4 correlation iteration), crossing state
// boundaries as needed, so the per-sample cost is flat instead of bursty.
// The mean/DC-removal passes and the peak/candidate scans are chunked under
// the same budget -- no state does a full-buffer pass in a single sample.
//
// Smoothing only runs on confident results, preventing stale or default
// values from leaking into the display when no signal is present.
// ---------------------------------------------------------------------------
struct DecimatedFrequencyTracker {

    // Buffer length is set per-tracker in setDecimation() based on the
    // minimum frequency to detect:
    //   bufferSize = 2 * effectiveSampleRate / minFrequency
    // Audio tracker (8x/48kHz, min ~16Hz):  bufferSize = 2*6000/16 = 750
    // LFO tracker (128x/48kHz, min ~0.5Hz): bufferSize = 2*375/0.5 = 1500
    // Smaller buffer = shorter inner loop = dramatically less CPU.
    static constexpr int MAX_BUFFER_SIZE = 1500;
    int bufferSize = MAX_BUFFER_SIZE;

    float writeBuffer[MAX_BUFFER_SIZE]   = {};
    float processBuffer[MAX_BUFFER_SIZE] = {};
    int   writeIndex = 0;

    float decimAccum = 0.f;
    int   decimCount = 0;
    int   decimationFactor = 8;

    // Incremental AC engine.
    // States: 0=idle, 1=mean, 2=dc-remove, 3=correlate, 4=peak-scan, 5=candidate+finalize
    std::vector<float> ac;
    std::vector<float> win;
    std::vector<float> dcRemovedSignal;
    int   acState = 0;
    int   acLag   = 1;
    int   acHalf  = 0;
    float acMean  = 0.f;
    bool  bufferReady = false;

    int     chunkPos = 0;            // element/lag position within the current state
    float_4 partNum  = float_4(0.f); // partial correlation sums carried across calls
    float_4 partSq0  = float_4(0.f);
    float_4 partSqL  = float_4(0.f);
    float   acGlobalPeak    = -2.f;
    int     acGlobalPeakLag = 1;
    float   acThreshold     = 0.f;
    int     acCandidateLag  = 0;
    // RobotBoy: the first lag where the autocorrelation has fallen below
    // acThreshold, i.e. left the lobe around lag 0 (0 until it has).
    int     acDipLag        = 0;

    // lagsPerCall scales the per-sample work budget; 8 is the baseline where
    // a full analysis completes in about half the buffer refill time.
    int lagsPerCall = 8;

    // workBudget: units of analysis work permitted per full-rate sample.
    // 1 unit = one float_4 correlation iteration = 4 element operations.
    // baseWorkBudget is derived from buffer geometry in setDecimation().
    int baseWorkBudget = 32;
    int workBudget     = 32;

    // analysisSkip: skip this many buffer fills between analyses.
    // 0 = analyse every fill (highest CPU, fastest lock).
    // N = analyse every (N+1)th fill -- directly scales CPU down by N+1.
    // The inner loop cost is unchanged; we simply run it less often.
    int analysisSkip = 0;
    int analysisSkipCounter = 0;

    float decimatedSampleRate = 6000.f;
    int   simdLen      = 0;   // (acHalf / 4) * 4, cached to avoid recomputing per lag
    float lastFreq     = -1.f;
    float smoothedFreq = -1.f;
    float acPeakValue  = 0.f;
    float confidenceFloor = 0.25f;
    float smoothFactor = 0.9995f;

    DecimatedFrequencyTracker() {
        ac.resize(MAX_BUFFER_SIZE / 2, 0.f);
        win.resize(MAX_BUFFER_SIZE / 2, 0.f);
        dcRemovedSignal.resize(MAX_BUFFER_SIZE, 0.f);
        rebuildWindow();
    }

    void setDecimation(int factor, float fullSampleRate, float minFrequency = 16.f) {
        decimationFactor    = std::max(1, factor);
        decimatedSampleRate = fullSampleRate / decimationFactor;
        // Buffer just large enough to detect minFrequency.
        // 2 * effectiveRate / minFreq gives enough lags for 2 full periods.
        bufferSize = std::min(MAX_BUFFER_SIZE,
                     std::max(64, (int)(2.f * decimatedSampleRate / minFrequency)));
        rebuildWindow();
        // ~50ms smoothing time constant regardless of decimation rate
        smoothFactor = expf(-1.f / (0.05f * decimatedSampleRate));

        // Per-sample analysis budget.  Total correlation work is roughly
        // (bufferSize/2) lags * (simdLen/4) float_4 iterations each.  Spread
        // it so a full analysis completes in about half the buffer refill
        // time (bufferSize * decimationFactor full-rate samples).  The 0.5f
        // completion fraction can be hand-tuned: smaller spreads the work
        // thinner (lower peak CPU, slower lock).  The floor of 8 keeps
        // heavily decimated trackers (LFO range) locking promptly even
        // though their refill time would allow a much smaller budget.
        float totalWork   = (float)(bufferSize / 2) * (simdLen / 4);
        float fillSamples = (float)bufferSize * decimationFactor;
        baseWorkBudget = std::max(8, (int)ceilf(totalWork / (0.5f * fillSamples)));
        workBudget     = std::max(1, baseWorkBudget * lagsPerCall / 8);

        // Abort any in-flight analysis -- the buffer geometry just changed.
        acState     = 0;
        bufferReady = false;
        chunkPos    = 0;
        writeIndex  = 0;
        decimAccum  = 0.f;
        decimCount  = 0;
        analysisSkipCounter = 0;
    }

    void setConfidenceFloor(float floor) { confidenceFloor = floor; }
    void setLagsPerCall(int lags) {
        lagsPerCall = std::max(1, lags);
        workBudget  = std::max(1, baseWorkBudget * lagsPerCall / 8);
    }
    void setAnalysisSkip(int skip)       { analysisSkip = std::max(0, skip); }

    void rebuildWindow() {
        int half = bufferSize / 2;
        simdLen = (half / 4) * 4;
        win.resize(half);
        ac.resize(half, 0.f);
        dcRemovedSignal.resize(bufferSize, 0.f);
        for (int i = 0; i < half; ++i)
            win[i] = 0.5f * (1.f - cosf(2.f * (float)M_PI * i / (half - 1)));
    }

    // Returns smoothed Hz or -1 if no confident lock.
    float process(float in) {
        decimAccum += in;
        if (++decimCount >= decimationFactor) {
            writeBuffer[writeIndex++] = decimAccum / decimationFactor;
            decimAccum = 0.f;
            decimCount = 0;
            if (writeIndex >= bufferSize) {
                writeIndex = 0;
                if (acState == 0) {
                    // Skip N buffer fills between analyses to reduce CPU.
                    // Each skipped fill costs nothing -- we just keep writing
                    // into the write buffer and discard it.
                    if (++analysisSkipCounter > analysisSkip) {
                        analysisSkipCounter = 0;
                        std::memcpy(processBuffer, writeBuffer, bufferSize * sizeof(float));
                        bufferReady = true;
                        acHalf      = bufferSize / 2;
                        acMean      = 0.f;
                        chunkPos    = 0;
                        acState     = 1;
                    }
                }
            }
        }

        // Run a small, fixed slice of analysis work on every full-rate sample.
        // Total work per analysis is unchanged; spreading it over every sample
        // (instead of bursting on decimated ticks) flattens the CPU profile.
        if (bufferReady) processChunk();

        // Only smooth when a new decimated sample was just produced --
        // running this every full-rate sample wastes CPU since the value
        // only changes once per decimationFactor samples.
        if (decimCount == 0) {
            if (lastFreq > 0.f) {
                if (smoothedFreq < 0.f)
                    smoothedFreq = lastFreq;  // cold start: snap rather than ramp
                else
                    smoothedFreq = smoothFactor * smoothedFreq + (1.f - smoothFactor) * lastFreq;
            } else {
                smoothedFreq = -1.f;
            }
        }

        return smoothedFreq;
    }

    // Apply a finished analysis result and return to idle.
    // result: >0 confident Hz, -1 noise floor, -2 weak (hold previous)
    void finishAnalysis(float result) {
        if      (result > 0.f)   lastFreq = result;
        else if (result == -1.f) lastFreq = -1.f;
        // result == -2: weak signal, hold lastFreq unchanged
        bufferReady = false;
        acState     = 0;
        chunkPos    = 0;
    }

    // RobotBoy: refine a period (in lags) found from one autocorrelation peak.
    // Parabolic interpolation places a peak to within a fraction of a lag
    // however long the period is, so a short period comes out several cents
    // off (3.4 lags at 3.5 kHz: 27 cents), and a bright waveform's narrow
    // peaks fit a parabola worse still.  A periodic signal also peaks at
    // every multiple of its period, and the peak at k periods, placed just as
    // precisely, gives the period k times as precisely.  So follow the peaks
    // outwards, doubling k while the estimate is good enough to predict the
    // next one to within a lag, and finish on the last multiple that fits.
    // Stops at the first multiple with no clear peak (a pitch that moved
    // within the frame) and keeps the estimate it has.
    float refinePeriod(float period, float basePeak) const {
        int k = 1;
        for (;;) {
            int kMax = (int)((float)(acHalf - 3) / period);
            int k2   = std::min(k * 2, kMax);
            if (k2 <= k) break;
            int lag = (int)lroundf(period * k2);
            if (lag < 2 || lag >= acHalf - 2) break;
            // The highest of the predicted lag and its neighbours
            if (ac[lag - 1] > ac[lag] && ac[lag - 1] >= ac[lag + 1]) lag--;
            else if (ac[lag + 1] > ac[lag])                          lag++;
            if (lag < 2 || lag >= acHalf - 2) break;
            float y0 = ac[lag - 1];
            float y1 = ac[lag];
            float y2 = ac[lag + 1];
            if (y1 < y0 || y1 < y2 || y1 < 0.5f * basePeak) break;
            float denom = y0 - 2.f * y1 + y2;
            float shift = (fabsf(denom) > 1e-12f) ? (0.5f * (y0 - y2) / denom) : 0.f;
            period = ((float)lag + shift) / (float)k2;
            k      = k2;
        }
        return period;
    }

    // Incremental analysis engine.  Called once per full-rate sample while an
    // analysis is in flight.  Each call consumes at most workBudget units of
    // work (1 unit = one float_4 iteration = 4 element operations), crossing
    // state boundaries as needed, so the per-sample cost stays flat.
    void processChunk() {
        int budget = workBudget;
        while (budget > 0 && acState != 0) {
            switch (acState) {

                case 1: { // accumulate mean of the captured frame
                    int elems = std::min(bufferSize - chunkPos, budget * 4);
                    for (int i = chunkPos; i < chunkPos + elems; ++i)
                        acMean += processBuffer[i];
                    chunkPos += elems;
                    budget   -= (elems + 3) / 4;
                    if (chunkPos >= bufferSize) {
                        acMean  /= bufferSize;
                        chunkPos = 0;
                        acState  = 2;
                    }
                    break;
                }

                case 2: { // subtract mean (DC removal)
                    int elems = std::min(bufferSize - chunkPos, budget * 4);
                    for (int i = chunkPos; i < chunkPos + elems; ++i)
                        dcRemovedSignal[i] = processBuffer[i] - acMean;
                    chunkPos += elems;
                    budget   -= (elems + 3) / 4;
                    if (chunkPos >= bufferSize) {
                        // No need to zero ac[] -- every ac[acLag] is written before being read
                        acLag    = 1;
                        chunkPos = 0;
                        partNum  = float_4(0.f);
                        partSq0  = float_4(0.f);
                        partSqL  = float_4(0.f);
                        acState  = 3;
                    }
                    break;
                }

                case 3: { // normalised autocorrelation, partial lags carried across calls
                    const float* w   = win.data();
                    const float* sig = dcRemovedSignal.data();
                    int iters = std::min((simdLen - chunkPos) / 4, budget);
                    for (int n = 0; n < iters; ++n) {
                        float_4 wi = float_4::load(w   + chunkPos);
                        float_4 a  = float_4::load(sig + chunkPos)         * wi;
                        float_4 b  = float_4::load(sig + chunkPos + acLag) * wi;
                        partNum += a * b;
                        partSq0 += a * a;
                        partSqL += b * b;
                        chunkPos += 4;
                    }
                    budget -= iters;
                    if (chunkPos >= simdLen) {
                        float num = partNum[0] + partNum[1] + partNum[2] + partNum[3];
                        float sq0 = partSq0[0] + partSq0[1] + partSq0[2] + partSq0[3];
                        float sqL = partSqL[0] + partSqL[1] + partSqL[2] + partSqL[3];
                        // Scalar tail for remaining samples of this lag
                        for (int i = simdLen; i < acHalf; ++i) {
                            float a = sig[i]         * w[i];
                            float b = sig[i + acLag] * w[i];
                            num += a * b;
                            sq0 += a * a;
                            sqL += b * b;
                        }
                        ac[acLag] = std::min(1.f, std::max(-1.f,
                                        num / (sqrtf(sq0 * sqL) + 1e-12f)));
                        acLag++;
                        chunkPos = 0;
                        partNum  = float_4(0.f);
                        partSq0  = float_4(0.f);
                        partSqL  = float_4(0.f);
                        budget--;  // tail + normalisation counted as one unit
                        if (acLag >= acHalf) {
                            acGlobalPeak    = -2.f;
                            acGlobalPeakLag = 1;
                            chunkPos        = 1;
                            acState         = 4;
                        }
                    }
                    break;
                }

                case 4: { // global peak scan over ac[1 .. acHalf-1]
                    int last = std::min(acHalf, chunkPos + budget * 4);
                    for (int lag = chunkPos; lag < last; ++lag) {
                        if (ac[lag] > acGlobalPeak) {
                            acGlobalPeak    = ac[lag];
                            acGlobalPeakLag = lag;
                        }
                    }
                    budget  -= (last - chunkPos + 3) / 4;
                    chunkPos = last;
                    if (chunkPos >= acHalf) {
                        acPeakValue = acGlobalPeak;
                        if (acGlobalPeak < confidenceFloor) {
                            finishAnalysis(-1.f);
                            break;
                        }
                        if (acGlobalPeak < confidenceFloor + 0.10f) {
                            finishAnalysis(-2.f);
                            break;
                        }
                        acThreshold = std::min(0.88f, std::max(confidenceFloor + 0.10f,
                                          0.55f + 0.1f * (acGlobalPeak - 0.8f)));
                        acCandidateLag = 0;
                        acDipLag       = (ac[1] < acThreshold) ? 1 : 0;
                        chunkPos       = 2;
                        acState        = 5;
                    }
                    break;
                }

                case 5: { // first-peak candidate scan, then finalize
                    int last = std::min(acHalf - 1, chunkPos + budget * 4);
                    int lag  = chunkPos;
                    for (; lag < last; ++lag) {
                        // RobotBoy: peaks only count beyond the lobe around lag 0.
                        // A signal slower than this tracker's range is a ramp across
                        // the frame; its autocorrelation falls slowly away from 1, and
                        // any noise ripples it into "peaks" at lags of a few samples.
                        if (acDipLag == 0) {
                            if (ac[lag] < acThreshold) acDipLag = lag;
                            continue;
                        }
                        if (ac[lag] > ac[lag-1] && ac[lag] >= ac[lag+1]
                            && ac[lag] >= acThreshold) {
                            acCandidateLag = lag;
                            break;
                        }
                    }
                    budget  -= (lag - chunkPos) / 4 + 1;
                    chunkPos = lag;
                    if (acCandidateLag == 0 && chunkPos < acHalf - 1) break;

                    // Finalize: constant-time selection and refinement.
                    int bestLag = (acCandidateLag >= 2) ? acCandidateLag
                                                        : std::max(2, acGlobalPeakLag);

                    // RobotBoy: with no candidate, the global peak only counts if it is
                    // a peak beyond the lobe around lag 0.  For a ramp it is lag 1,
                    // and interpolating there gave a confident, meaningless frequency
                    // that outvoted the LFO tracker.
                    if (acCandidateLag < 2
                        && !(acDipLag > 0 && bestLag > acDipLag && bestLag < acHalf - 1
                             && ac[bestLag] > ac[bestLag - 1] && ac[bestLag] >= ac[bestLag + 1])) {
                        finishAnalysis(-1.f);
                        break;
                    }

                    // Sub-harmonic correction: harmonic-rich waveforms (square, pulse, saw) produce
                    // strong AC peaks at sub-multiples of the true period (lag/2, lag/3, lag/4).
                    // The first-peak search can land on one of these instead of the fundamental.
                    // If a shorter lag is itself a local peak and nearly as strong, it wins.
                    for (int divisor = 2; divisor <= 4; ++divisor) {
                        int subLag = bestLag / divisor;
                        if (subLag < 2 || subLag >= acHalf - 1) continue;
                        bool isLocalMax   = ac[subLag] > ac[subLag - 1] && ac[subLag] >= ac[subLag + 1];
                        bool isComparable = ac[subLag] >= ac[bestLag] * 0.85f;
                        if (isLocalMax && isComparable) bestLag = subLag;
                    }

                    // Parabolic sub-sample interpolation
                    float freq = -1.f;
                    if (bestLag > 1 && bestLag < acHalf - 1) {
                        float y0    = ac[bestLag - 1];
                        float y1    = ac[bestLag];
                        float y2    = ac[bestLag + 1];
                        float denom = y0 - 2.f * y1 + y2;
                        float shift = (fabsf(denom) > 1e-12f) ? (0.5f * (y0 - y2) / denom) : 0.f;
                        float refinedLag = (float)bestLag + shift;
                        if (refinedLag > 1.f) {
                            // RobotBoy: one peak is only as precise as the lag grid allows
                            refinedLag = refinePeriod(refinedLag, y1);
                            freq = decimatedSampleRate / refinedLag;
                        }
                    }
                    if (!(std::isfinite(freq) && freq > 0.f)) {
                        freq = decimatedSampleRate / (float)bestLag;
                        if (!(std::isfinite(freq) && freq > 0.f)) freq = -1.f;
                    }
                    finishAnalysis(freq);
                    break;
                }
            }
        }
    }
};
constexpr int DecimatedFrequencyTracker::MAX_BUFFER_SIZE;   // std::min takes it by reference, so C++11 needs a definition


// ---------------------------------------------------------------------------
// DualRangeTracker
//
// Two DecimatedFrequencyTrackers in parallel:
//   audioTracker : 4x decimation  -> ~12kHz effective at 48kHz -> ~16Hz to ~6kHz
//   lfoTracker   : 128x decimation -> ~375Hz effective at 48kHz -> ~0.5Hz to ~187Hz
//
// Both run every sample.  The result with higher AC confidence wins, with a
// bias toward the audio tracker to avoid spurious LFO detections on normal
// pitched signals.  The LFO tracker uses a relaxed confidence floor since
// slow periodic signals produce lower AC peaks over a fixed-length window.
//
// audioNyquist is the Nyquist of the audio tracker.  Results within 1Hz of
// this ceiling are spurious alias locks from slow/LFO signals and are
// suppressed -- the caller should treat them as no-lock.
// ---------------------------------------------------------------------------
struct DualRangeTracker {
    DecimatedFrequencyTracker audioTracker;
    DecimatedFrequencyTracker lfoTracker;

    // Nyquist ceiling of the audio tracker, computed in setSampleRate.
    // Results this close to the ceiling are considered spurious alias locks.
    float audioNyquist = 6000.f;

    void setSampleRate(float sr) {
        // Audio tracker: 4x decimation -> 12kHz effective -> ceiling ~6kHz
        // buffer = 2*12000/16 = 1500 samples
        // SIMD float_4 on inner loop keeps CPU cost manageable
        audioTracker.setDecimation(4, sr, 16.f);
        audioTracker.setConfidenceFloor(0.25f);

        // LFO tracker: min 0.5Hz, buffer = 2*375/0.5 = 1500 samples (full)
        lfoTracker.setDecimation(128, sr, 0.5f);
        lfoTracker.setConfidenceFloor(0.15f);

        // Nyquist = effectiveRate / 2 = (sr / decimation) / 2
        audioNyquist = audioTracker.decimatedSampleRate / 2.f;
    }

    void setLagsPerCall(int lags) {
        audioTracker.setLagsPerCall(lags);
        lfoTracker.setLagsPerCall(lags);
    }

    void setAnalysisSkip(int skip) {
        audioTracker.setAnalysisSkip(skip);
        lfoTracker.setAnalysisSkip(skip);
    }

    float process(float in) {
        float audioFreq = audioTracker.process(in);
        float lfoFreq   = lfoTracker.process(in);

        bool audioValid = (audioFreq > 0.f && audioTracker.acPeakValue > 0.35f);
        bool lfoValid   = (lfoFreq   > 0.f && lfoTracker.acPeakValue   > 0.25f);

        if (audioValid && lfoValid)
            return (audioTracker.acPeakValue >= lfoTracker.acPeakValue - 0.15f)
                   ? audioFreq : lfoFreq;
        if (audioValid) return audioFreq;
        if (lfoValid)   return lfoFreq;
        return -1.f;
    }
};


// ---------------------------------------------------------------------------
// PhaseAccumulatorTrigger
//
// Generates display trigger pulses from the known detected frequency rather
// than from raw zero-crossings.  This eliminates node-flickering on waveforms
// with multiple zero-crossings per cycle (square, saw, complex harmonics).
//
// The accumulator advances at currentHz / (sampleRate * numCycles) per sample
// and wraps once per full display window.  On wrap it arms itself and waits
// for the next rising zero-crossing to resync phase, keeping the display
// aligned to the waveform shape.
//
// The resync window is scaled by 1/numCycles so it represents a fixed fraction
// of one single period regardless of how many cycles are displayed.
//
// A safety fallback fires if no zero-crossing appears within half the window,
// handling DC-offset signals or silence gracefully.
// ---------------------------------------------------------------------------
struct PhaseAccumulatorTrigger {
    float phase          = 0.f;
    float phaseIncrement = 0.f;
    bool  armed          = false;
    bool  triggered      = false;

    // Fraction of one single period accepted as resync tolerance on each side.
    static constexpr float RESYNC_WINDOW = 0.15f;

    void update(float currentHz, float sampleRate, float numCycles,
                float signalIn, float prevSignalIn) {
        triggered = false;

        if (currentHz > 0.f)
            phaseIncrement = currentHz / (sampleRate * numCycles);

        phase += phaseIncrement;

        if (phase >= 1.f) {
            phase -= 1.f;
            armed  = true;
        }

        bool risingCrossing = (signalIn >= 0.f && prevSignalIn < 0.f);
        float resyncWindow  = RESYNC_WINDOW / numCycles;

        if (risingCrossing) {
            if (armed) {
                phase     = 0.f;
                armed     = false;
                triggered = true;
            } else if (phase > 1.f - resyncWindow) {
                phase     = 0.f;
                triggered = true;
            }
        }

        // Safety: fire if no zero-crossing appears within half the window
        if (armed && phase > 0.5f) {
            armed     = false;
            triggered = true;
        }
    }
};
