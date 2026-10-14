// The hand-off from Particules' or Retours' audio thread to Lucarne's
// display: the buffer's waveform peaks, an event per grain start, the live
// state of every grain slot and, for Retours (delay mode), a finer ring of
// the line's recent audio for the echo scope. One producer (the engine's
// GrainCapture, audio thread), one consumer (Lucarne's widget, UI thread).
// Nothing here blocks or allocates; a slow reader loses old data, never
// corrupts it.

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace robotboy_beads {

struct GrainStartEvent {
  uint32_t serial;      // the slot's start counter (Grain::serial())
  int32_t slot;
  int32_t start_index;  // delay-line index where the grain began reading
  float speed;          // playback ratio; negative = reversed
  float duration;       // in delay-line samples
  float pan = 0.5f;     // stereo position: 0 = left, 0.5 = center, 1 = right
};

struct GrainSlotState {
  uint32_t serial;
  int32_t position_index;  // delay-line index of the read head
  float level;             // envelope, 0..1
  bool done;
  float elapsed;           // samples played so far (duration - remaining)
};

// One delay tap or Slice play head (delay mode). Slot meaning: interval =
// i / 4, ratio = (i / 2) % 2, lane bit = i & 1 (Slice: 0 = main play head,
// 1 = its overlapping second head).
struct TapState {
  int32_t read_index = 0;    // delay-line index the tap reads (nominal delay)
  float level = 0.0f;        // gain the tap rendered with (envelope and crossfades included)
  bool slice = false;        // a Slice play head: the fields below are valid
  uint32_t serial = 0;       // play head start counter
  int32_t slice_start = 0;   // index where the slice starts playing (its oldest end)
  int32_t slice_length = 0;  // in line samples
};

struct GrainLiveFrame {
  static constexpr int kSlots = 32;
  static constexpr int kTaps = 8;
  int32_t write_index = 0;
  int32_t max_delay = 1;  // delay-line length in samples
  bool frozen = false;
  bool tap_mode = false;  // delay mode: taps[] valid, slots all done, no events
  GrainSlotState slots[kSlots] = {};
  TapState taps[kTaps] = {};
  // Delay mode: each tap ratio's crossfade gain (no envelope). Tap i belongs
  // to ratio (i / 2) % 2; the louder ratio is the primary one.
  float ratio_gain[2] = {1.0f, 0.0f};
};

class GrainFeed {
 public:
  static constexpr int kColumns = 256;
  static constexpr int kSlots = GrainLiveFrame::kSlots;
  static constexpr int kEvents = 128;
  // Echo scope (delay mode): min/max of the line's audio, one entry per
  // samples_per_entry line samples, newest at head - 1.
  static constexpr int kScopeEntries = 8192;

  struct ScopeInfo {
    uint32_t head;              // one past the newest complete entry; 0 = never reset
    int32_t samples_per_entry;  // line samples per entry
    float line_rate;            // line samples per second
    uint32_t epoch;             // bumped by every ScopeReset
  };

  // Set by Particules or Retours: a Lucarne is attached (capture runs only
  // then).
  std::atomic<bool> attached{false};

  // --- Producer (audio thread) ---

  // The buffer was reformatted or cleared: empty the peaks, tell readers.
  void Reset() {
    for (auto& p : peaks_) p.store(0, std::memory_order_relaxed);
    epoch_.fetch_add(1, std::memory_order_release);
  }

  void StoreColumn(int column, float lo, float hi) {
    peaks_[column].store(Pack(Quantize(lo), Quantize(hi)), std::memory_order_relaxed);
  }

  void PushEvent(const GrainStartEvent& e) {
    const uint32_t w = event_write_.load(std::memory_order_relaxed);
    events_[w % kEvents] = e;
    event_write_.store(w + 1, std::memory_order_release);
  }

  // Scope ring: empty it and set its scale. Entries written from here on
  // start at absolute index kScopeEntries; older backfilled entries take
  // kScopeEntries - 1 down to 0.
  void ScopeReset(int32_t samples_per_entry, float line_rate) {
    for (auto& e : scope_) e.store(0, std::memory_order_relaxed);
    scope_spe_.store(samples_per_entry, std::memory_order_relaxed);
    scope_rate_.store(line_rate, std::memory_order_relaxed);
    scope_head_.store(uint32_t(kScopeEntries), std::memory_order_relaxed);
    scope_epoch_.fetch_add(1, std::memory_order_release);
  }

  void ScopeStore(uint32_t absolute_index, float lo, float hi) {
    scope_[absolute_index % kScopeEntries].store(Pack(Quantize(lo), Quantize(hi)),
                                                 std::memory_order_relaxed);
  }

  // Entries below head are complete.
  void ScopePublish(uint32_t head) { scope_head_.store(head, std::memory_order_release); }

  // Seqlock writer: BeginFrame(), fill the frame, EndFrame().
  GrainLiveFrame& BeginFrame() {
    frame_seq_.store(frame_seq_.load(std::memory_order_relaxed) + 1,
                     std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    return frame_;
  }
  void EndFrame() {
    frame_seq_.store(frame_seq_.load(std::memory_order_relaxed) + 1,
                     std::memory_order_release);
  }

  // --- Consumer (UI thread) ---

  uint32_t epoch() const { return epoch_.load(std::memory_order_acquire); }
  uint32_t event_write() const { return event_write_.load(std::memory_order_acquire); }

  void ReadPeaks(int8_t* lo, int8_t* hi) const {
    for (int c = 0; c < kColumns; ++c) {
      const uint16_t p = peaks_[c].load(std::memory_order_relaxed);
      lo[c] = int8_t(uint8_t(p & 0xff));
      hi[c] = int8_t(uint8_t(p >> 8));
    }
  }

  ScopeInfo ReadScopeInfo() const {
    ScopeInfo info{};
    // Retry if a reset lands mid-read, so the fields belong together.
    for (int attempt = 0; attempt < 4; ++attempt) {
      info.epoch = scope_epoch_.load(std::memory_order_acquire);
      info.head = scope_head_.load(std::memory_order_acquire);
      info.samples_per_entry = scope_spe_.load(std::memory_order_relaxed);
      info.line_rate = scope_rate_.load(std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_acquire);
      if (scope_epoch_.load(std::memory_order_relaxed) == info.epoch) break;
    }
    return info;
  }

  // Copies the n newest complete entries before head, newest first
  // (lo[0] is entry head - 1). Returns how many were copied. Like
  // ReadEvents, entries the producer may have overwritten during the copy
  // are dropped from the old end, so at most kScopeEntries - 1 come back;
  // none if the ring was reset since head was read.
  int ReadScope(uint32_t head, int8_t* lo, int8_t* hi, int n) const {
    n = std::min(n, kScopeEntries - 1);
    if (int64_t(n) > int64_t(head)) n = int(head);
    for (int j = 0; j < n; ++j) {
      const uint16_t p = scope_[(head - 1u - uint32_t(j)) % kScopeEntries].load(
          std::memory_order_relaxed);
      lo[j] = int8_t(uint8_t(p & 0xff));
      hi[j] = int8_t(uint8_t(p >> 8));
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    const int64_t lag =
        int64_t(scope_head_.load(std::memory_order_relaxed)) - int64_t(head);
    if (lag < 0) return 0;
    // The producer may be writing entry head + lag, whose slot holds
    // head + lag - kScopeEntries: only newer entries are intact.
    const int64_t intact = int64_t(kScopeEntries) - 1 - lag;
    return int(std::max<int64_t>(0, std::min<int64_t>(n, intact)));
  }

  // Copies the newest complete frame. False if none was published yet or
  // the writer kept interrupting. A seqlock: the memcpy may overlap a write
  // (formally a data race), and the sequence check discards that copy.
  bool ReadFrame(GrainLiveFrame* out) const {
    for (int attempt = 0; attempt < 4; ++attempt) {
      const uint32_t s1 = frame_seq_.load(std::memory_order_acquire);
      if (s1 == 0) return false;
      if (s1 & 1) continue;
      std::memcpy(static_cast<void*>(out), &frame_, sizeof frame_);
      std::atomic_thread_fence(std::memory_order_acquire);
      if (frame_seq_.load(std::memory_order_relaxed) == s1) return true;
    }
    return false;
  }

  // Copies the events after *cursor (at most max) and advances it. Only the
  // newest kEvents - 1 events are ever returned: the slot of the oldest ring
  // entry is the one a concurrent PushEvent would overwrite, so the reader
  // never starts on it, and any entry the producer may have reached during
  // the copy is dropped. Every returned event is intact.
  int ReadEvents(uint32_t* cursor, GrainStartEvent* out, int max) const {
    const uint32_t w = event_write_.load(std::memory_order_acquire);
    uint32_t c = *cursor;
    if (w - c > uint32_t(kEvents - 1)) c = w - (kEvents - 1);
    const uint32_t first = c;
    int n = 0;
    for (; c != w && n < max; ++c) out[n++] = events_[c % kEvents];
    // Order the copy above before re-reading the write index.
    std::atomic_thread_fence(std::memory_order_acquire);
    const uint32_t w2 = event_write_.load(std::memory_order_relaxed);
    // While event_write_ reads w2, the producer may be mid-write on the slot
    // of index w2 - kEvents, so that entry and everything older is suspect.
    if (w2 - first >= uint32_t(kEvents)) {
      const int skip = std::min(n, int(w2 - first - kEvents + 1));
      std::memmove(out, out + skip, sizeof(GrainStartEvent) * size_t(n - skip));
      n -= skip;
    }
    *cursor = c;
    return n;
  }

 private:
  static int8_t Quantize(float x) {
    return int8_t(std::lround(std::min(std::max(x, -1.0f), 1.0f) * 127.0f));
  }
  static uint16_t Pack(int8_t lo, int8_t hi) {
    return uint16_t(uint8_t(lo)) | uint16_t(uint16_t(uint8_t(hi)) << 8);
  }

  std::atomic<uint16_t> peaks_[kColumns] = {};
  std::atomic<uint32_t> epoch_{0};
  GrainStartEvent events_[kEvents] = {};
  std::atomic<uint32_t> event_write_{0};
  GrainLiveFrame frame_;
  std::atomic<uint32_t> frame_seq_{0};
  std::atomic<uint16_t> scope_[kScopeEntries] = {};
  std::atomic<uint32_t> scope_head_{0};
  std::atomic<int32_t> scope_spe_{1};
  std::atomic<float> scope_rate_{0.0f};
  std::atomic<uint32_t> scope_epoch_{0};
};

}  // namespace robotboy_beads
