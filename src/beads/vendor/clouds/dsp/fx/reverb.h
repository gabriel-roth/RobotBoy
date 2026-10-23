// Copyright 2014 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Reverb.

#ifndef CLOUDS_DSP_FX_REVERB_H_
#define CLOUDS_DSP_FX_REVERB_H_

#include <type_traits>  // RobotBoy

#include "stmlib/stmlib.h"

#include "clouds/dsp/fx/fx_engine.h"

namespace clouds {

class Reverb {
 public:
  Reverb() { }
  ~Reverb() { }
  
  void Init(uint16_t* buffer) {
    engine_.Init(buffer);
    engine_.SetLFOFrequency(LFO_1, 0.5f / 32000.0f);
    engine_.SetLFOFrequency(LFO_2, 0.3f / 32000.0f);
    lp_ = 0.7f;
    diffusion_ = 0.625f;
    mod_depth_ = 1.0f;  // RobotBoy
    size_ = 1.0f;  // RobotBoy
    size_now_ = 1.0f;  // RobotBoy
    force_custom_ = false;  // RobotBoy
  }
  
  // RobotBoy: at the default modulation depth and size the tank runs the
  // stock code with its constants, so the output matches upstream bit for bit
  // (a variable in place of a constant can change the rounding under
  // -funsafe-math-optimizations). Anything else, or set_force_custom(true),
  // takes the variable-driven instance, whose delay layout has room for 2x
  // size and 4x depth.
  void Process(FloatFrame* in_out, size_t size) {
    if (!force_custom_ && mod_depth_ == 1.0f && size_ == 1.0f &&
        size_now_ == 1.0f) {
      ProcessTank<false>(in_out, size);
    } else {
      ProcessTank<true>(in_out, size);
    }
  }
  
 private:
  template<bool custom>
  void ProcessTank(FloatFrame* in_out, size_t size) {
    // This is the Griesinger topology described in the Dattorro paper
    // (4 AP diffusers on the input, then a loop of 2x 2AP+1Delay).
    // Modulation is applied in the loop of the first diffuser AP for additional
    // smearing; and to the two long delays for a slow shimmer/chorus effect.
    typedef E::Reserve<113,
      E::Reserve<162,
      E::Reserve<241,
      E::Reserve<399,
      E::Reserve<1653,
      E::Reserve<2038,
      E::Reserve<3411,
      E::Reserve<1913,
      E::Reserve<1663,
      E::Reserve<4782> > > > > > > > > > StockMemory;
    // RobotBoy: the custom layout makes room for kMaxSize and kMaxModDepth.
    // Every delay is read at its stock length times the size, interpolated
    // (exact at size 1, where the offsets are whole). ap1's smear tap is
    // re-written further along the line, and its tail moves with it, so at
    // size 1 the allpass keeps its stock delay; del2 is only read through its
    // modulated tap.
    typedef E::Reserve<kAp1Length,
      E::Reserve<161 * kMaxSize + 2,
      E::Reserve<240 * kMaxSize + 2,
      E::Reserve<398 * kMaxSize + 2,
      E::Reserve<1652 * kMaxSize + 2,
      E::Reserve<2037 * kMaxSize + 2,
      E::Reserve<3410 * kMaxSize + 2,
      E::Reserve<1912 * kMaxSize + 2,
      E::Reserve<1662 * kMaxSize + 2,
      E::Reserve<kDel2Length> > > > > > > > > > CustomMemory;
    typedef typename std::conditional<
        custom, CustomMemory, StockMemory>::type Memory;
    E::DelayLine<Memory, 0> ap1;
    E::DelayLine<Memory, 1> ap2;
    E::DelayLine<Memory, 2> ap3;
    E::DelayLine<Memory, 3> ap4;
    E::DelayLine<Memory, 4> dap1a;
    E::DelayLine<Memory, 5> dap1b;
    E::DelayLine<Memory, 6> del1;
    E::DelayLine<Memory, 7> dap2a;
    E::DelayLine<Memory, 8> dap2b;
    E::DelayLine<Memory, 9> del2;
    E::Context c;

    const float kap = diffusion_;
    const float klp = lp_;
    const float krt = reverb_time_;
    const float amount = amount_;
    const float gain = input_gain_;
    const float mod_depth = custom ? mod_depth_ : 1.0f;  // RobotBoy
    // RobotBoy: the size glides linearly across the block to its new value.
    // The stock path keeps the constant 1.
    float scale = custom ? size_now_ : 1.0f;
    const float scale_step = custom && size ?
        (size_ - size_now_) / static_cast<float>(size) : 0.0f;
    if (custom) {
      size_now_ = size_;
    }

    float lp_1 = lp_decay_1_;
    float lp_2 = lp_decay_2_;

    while (size--) {
      float wet;
      float apout = 0.0f;
      engine_.Start(&c);
      if (custom) {
        scale += scale_step;  // RobotBoy
      }
      
      // Smear AP1 inside the loop.
      c.Interpolate(ap1, 10.0f * scale, LFO_1, 60.0f * mod_depth, 1.0f);  // RobotBoy
      c.Write(ap1, custom ? kAp1SmearWrite : 100, 0.0f);  // RobotBoy
      
      c.Read(in_out->l + in_out->r, gain);

      // Diffuse through 4 allpasses.
      // RobotBoy: the custom path reads each tail at its stock offset times
      // the size; ap1's tail sits kAp1SmearToTail (scaled) past the smear.
      if (custom) {
        c.Interpolate(
            ap1,
            static_cast<float>(kAp1SmearWrite) +
                static_cast<float>(kAp1SmearToTail) * scale,
            kap);
      } else {
        c.Read(ap1 TAIL, kap);
      }
      c.WriteAllPass(ap1, -kap);
      ReadTail<custom>(&c, ap2, 161.0f * scale, kap);
      c.WriteAllPass(ap2, -kap);
      ReadTail<custom>(&c, ap3, 240.0f * scale, kap);
      c.WriteAllPass(ap3, -kap);
      ReadTail<custom>(&c, ap4, 398.0f * scale, kap);
      c.WriteAllPass(ap4, -kap);
      c.Write(apout);
      
      // Main reverb loop.
      c.Load(apout);
      c.Interpolate(del2, 4680.0f * scale, LFO_2, 100.0f * mod_depth, krt);  // RobotBoy
      c.Lp(lp_1, klp);
      ReadTail<custom>(&c, dap1a, 1652.0f * scale, -kap);
      c.WriteAllPass(dap1a, kap);
      ReadTail<custom>(&c, dap1b, 2037.0f * scale, kap);
      c.WriteAllPass(dap1b, -kap);
      c.Write(del1, 2.0f);
      c.Write(wet, 0.0f);

      in_out->l += (wet - in_out->l) * amount;

      c.Load(apout);
      // c.Interpolate(del1, 4450.0f, LFO_1, 50.0f, krt);
      ReadTail<custom>(&c, del1, 3410.0f * scale, krt);  // RobotBoy
      c.Lp(lp_2, klp);
      ReadTail<custom>(&c, dap2a, 1912.0f * scale, kap);
      c.WriteAllPass(dap2a, -kap);
      ReadTail<custom>(&c, dap2b, 1662.0f * scale, -kap);
      c.WriteAllPass(dap2b, kap);
      c.Write(del2, 2.0f);
      c.Write(wet, 0.0f);

      in_out->r += (wet - in_out->r) * amount;
      
      ++in_out;
    }
    
    lp_decay_1_ = lp_1;
    lp_decay_2_ = lp_2;
  }
  
 public:
  inline void set_amount(float amount) {
    amount_ = amount;
  }
  
  inline void set_input_gain(float input_gain) {
    input_gain_ = input_gain;
  }

  inline void set_time(float reverb_time) {
    reverb_time_ = reverb_time;
  }
  
  inline void set_diffusion(float diffusion) {
    diffusion_ = diffusion;
  }
  
  inline void set_lp(float lp) {
    lp_ = lp;
  }
  
  // RobotBoy: Brume's controls. set_lfo_rate replaces the hardcoded 32 kHz
  // and keeps the LFO phase; set_mod_depth scales the modulation excursions;
  // Clear empties the memory and both filter states (upstream leaves them
  // uninitialized).
  inline void set_lfo_rate(float multiplier, float sample_rate) {
    engine_.SetLFOFrequencyKeepPhase(LFO_1, 0.5f * multiplier / sample_rate);
    engine_.SetLFOFrequencyKeepPhase(LFO_2, 0.3f * multiplier / sample_rate);
  }
  
  // Clamped to 0 .. kMaxModDepth, which the custom layout has room for.
  inline void set_mod_depth(float depth) {
    const float max_depth = static_cast<float>(kMaxModDepth);
    mod_depth_ = depth < 0.0f ? 0.0f : (depth > max_depth ? max_depth : depth);
  }
  
  // Scales every delay length, kMinSize .. kMaxSize (clamped). It glides
  // linearly to a new value over the next Process() block; the custom path
  // only.
  inline void set_size(float size) {
    const float lo = kMinSize, hi = static_cast<float>(kMaxSize);
    size_ = size > hi ? hi : (size > lo ? size : lo);
  }
  
  // Always take the custom path, so that changing the depth or size never
  // switches delay layouts under a ringing tail.
  inline void set_force_custom(bool force) {
    force_custom_ = force;
  }
  
  inline float lfo_value(int index) const {
    return engine_.lfo_value(index ? LFO_2 : LFO_1);
  }
  
  inline void Clear() {
    engine_.Clear();
    lp_decay_1_ = 0.0f;
    lp_decay_2_ = 0.0f;
  }
  
 private:
  // RobotBoy: 65536 (upstream 16384), for the custom layout. The stock
  // layout's reads and writes are all relative to the write pointer, and the
  // LFOs step every 32 samples either way, so it runs unchanged inside the
  // larger memory.
  typedef FxEngine<65536, FORMAT_12_BIT> E;
  E engine_;
  
  // RobotBoy: the custom layout. Each read reaches its stock offset times
  // the size, plus, for the modulated ones, amplitude * depth * lfo with lfo
  // in [0, 1] (the excursions do not scale with the size), plus one for the
  // interpolation. It must stay short of the line's end (or, for ap1's smear
  // tap, of the smear write, beyond which it would read its own output).
  enum {
    kMaxSize = 2,
    kMaxModDepth = 4,
    kAp1SmearWrite = 265,  // stock: 100
    kAp1SmearToTail = 12,  // stock: 112 - 100
    kAp1Length = kAp1SmearWrite + kAp1SmearToTail * kMaxSize + 2,
    kDel2Length = 9770
  };
  static constexpr float kMinSize = 0.5f;
  static_assert(10 * kMaxSize + 60 * kMaxModDepth + 1 < kAp1SmearWrite,
                "ap1 smear");
  static_assert(kAp1SmearWrite + kAp1SmearToTail * kMaxSize + 1 < kAp1Length,
                "ap1 tail");
  static_assert(4680 * kMaxSize + 100 * kMaxModDepth + 1 < kDel2Length,
                "del2");
  
  // RobotBoy: a tail read. The stock path reads the line's last word; the
  // custom path reads at `offset` (the stock offset times the size).
  template<bool custom, typename D>
  static inline void ReadTail(E::Context* c, D& d, float offset, float scale) {
    if (custom) {
      c->Interpolate(d, offset, scale);
    } else {
      c->Read(d TAIL, scale);
    }
  }
  
  float amount_;
  float input_gain_;
  float reverb_time_;
  float diffusion_;
  float lp_;
  float mod_depth_;  // RobotBoy
  float size_;  // RobotBoy: target
  float size_now_;  // RobotBoy: at the end of the last block
  bool force_custom_;  // RobotBoy
  
  float lp_decay_1_;
  float lp_decay_2_;
  
  DISALLOW_COPY_AND_ASSIGN(Reverb);
};

}  // namespace clouds

#endif  // CLOUDS_DSP_FX_REVERB_H_
