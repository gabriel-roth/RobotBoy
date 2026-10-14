#include "host/grain_capture.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

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

void GrainCapture::AfterBlock(const beads::Processor& processor, float engine_rate) {
  if (!feed_) return;
  if (!feed_->attached.load(std::memory_order_relaxed)) {
    synced_ = false;
    return;
  }
  auto visit = [this, &processor, engine_rate](const auto& granular, bool mono) {
    using GP = std::decay_t<decltype(granular)>;
    TapSource src{&granular.last_taps(), &processor.delay_controller(),
                  GP::kDownsampleRatio, engine_rate / float(GP::kDownsampleRatio)};
    const TapSource* taps = granular.tap_mode() ? &src : nullptr;
    if (mono) {
      Capture(granular.mono_delay_line(), granular.granular_player(), true, taps);
    } else {
      Capture(granular.stereo_delay_line(), granular.granular_player(), false, taps);
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

// After a scope reset, fill the ring's older entries from the audio that
// was already in the line, newest first: entry kScopeEntries - 1 - j covers
// the spe samples starting at offset 1 + j * spe above the write pointer at
// the reset (larger index = older). Same flat budget as the columns (blocks
// run at 4000/s, 192 reads each): a full ring takes about 0.26 s at Bright
// stereo (spe 24), 0.5 s at Bright mono and 1 s at Scorched mono.
constexpr int kScopeBackfillReads = 192;

template <typename Line>
void GrainCapture::ScopeBackfill(const Line& line, int32_t max_delay) {
  const int k = GrainFeed::kScopeEntries;
  const int32_t spe = scope_spe_;
  int budget = kScopeBackfillReads;
  while (budget > 0 && scope_backfill_done_ < k) {
    const int j = scope_backfill_done_;
    const uint32_t abs = uint32_t(k - 1 - j);
    const int64_t first = 1 + int64_t(j) * spe;
    // Stop once live entries have reached this slot, or live writes since
    // the reset have overwritten the samples this entry would read (they
    // replace the oldest audio first, so every older entry is gone too).
    if (scope_head_ - abs >= uint32_t(k) ||
        first + spe > int64_t(max_delay) - scope_live_) {
      scope_backfill_done_ = k;
      break;
    }
    if (scope_backfill_next_ < 0) {
      scope_backfill_next_ = 0;
      scope_backfill_lo_ = 0.0f;
      scope_backfill_hi_ = 0.0f;
    }
    for (; scope_backfill_next_ < spe && budget > 0; ++scope_backfill_next_, --budget) {
      const beads::FloatFrame f = line.LoadAt(
          Wrap(int64_t(scope_write_at_reset_) + first + scope_backfill_next_, max_delay));
      const float l = std::min(f.l, f.r), h = std::max(f.l, f.r);
      const bool start = scope_backfill_next_ == 0;
      scope_backfill_lo_ = start ? l : std::min(scope_backfill_lo_, l);
      scope_backfill_hi_ = start ? h : std::max(scope_backfill_hi_, h);
    }
    if (scope_backfill_next_ >= spe) {
      feed_->ScopeStore(abs, scope_backfill_lo_, scope_backfill_hi_);
      ++scope_backfill_done_;
      scope_backfill_next_ = -1;
    }
  }
}

template <typename Line, typename Player>
void GrainCapture::Capture(const Line& line, const Player& player, bool mono,
                           const TapSource* taps) {
  const int32_t max_delay = Line::MAX_DELAY;
  const int32_t write = line.write_ptr();
  // A block is at most 12 samples (times the resampling ratio); anything
  // far beyond that means we missed blocks, so resync instead of looping.
  constexpr int32_t kMaxWritten = 64;
  if (synced_ && Wrap(int64_t(last_write_) - write, max_delay) > kMaxWritten) synced_ = false;
  const bool resync = !synced_ || mono != mono_ || max_delay != max_delay_;
  if (resync) {
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

  // Scope ring, tap mode only: (re)start it on a resync, when tap mode
  // starts, or when its scale changes.
  const bool scope = taps != nullptr;
  if (scope) {
    const int32_t spe = (max_delay + GrainFeed::kScopeEntries - 1) / GrainFeed::kScopeEntries;
    if (resync || !scope_on_ || spe != scope_spe_ || taps->line_rate != scope_rate_) {
      feed_->ScopeReset(spe, taps->line_rate);
      scope_on_ = true;
      scope_spe_ = spe;
      scope_rate_ = taps->line_rate;
      scope_head_ = uint32_t(GrainFeed::kScopeEntries);
      scope_count_ = 0;
      scope_live_ = 0;
      // The line holds older audio above the next index to be written.
      scope_write_at_reset_ = last_write_;
      scope_backfill_done_ = 0;
      scope_backfill_next_ = -1;
    }
  } else {
    scope_on_ = false;
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
    if (scope) {
      // Oldest to newest, as the line wrote them.
      scope_lo_ = scope_count_ == 0 ? lo : std::min(scope_lo_, lo);
      scope_hi_ = scope_count_ == 0 ? hi : std::max(scope_hi_, hi);
      if (++scope_count_ == scope_spe_) {
        feed_->ScopeStore(scope_head_++, scope_lo_, scope_hi_);
        scope_count_ = 0;
      }
    }
  }
  last_write_ = write;
  Backfill(line, write, max_delay);
  if (scope) {
    scope_live_ += written;
    feed_->ScopePublish(scope_head_);
    ScopeBackfill(line, max_delay);
  }

  GrainLiveFrame& frame = feed_->BeginFrame();
  frame.write_index = write;
  frame.max_delay = max_delay;
  frame.frozen = line.frozen();
  frame.tap_mode = taps != nullptr;
  if (taps) {
    frame.ratio_gain[0] = taps->controller->ratio_gain(0);
    frame.ratio_gain[1] = taps->controller->ratio_gain(1);
    const bool sliced = taps->controller->frozen();
    for (int i = 0; i < GrainLiveFrame::kTaps; ++i) {
      const beads::DelayTap& t = taps->out->tap[i];
      TapState& s = frame.taps[i];
      s.read_index = t.delay > 0 ? ReadIndex(write, t.delay, max_delay) : write;
      s.level = t.delay > 0 ? t.gain : 0.0f;
      s.slice = sliced;
      if (sliced) {
        const int h = i / 2;
        const beads::PlayHead& ph = (i & 1) ? taps->controller->slave_play_head(h)
                                            : taps->controller->play_head(h);
        s.serial = ph.serial();
        s.slice_length = ph.duration() / taps->ratio;
        s.slice_start = ReadIndex(
            write, int32_t((int64_t(ph.start() + ph.duration()) << 10) / taps->ratio),
            max_delay);
      } else {
        s.serial = 0;
        s.slice_start = s.slice_length = 0;
      }
    }
    for (int s = 0; s < GrainFeed::kSlots; ++s) {
      frame.slots[s] = {serial_[s], write, 0.0f, true, 0.0f};
    }
  } else {
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
                          speed, float(g.duration()), g.pan()});
      }
      frame.slots[s] = {g.serial(), position, g.level(), g.done(),
                        float(g.duration() - g.remaining_samples())};
    }
  }
  feed_->EndFrame();
}

}  // namespace robotboy_beads
