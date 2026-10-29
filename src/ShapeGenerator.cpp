#include "ShapeGenerator.hpp"

namespace shapeui {

static const float kSlewMs[] = {0.f, 2.f, 5.f, 20.f};
static const int kPulseChoices[] = {1, 2, 4, 8, 16, 24, 32, 48, 96};

void Settings::toJson(json_t* rootJ) const {
	json_object_set_new(rootJ, "range", json_integer((int) range));
	json_object_set_new(rootJ, "treat", json_integer((int) treatMode));
	json_object_set_new(rootJ, "slewMs", json_real(slewMs));
	json_object_set_new(rootJ, "pulsesPerCycle", json_integer(pulsesPerCycle));
}

void Settings::fromJson(json_t* rootJ) {
	json_t* j = json_object_get(rootJ, "range");
	range = j && json_integer_value(j) == 1 ? shapes::Range::Unipolar10 : shapes::Range::Bipolar5;
	j = json_object_get(rootJ, "treat");
	int t = j ? (int) json_integer_value(j) : 0;
	treatMode = t >= 0 && t <= (int) shapes::Treat::Wrap ? (shapes::Treat) t : shapes::Treat::Fit;
	j = json_object_get(rootJ, "slewMs");
	float ms = j ? (float) json_number_value(j) : 0.f;
	slewMs = 0.f;
	for (float choice : kSlewMs)
		if (ms == choice)
			slewMs = choice;
	j = json_object_get(rootJ, "pulsesPerCycle");
	pulsesPerCycle = validPulses(j ? (int) json_integer_value(j) : 4);
}

int validPulses(int ppc) {
	for (int choice : kPulseChoices)
		if (ppc == choice)
			return choice;
	return 4;
}

MenuItem* createPulsesMenuItem(Module* module, int* ppc) {
	return createIndexSubmenuItem("Clock pulses per cycle",
		{"1", "2", "4", "8", "16", "24", "32", "48", "96"},
		[=]() {
			for (int i = 0; i < 9; i++)
				if (*ppc == kPulseChoices[i])
					return i;
			return 2;
		},
		[=](int i) {
			withMenuUndo(module, "set clock pulses per cycle", [=]() { *ppc = kPulseChoices[i]; });
		});
}

void appendSettingsMenu(Menu* menu, Module* module, Settings* s) {
	menu->addChild(createIndexSubmenuItem("Output range", {"+-5 V", "0-10 V"},
		[=]() { return (int) s->range; },
		[=](int i) {
			withMenuUndo(module, "set output range", [=]() { s->range = (shapes::Range) i; });
		}));
	menu->addChild(createIndexSubmenuItem("When out of range", {"Fit", "Clip", "Soft clip", "Fold", "Wrap"},
		[=]() { return (int) s->treatMode; },
		[=](int i) {
			withMenuUndo(module, "set out-of-range mode", [=]() { s->treatMode = (shapes::Treat) i; });
		}));
	menu->addChild(createIndexSubmenuItem("Slew", {"Off", "2 ms", "5 ms", "20 ms"},
		[=]() {
			for (int i = 0; i < 4; i++)
				if (s->slewMs == kSlewMs[i])
					return i;
			return 0;
		},
		[=](int i) {
			withMenuUndo(module, "set slew", [=]() { s->slewMs = kSlewMs[i]; });
		}));
	menu->addChild(createPulsesMenuItem(module, &s->pulsesPerCycle));
}

// Tempo in 0.01 BPM steps, so a typed 121.35 holds. Rounded where it's
// read, not as it's set, so slow knob drags still creep along.
struct BpmQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		return string::f("%.2f", bpmOf(getValue()));
	}
};

double bpmOf(float value) {
	return std::round(clamp(value, kMinBpm, kMaxBpm) * 100.0) / 100.0;
}

ParamQuantity* configBpm(Module* module, int paramId) {
	ParamQuantity* q = module->configParam<BpmQuantity>(paramId, kMinBpm, kMaxBpm, 120.f, "Tempo", " BPM");
	q->randomizeEnabled = false;
	return q;
}

std::shared_ptr<window::Font> loadFont() {
	return APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
}

void Screen::draw(const DrawArgs& args) {
	nvgBeginPath(args.vg);
	nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, mm2px(radiusMm));
	nvgFillColor(args.vg, backgroundColor());
	nvgFill(args.vg);
}

void Screen::drawLayer(const DrawArgs& args, int layer) {
	if (layer == 1) {
		nvgScissor(args.vg, 0, 0, box.size.x, box.size.y);
		drawScreen(args.vg);
		nvgResetScissor(args.vg);
	}
	Widget::drawLayer(args, layer);
}

void drawGrid(NVGcontext* vg, float x, float y, float w, float h) {
	for (int i = 0; i <= 4; i++) {
		float ly = y + h * i / 4.f;
		nvgBeginPath(vg);
		nvgMoveTo(vg, x, ly);
		nvgLineTo(vg, x + w, ly);
		nvgStrokeWidth(vg, mm2px(i == 2 ? 0.25f : 0.12f));
		nvgStrokeColor(vg, gridColor());
		nvgStroke(vg);
	}
}

void drawTrace(NVGcontext* vg, const float* values, int n, float x, float y, float w, float h, const TraceStyle& style) {
	if (n < 2)
		return;
	int points = style.closed ? n + 1 : n;
	int span = style.closed ? n : n - 1;
	auto pointAt = [&](int k) {
		float v = values[k % n];
		if (!std::isfinite(v))
			v = 0.f;
		v = clamp(v, -style.limit, style.limit);
		return Vec(x + w * k / span, y + h * 0.5f - v * h * 0.5f);
	};

	nvgBeginPath(vg);
	if (style.dashMm <= 0.f) {
		for (int k = 0; k < points; k++) {
			Vec p = pointAt(k);
			if (k == 0 && style.leadIn && !style.closed) {
				nvgMoveTo(vg, p.x, pointAt(n - 1).y);
				nvgLineTo(vg, p.x, p.y);
			}
			else if (k == 0)
				nvgMoveTo(vg, p.x, p.y);
			else
				nvgLineTo(vg, p.x, p.y);
		}
	}
	else {
		// nanovg has no dashes: walk the polyline, drawing only the dash parts
		float dash = mm2px(style.dashMm), gap = mm2px(style.gapMm);
		bool on = true;
		float left = dash;  // of the current dash or gap
		Vec a = pointAt(0);
		nvgMoveTo(vg, a.x, a.y);
		for (int k = 1; k < points; k++) {
			Vec b = pointAt(k);
			float len = b.minus(a).norm();
			float done = 0.f;
			while (len - done > left) {
				done += left;
				Vec p = a.plus(b.minus(a).mult(done / len));
				if (on)
					nvgLineTo(vg, p.x, p.y);
				else
					nvgMoveTo(vg, p.x, p.y);
				on = !on;
				left = on ? dash : gap;
			}
			left -= len - done;
			if (on)
				nvgLineTo(vg, b.x, b.y);
			a = b;
		}
	}
	nvgLineJoin(vg, NVG_ROUND);
	nvgStrokeWidth(vg, mm2px(style.widthMm));
	nvgStrokeColor(vg, style.color);
	nvgStroke(vg);
}

float traceAt(const float* values, int n, float pos, bool closed) {
	if (!closed) {
		float k = clamp(pos, 0.f, 1.f) * (n - 1);
		int i0 = std::min((int) k, n - 2);
		return values[i0] + (values[i0 + 1] - values[i0]) * (k - i0);
	}
	float k = clamp(pos, 0.f, 1.f) * n;
	int i0 = std::min((int) k, n - 1);
	return values[i0] + (values[(i0 + 1) % n] - values[i0]) * (k - i0);
}

void drawPlayhead(NVGcontext* vg, float x, float y, float w, float h, float pos, float value) {
	if (!std::isfinite(pos))
		pos = 0.f;
	pos = clamp(pos, 0.f, 1.f);
	if (!std::isfinite(value))
		value = 0.f;
	float hx = x + w * pos;
	nvgBeginPath(vg);
	nvgMoveTo(vg, hx, y);
	nvgLineTo(vg, hx, y + h);
	nvgStrokeWidth(vg, mm2px(0.15f));
	nvgStrokeColor(vg, nvgRGBA(0xe6, 0xe8, 0xeb, 153));
	nvgStroke(vg);
	nvgBeginPath(vg);
	nvgCircle(vg, hx, y + h * 0.5f - clamp(value, -1.f, 1.f) * h * 0.5f, mm2px(0.7f));
	nvgFillColor(vg, signalColor());
	nvgFill(vg);
}

}  // namespace shapeui
