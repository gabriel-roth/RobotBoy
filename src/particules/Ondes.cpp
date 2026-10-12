#include "plugin.hpp"
#include "ondes_pitch_map.hpp"

#include "stmlib/dsp/dsp.h"
#include "beads/resources.h"
#include "beads/dsp/frame.h"
#include "beads/dsp/granular_player.h"
#include "beads/dsp/wavetable_grain.h"   // table layout + InterpolateWaveHermite
#include "beads/dsp/wavetable_voice.h"
#include "stmlib/dsp/hysteresis_quantizer.h"

#include <algorithm>
#include <cmath>

// Ondes is the wavetable voice of Mutable Instruments Beads (the oscillator
// Beads plays when nothing is patched into its audio inputs), running on the
// Beads source: 12 banks of 16 band-limited waves, with Position morphing
// through a bank and Bank switching banks via a short crossfade.

static constexpr int kOndesNumBanks = 12;  // beads/dsp/synth.h kNumBanks
static_assert(WAV_INTEGRATED_WAVES_SIZE ==
	kOndesNumBanks * beads::kNumWavesPerTable * (beads::kTableSize + 4),
	"Beads wavetable layout");

// Ondes keeps VCV's 0 V = C4 tuning; Beads' voice is rooted on C3.
static constexpr float kOndesTuningSemitones = 12.f;
// Beads' Digital (16-bit) setting; Ondes has no Quality control.
static constexpr int kOndesBitShift = 16;

// Ondes' own pitch-knob quantity: plain linear +-24 st (see
// ondes_pitch_map.hpp), independent of the notched PitchParamQuantity
// that Particules/Retours use.
struct OndesPitchParamQuantity : ParamQuantity {
    float getDisplayValue() override { return ondesKnobToSemitones(getValue()); }
    void setDisplayValue(float semitones) override { setValue(ondesSemitonesToKnob(semitones)); }
    std::string getDisplayValueString() override {
        float st = getDisplayValue();
        if (std::fabs(st - std::round(st)) < 0.05f) return string::f("%d", (int)std::round(st));
        return string::f("%.1f", st);
    }
    std::string getUnit() override { return " st"; }
};

struct Ondes : Module {
    enum ParamId {
        PITCH_PARAM,
        POSITION_PARAM,
        POSITION_AMT_PARAM,
        BANK_PARAM,
        BANK_AMT_PARAM,
        PARAMS_LEN
    };
    enum InputId { VOCT_INPUT, BANK_INPUT, POSITION_INPUT, INPUTS_LEN };
    enum OutputId { OUT_OUTPUT, OUTPUTS_LEN };
    enum LightId { LIGHTS_LEN };

    beads::WavetableVoice voice_;
    stmlib::HysteresisQuantizer2 bank_quantizer_;
    beads::Parameters parameters_;

    // Last bank (0-11) and position (0-1), read by the panel display. A
    // UI-thread read of an audio-thread write is a benign race (display only).
    int lastBank = 0;
    float lastWave = 0.f;

    Ondes() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam<OndesPitchParamQuantity>(PITCH_PARAM, 0.f, 1.f, 0.5f, "Pitch");
        configParam(POSITION_PARAM, 0.f, 1.f, 0.5f, "Position");
        configParam(POSITION_AMT_PARAM, -1.f, 1.f, 0.f, "Position CV amount");
        configParam(BANK_PARAM, 0.f, 1.f, 0.f, "Bank");
        configParam(BANK_AMT_PARAM, -1.f, 1.f, 0.f, "Bank CV amount");
        configInput(VOCT_INPUT, "Pitch (V/oct)");
        configInput(POSITION_INPUT, "Position CV");
        configInput(BANK_INPUT, "Bank CV");
        configOutput(OUT_OUTPUT, "Audio out");

        // Position and pitch are computed here, so the voice sees plain
        // values: no internal randomization, no CV path of its own.
        auto plain = [](beads::RandomizedParameter* r) {
            r->base = 0.f;
            r->attenuverter = 0.f;
            r->cv = 0.f;
            r->has_cv = false;
        };
        plain(&parameters_.time);
        plain(&parameters_.size);
        plain(&parameters_.shape);
        plain(&parameters_.pitch);
        parameters_.monophonic_mode = false;
        parameters_.pitch_lock = nullptr;

        Init(APP->engine->getSampleRate());
    }

    void Init(float sample_rate) {
        voice_.Init(sample_rate);
        // Beads' synth: 12 banks, 0.048 of hysteresis between them.
        bank_quantizer_.Init(kOndesNumBanks, 0.048f, false);
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        Init(e.sampleRate);
    }

    void process(const ProcessArgs& args) override {
        float pitch = ondesKnobToSemitones(params[PITCH_PARAM].getValue())
                    + inputs[VOCT_INPUT].getVoltage() * 12.f
                    + kOndesTuningSemitones;
        // Beads' SemitonesToRatio table covers +-128 semitones.
        pitch = std::isfinite(pitch) ? clamp(pitch, -120.f, 120.f) : 0.f;
        float position = params[POSITION_PARAM].getValue()
            + inputs[POSITION_INPUT].getVoltage() * 0.2f * params[POSITION_AMT_PARAM].getValue();
        position = std::isfinite(position) ? clamp(position, 0.f, 1.f) : 0.f;
        float bank = params[BANK_PARAM].getValue()
            + inputs[BANK_INPUT].getVoltage() * 0.2f * params[BANK_AMT_PARAM].getValue();
        bank = std::isfinite(bank) ? clamp(bank, 0.f, 1.f) : 0.f;

        parameters_.pitch.base = pitch;
        parameters_.time.base = position;
        const int bank_index = bank_quantizer_.Process(bank);
        lastBank = bank_index;
        lastWave = position;

        beads::FloatFrame out;
        out.l = out.r = 0.f;
        const bool freeze = false;
        voice_.Render(bank_index, kOndesBitShift, &freeze, parameters_, &out, 1);
        outputs[OUT_OUTPUT].setVoltage(out.l * 5.f);
    }
};

// One cycle of the wave the voice is playing: Beads stores each wave
// integrated, so the shape is the table's first difference.
static float ondesWaveSample(int bank, float position, int i) {
    float wave = clamp(position, 0.f, 1.f) * float(beads::kNumWavesPerTable - 1);
    int wave_integral = std::min(int(wave), int(beads::kNumWavesPerTable) - 2);
    float wave_fractional = wave - float(wave_integral);
    const int16_t* a = beads::wav_integrated_waves
        + (bank * beads::kNumWavesPerTable + wave_integral) * (beads::kTableSize + 4);
    const int16_t* b = a + beads::kTableSize + 4;
    float da = float(a[i + 2]) - float(a[i + 1]);
    float db = float(b[i + 2]) - float(b[i + 1]);
    return da + (db - da) * wave_fractional;
}

// Draws the current wave as a cyan trace that morphs as Bank/Position (and
// their CV) change. Bare trace, no text.
struct WavetableDisplay : LedDisplay {
    Ondes* module = nullptr;

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer == 1) {
            nvgScissor(args.vg, RECT_ARGS(args.clipBox));

            const int bank = module ? clamp(module->lastBank, 0, kOndesNumBanks - 1) : 0;
            const float wave = module ? module->lastWave : 0.f;

            const int n = beads::kTableSize;
            float samples[beads::kTableSize];
            float peak = 1e-6f;
            for (int i = 0; i < n; ++i) {
                samples[i] = ondesWaveSample(bank, wave, i);
                peak = std::max(peak, std::fabs(samples[i]));
            }

            Rect scope = Rect(Vec(0, 0), box.size).shrink(Vec(4, 5));
            nvgBeginPath(args.vg);
            for (int i = 0; i <= n; ++i) {
                Vec p;
                p.x = float(i) / n;
                p.y = 0.5f - 0.5f * samples[i % n] / peak;
                p = scope.pos + scope.size.mult(p);
                if (i == 0) nvgMoveTo(args.vg, VEC_ARGS(p));
                else        nvgLineTo(args.vg, VEC_ARGS(p));
            }
            nvgLineCap(args.vg, NVG_ROUND);
            nvgMiterLimit(args.vg, 2.f);
            nvgStrokeWidth(args.vg, 1.5f);
            nvgStrokeColor(args.vg, nvgRGB(0x00, 0xe5, 0xff));  // cyan
            nvgStroke(args.vg);

            nvgResetScissor(args.vg);
        }
        LedDisplay::drawLayer(args, layer);
    }
};

struct OndesWidget : ModuleWidget {
    OndesWidget(Ondes* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Ondes.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(14.145, 51.35)), module, Ondes::PITCH_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(11.66, 74.35)),  module, Ondes::BANK_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(28.98, 74.35)),  module, Ondes::POSITION_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(11.66, 97.05)),         module, Ondes::BANK_AMT_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(28.98, 97.05)),         module, Ondes::POSITION_AMT_PARAM));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(26.495, 51.35)), module, Ondes::VOCT_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.66, 86.7)),   module, Ondes::BANK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(28.98, 86.7)),   module, Ondes::POSITION_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(20.32, 113.7)), module, Ondes::OUT_OUTPUT));

        WavetableDisplay* display = createWidget<WavetableDisplay>(mm2px(Vec(3.0, 9.4)));
        display->box.size = mm2px(Vec(34.64, 24.5));
        display->module = module;
        addChild(display);
    }
};

Model* modelOndes = createModel<Ondes, OndesWidget>("Ondes");
