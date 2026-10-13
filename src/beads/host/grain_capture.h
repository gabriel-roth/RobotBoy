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
  void AfterBlock(const beads::Processor& processor);

  // Delay-line index a grain reads, from its Q10 delay (DelayLine::Read's
  // Hermite latency fix included).
  static int32_t ReadIndex(int32_t write_ptr, int32_t delay_q10, int32_t max_delay);
  // Peak column for an index: newer audio to the right, so the write head
  // (which decrements) moves right.
  static int Column(int32_t index, int32_t max_delay);

 private:
  template <typename Line, typename Player>
  void Capture(const Line& line, const Player& player, bool mono);

  GrainFeed* feed_ = nullptr;
  bool synced_ = false;
  bool mono_ = false;
  int32_t max_delay_ = 0;
  int32_t last_write_ = 0;
  int column_ = -1;
  float column_lo_ = 0.0f;
  float column_hi_ = 0.0f;
  uint32_t serial_[GrainFeed::kSlots] = {};
};

}  // namespace robotboy_beads
