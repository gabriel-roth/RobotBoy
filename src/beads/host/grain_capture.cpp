#include "host/grain_capture.h"

#include <algorithm>
#include <cmath>

namespace robotboy_beads {

namespace {

int32_t Wrap(int64_t i, int32_t n) {
  const int64_t r = i % n;
  return int32_t(r < 0 ? r + n : r);
}

}  // namespace

int32_t GrainCapture::ReadIndex(int32_t write_ptr, int32_t delay_q10,
                                int32_t max_delay) {
  return Wrap(int64_t(write_ptr) + (delay_q10 >> 10) + max_delay - 1, max_delay);
}

int GrainCapture::Column(int32_t index, int32_t max_delay) {
  return int(int64_t(max_delay - 1 - index) * GrainFeed::kColumns / max_delay);
}

void GrainCapture::AfterBlock(const beads::Processor& processor) {
  if (!feed_) return;
  if (!feed_->attached.load(std::memory_order_relaxed)) {
    synced_ = false;
    return;
  }
  auto visit = [this](const auto& granular, bool mono) {
    if (mono) {
      Capture(granular.mono_delay_line(), granular.granular_player(), true);
    } else {
      Capture(granular.stereo_delay_line(), granular.granular_player(), false);
    }
  };
  processor.VisitGranular(visit);
}

template <typename Line, typename Player>
void GrainCapture::Capture(const Line& line, const Player& player, bool mono) {
  const int32_t max_delay = Line::MAX_DELAY;
  const int32_t write = line.write_ptr();
  if (!synced_ || mono != mono_ || max_delay != max_delay_) {
    feed_->Reset();
    synced_ = true;
    mono_ = mono;
    max_delay_ = max_delay;
    last_write_ = write;
    column_ = -1;
    // Grains already playing at a resync are not announced.
    for (int s = 0; s < GrainFeed::kSlots; ++s) serial_[s] = player.grain(s).serial();
  }

  // DelayLine::Write stores at write_ptr, then decrements it: this block
  // wrote last_write_, last_write_ - 1, ..., write + 1.
  const int32_t written = Wrap(int64_t(last_write_) - write, max_delay);
  for (int32_t k = 0; k < written; ++k) {
    const int32_t i = Wrap(int64_t(last_write_) - k, max_delay);
    const beads::FloatFrame f = line.LoadAt(i);
    const float lo = std::min(f.l, f.r);
    const float hi = std::max(f.l, f.r);
    const int c = Column(i, max_delay);
    if (c != column_) {
      // The write head entered a column: its old audio is being replaced.
      column_ = c;
      column_lo_ = lo;
      column_hi_ = hi;
    } else {
      column_lo_ = std::min(column_lo_, lo);
      column_hi_ = std::max(column_hi_, hi);
    }
    feed_->StoreColumn(c, column_lo_, column_hi_);
  }
  last_write_ = write;

  GrainLiveFrame& frame = feed_->BeginFrame();
  frame.write_index = write;
  frame.max_delay = max_delay;
  frame.frozen = line.frozen();
  for (int s = 0; s < GrainFeed::kSlots; ++s) {
    const auto& g = player.grain(s);
    const int32_t position = ReadIndex(write, g.delay(), max_delay);
    if (g.serial() != serial_[s]) {
      serial_[s] = g.serial();
      // A grain's read index moves by -speed per sample, so it started
      // speed * elapsed above where it is now.
      const float speed = float(g.speed()) / 1024.0f;
      const float elapsed = float(g.duration() - g.remaining_samples());
      feed_->PushEvent({g.serial(), s,
                        Wrap(int64_t(position) + std::lround(speed * elapsed), max_delay),
                        speed, float(g.duration())});
    }
    frame.slots[s] = {g.serial(), position, g.level(), g.done(),
                      float(g.duration() - g.remaining_samples())};
  }
  feed_->EndFrame();
}

}  // namespace robotboy_beads
