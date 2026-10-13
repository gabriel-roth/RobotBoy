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

// After a resync the peaks are empty and the write head only fills columns
// as it passes (never, while frozen). Fill them from the audio already in
// the buffer, newest first, reading at most kBackfillReads samples per
// block so the audio thread's cost stays flat: a full ring takes 0.5 s
// (Bright mono) to 2 s (Scorched mono).
constexpr int kBackfillReads = 192;

template <typename Line>
void GrainCapture::Backfill(const Line& line, int32_t write, int32_t max_delay) {
  const int k = GrainFeed::kColumns;
  const int64_t m = max_delay;
  int budget = kBackfillReads;
  while (budget > 0 && backfill_done_ < k) {
    // Newest audio is just left of the write head's column; that column
    // itself comes last.
    const int c = ((backfill_start_ - 1 - backfill_done_) % k + k) % k;
    // The live path owns the column the write head is in, unless it's stopped.
    if (!line.frozen() && c == Column(write, max_delay)) {
      ++backfill_done_;
      backfill_next_ = -1;
      continue;
    }
    const int64_t d0 = (c * m + k - 1) / k;
    const int64_t d1 = std::min(((c + 1) * m + k - 1) / k - 1, m - 1);   // Column(i) == c for d in [d0, d1]
    if (backfill_next_ < 0) {
      backfill_next_ = d0;
      backfill_lo_ = 0.0f;
      backfill_hi_ = 0.0f;
    }
    for (; backfill_next_ <= d1 && budget > 0; ++backfill_next_, --budget) {
      const beads::FloatFrame f = line.LoadAt(int32_t(m - 1 - backfill_next_));
      const float l = std::min(f.l, f.r), h = std::max(f.l, f.r);
      const bool first = backfill_next_ == d0;
      backfill_lo_ = first ? l : std::min(backfill_lo_, l);
      backfill_hi_ = first ? h : std::max(backfill_hi_, h);
    }
    if (backfill_next_ > d1) {
      if (d1 >= d0) feed_->StoreColumn(c, backfill_lo_, backfill_hi_);
      ++backfill_done_;
      backfill_next_ = -1;
    }
  }
}

template <typename Line, typename Player>
void GrainCapture::Capture(const Line& line, const Player& player, bool mono) {
  const int32_t max_delay = Line::MAX_DELAY;
  const int32_t write = line.write_ptr();
  // A block is at most 12 samples (times the resampling ratio); anything
  // far beyond that means we missed blocks, so resync instead of looping.
  constexpr int32_t kMaxWritten = 64;
  if (synced_ && Wrap(int64_t(last_write_) - write, max_delay) > kMaxWritten) synced_ = false;
  if (!synced_ || mono != mono_ || max_delay != max_delay_) {
    feed_->Reset();
    synced_ = true;
    mono_ = mono;
    max_delay_ = max_delay;
    last_write_ = write;
    column_ = -1;
    backfill_done_ = 0;
    backfill_start_ = Column(write, max_delay);
    backfill_next_ = -1;
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
  Backfill(line, write, max_delay);

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
