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

#ifndef BEADS_DSP_FX_REVERB_H_
#define BEADS_DSP_FX_REVERB_H_

#include "stmlib/stmlib.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "beads/dsp/fx/fx_engine.h"

namespace beads {

class Reverb {
 public:
  Reverb() { }
  ~Reverb() { }
  
  void Init(uint16_t* buffer, float sample_rate) {
    engine_.Init(buffer);
    Reset(sample_rate);
    sleep_enabled_ = true;  // RobotBoy
    asleep_ = false;  // RobotBoy
    silent_samples_ = E::kSize;  // RobotBoy: the memory was just cleared.
    diffusion_ = 0.625f;  // RobotBoy
    mod_depth_ = 1.0f;  // RobotBoy
  }
  
  void Reset(float sample_rate) {
    engine_.SetLFOFrequency(LFO_1, 0.5f / sample_rate);
    engine_.SetLFOFrequency(LFO_2, 0.3f / sample_rate);
    lp_ = 0.7f;
    lp_1_ = 0.0f;
    lp_2_ = 0.0f;
    mix_ = 0.0f;
    dry_ = 0.0f;
    wet_ = 0.0f;
    send_ = 0.0f;
  }
  
  // RobotBoy: at the default diffusion and modulation depth the tank runs the
  // stock code with its constants, so the output stays bit-identical (with
  // -funsafe-math-optimizations, variables in place of the constants would
  // change the rounding). Anything else takes the variable-driven instance.
  void Process(FloatFrame* in_out, size_t size) {
    if (diffusion_ == 0.625f && mod_depth_ == 1.0f) {
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
    typedef E::Reserve<150,
      E::Reserve<214,
      E::Reserve<319,
      E::Reserve<527,
      E::Reserve<2182,
      E::Reserve<2690,
      E::Reserve<4501,
      E::Reserve<2525,
      E::Reserve<2197,
      E::Reserve<6312> > > > > > > > > > Memory;
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

    const float kap = custom ? diffusion_ : 0.625f;  // RobotBoy
    const float mod_depth = custom ? mod_depth_ : 1.0f;  // RobotBoy
    const float klp = lp_;
    const float krt = reverb_time_;

    const float input_gain = 0.2f * (1.0f - 0.333f * reverb_time_);
    const float output_gain = 0.4f / input_gain;
    
    // const float send_amount = 1.0f;
    // const float wet = mix_ * (2.0f - mix_) * output_gain;
    
    const float send_amount_sqrt = mix_ * (2.0f - mix_);
    const float send_amount = send_amount_sqrt * (2.0f - send_amount_sqrt);
    const float wet = output_gain;
    const float dry = (1.0f - mix_) * (1.0f + mix_);
    
    stmlib::ParameterInterpolator wet_modulation(&wet_, wet, size);
    stmlib::ParameterInterpolator dry_modulation(&dry_, dry, size);
    stmlib::ParameterInterpolator send_modulation(
        &send_,
        send_amount * input_gain,
        size);

    // RobotBoy: sleep. When the send is exactly 0, both filter states are
    // exactly 0 and the last full pass over the memory wrote nothing but
    // zeros, the tank can only output zeros. Skip it, but keep the level
    // glides, the write pointer and the LFOs moving exactly as the full path
    // would, so the output stays bit-identical and waking needs no handover.
    asleep_ = sleep_enabled_ && mix_ == 0.0f && send_ == 0.0f &&
        lp_1_ == 0.0f && lp_2_ == 0.0f && silent_samples_ >= E::kSize;
    if (asleep_) {
      engine_.Advance(size);
      while (size--) {
        // Step the glides one rounded add at a time, as the full path does:
        // without this barrier, -funsafe-math-optimizations lets the compiler
        // vectorize this short loop and reassociate the adds.
        asm volatile("" : : "r"(&dry_modulation), "r"(&wet_modulation),
                     "r"(&send_modulation) : "memory");
        const float dry_amount = dry_modulation.Next();
        wet_modulation.Next();
        send_modulation.Next();
        in_out->l *= dry_amount;
        in_out->r *= dry_amount;
        ++in_out;
      }
      return;
    }

    while (size--) {
      const float dry_amount = dry_modulation.Next();
      const float wet_amount = wet_modulation.Next();
      const float send_amount = send_modulation.Next();
      
      float wet;
      float apout = 0.0f;
      engine_.Start(&c);
      
      // Smear AP1 inside the loop.
      c.Interpolate(ap1, 10.0f, LFO_1, 80.0f * mod_depth, 1.0f);  // RobotBoy
      c.Write(ap1, 100, 0.0f);
      
      c.Read(in_out->l + in_out->r, send_amount);
      
      // Diffuse through 4 allpasses.
      c.Read(ap1 TAIL, kap);
      c.WriteAllPass(ap1, -kap);
      c.Read(ap2 TAIL, kap);
      c.WriteAllPass(ap2, -kap);
      c.Read(ap3 TAIL, kap);
      c.WriteAllPass(ap3, -kap);
      c.Read(ap4 TAIL, kap);
      c.WriteAllPass(ap4, -kap);
      c.Write(apout);
      
      // Main reverb loop.
      c.Load(apout);
      c.Interpolate(del2, 6261.0f, LFO_2, 50.0f * mod_depth, krt);  // RobotBoy
      c.Lp(lp_1_, klp);
      c.Read(dap1a TAIL, -kap);
      c.WriteAllPass(dap1a, kap);
      c.Read(dap1b TAIL, kap);
      c.WriteAllPass(dap1b, -kap);
      c.Write(del1, 1.0f);
      c.Write(wet);

      in_out->l = in_out->l * dry_amount + wet * wet_amount;

      c.Load(apout);
      c.Interpolate(del1, 4460.0f, LFO_1, 40.0f * mod_depth, krt);  // RobotBoy
      c.Lp(lp_2_, klp);
      c.Read(dap2a TAIL, kap);
      c.WriteAllPass(dap2a, -kap);
      c.Read(dap2b TAIL, -kap);
      c.WriteAllPass(dap2b, kap);
      c.Write(del2, 1.0f);
      c.Write(wet);
      
      in_out->r = in_out->r * dry_amount + wet * wet_amount;
      ++in_out;
      
      // RobotBoy: count the samples whose memory writes all truncated to 0
      // (FORMAT_16_BIT stores (int32_t)(x * 32768)).
      if (c.peak() * 32768.0f < 1.0f) {
        if (silent_samples_ < E::kSize) {
          ++silent_samples_;
        }
      } else {
        silent_samples_ = 0;
      }
    }
  }
  
 public:
  inline void set_mix(float mix) {
    mix_ = mix;
  }
  
  inline void set_time(float reverb_time) {
    reverb_time_ = reverb_time;
  }
  
  inline void set_lp(float lp) {
    lp_ = lp;
  }
  
  inline void Clear() {
    engine_.Clear();
    silent_samples_ = E::kSize;  // RobotBoy
  }
  
  // RobotBoy: Brume's controls. Defaults reproduce the stock reverb exactly.
  // set_lfo_rate scales the 0.5 and 0.3 Hz LFOs without restarting their
  // phase; set_mod_depth scales the delay-line modulation excursions.
  inline void set_diffusion(float diffusion) { diffusion_ = diffusion; }
  // Reset(sample_rate) restarts the LFOs at multiplier 1: re-apply the rate.
  inline void set_lfo_rate(float multiplier, float sample_rate) {
    engine_.SetLFOFrequencyKeepPhase(LFO_1, 0.5f * multiplier / sample_rate);
    engine_.SetLFOFrequencyKeepPhase(LFO_2, 0.3f * multiplier / sample_rate);
  }
  inline void set_mod_depth(float depth) { mod_depth_ = depth; }
  inline float lfo_value(int index) const {
    return engine_.lfo_value(index ? LFO_2 : LFO_1);
  }

  // RobotBoy: sleep can be turned off (for comparison tests); asleep() says
  // whether the last Process() skipped the tank.
  inline void set_sleep_enabled(bool enabled) { sleep_enabled_ = enabled; }
  inline bool asleep() const { return asleep_; }
  
 private:
  typedef FxEngine<32768, FORMAT_16_BIT> E;
  E engine_;
  
  float mix_;
  float wet_;
  float dry_;
  float send_;
  float reverb_time_;
  float lp_;
  
  float lp_1_;
  float lp_2_;
  
  float diffusion_;  // RobotBoy
  float mod_depth_;  // RobotBoy
  
  bool sleep_enabled_;  // RobotBoy
  bool asleep_;  // RobotBoy
  int32_t silent_samples_;  // RobotBoy
  
  DISALLOW_COPY_AND_ASSIGN(Reverb);
};

}  // namespace beads

#endif  // BEADS_DSP_FX_REVERB_H_
