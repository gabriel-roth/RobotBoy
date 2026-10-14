// Copies the active granular processor's buffer and grains into a
// GrainFeed after each engine block, for Lucarne's display. Runs on the
// audio thread, only while the feed says a Lucarne is attached; reads the
// processor and writes atomics, nothing else.

#pragma once

#include <cstdint>

#include "beads/dsp/processor.h"
#include "host/grain_feed.h"

namespace robotboy_beads {

class GrainCapture {
 public:
  void Attach(GrainFeed* feed) {
    feed_ = feed;
    synced_ = false;
  }
  // The buffer was reformatted or cleared: resync at the next block.
  void Invalidate() { synced_ = false; }
  // engine_rate: the processor's sample rate (EngineHost::engine_rate());
  // the line runs at engine_rate / the granular processor's downsampling.
  void AfterBlock(const beads::Processor& processor, float engine_rate);

  // Delay-line index a grain reads, from its Q10 delay (DelayLine::Read's
  // Hermite latency fix included).
  static int32_t ReadIndex(int32_t write_ptr, int32_t delay_q10, int32_t max_delay);
  // Peak column for an index: newer audio to the right, so the write head
  // (which decrements) moves right.
  static int Column(int32_t index, int32_t max_delay);

 private:
  // The delay controller's current taps, when the processor is in delay mode.
  struct TapSource {
    const beads::DelayController::Output* out;
    const beads::DelayController* controller;
    int ratio;
    float line_rate;  // line samples per second
  };

  // taps is nullptr in granular mode.
  template <typename Line, typename Player>
  void Capture(const Line& line, const Player& player, bool mono,
               const TapSource* taps);

  template <typename Line>
  void Backfill(const Line& line, int32_t write, int32_t max_delay);
  template <typename Line>
  void ScopeBackfill(const Line& line, int32_t max_delay);

  GrainFeed* feed_ = nullptr;
  bool synced_ = false;
  bool mono_ = false;
  int32_t max_delay_ = 0;
  int32_t last_write_ = 0;
  int column_ = -1;
  // Backfill after a resync: columns done (of kColumns), the column the
  // write head was in at the resync (filled newest-first from there), and
  // the partly read column carried across blocks.
  int backfill_done_ = GrainFeed::kColumns;
  int backfill_start_ = 0;
  int64_t backfill_next_ = -1;  // next delay offset to read; -1 = start a column
  float backfill_lo_ = 0.0f;
  float backfill_hi_ = 0.0f;
  float column_lo_ = 0.0f;
  float column_hi_ = 0.0f;
  uint32_t serial_[GrainFeed::kSlots] = {};

  // Scope ring (tap mode only). scope_on_: the ring was set up for the
  // current tap-mode run; cleared outside tap mode so a new run resets it.
  bool scope_on_ = false;
  int32_t scope_spe_ = 0;
  float scope_rate_ = 0.0f;
  uint32_t scope_head_ = 0;      // absolute index of the entry being filled
  int32_t scope_count_ = 0;      // samples in that entry so far
  float scope_lo_ = 0.0f;
  float scope_hi_ = 0.0f;
  int64_t scope_live_ = 0;       // line samples written since the reset
  // Scope backfill: the write pointer at the reset (the line held older
  // audio above it), entries done (newest first, of kScopeEntries), and
  // the partly read entry carried across blocks.
  int32_t scope_write_at_reset_ = 0;
  int scope_backfill_done_ = GrainFeed::kScopeEntries;
  int32_t scope_backfill_next_ = -1;  // next sample of the entry; -1 = start one
  float scope_backfill_lo_ = 0.0f;
  float scope_backfill_hi_ = 0.0f;
};

}  // namespace robotboy_beads
