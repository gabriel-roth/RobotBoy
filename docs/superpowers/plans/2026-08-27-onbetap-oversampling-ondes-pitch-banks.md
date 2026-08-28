# Onbetap Oversampling Menu + Ondes Pitch Knob + Bank Groups Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Three independent, user-requested tweaks: (1) Onbetap's Oversampling menu drops 1x and renames 2x/4x to "CPU efficient"/"high quality"; (2) Ondes' pitch knob loses its notch stretching and becomes a plain linear ±24 semitone knob; (3) Ondes gains a right-click menu to disable whole waveform-bank groups (Sines/Formants/Braids), with the remaining groups' banks spread across the knob's full range and at least one group always left enabled.

**Architecture:** Three self-contained tasks touching disjoint concerns. Task 1 is Onbetap-only (`src/Onbetap.cpp`). Tasks 2 and 3 both touch `src/particules/Ondes.cpp` (run Task 2 before Task 3 to avoid stacking unrelated diffs in the same file) but are logically independent: Task 2 changes only pitch-knob mapping, Task 3 changes only wavetable-bank selection. No task depends on another's code (only on edit ordering within Ondes.cpp).

**Tech Stack:** VCV Rack C++ plugin (also builds for MetaModule via `#if defined(METAMODULE)`), Jansson JSON for patch persistence, host-free `g++` plain-assert tests (Lane 1, `tests/run.sh`) and a vendored-Catch2/CMake suite (Lane 2, `tests/particules_dsp/run.sh`).

**Spec:** None — this plan was scoped directly from the user's request in conversation (no separate brainstorming spec doc; the three changes are small and bounded, and every open question below was resolved by reading the existing code rather than needing further user input). This document is self-contained.

## Global Constraints

- Do not modify `src/particules/pitch_notch_map.hpp` or the shared `PitchParamQuantity` in `src/plugin.hpp` — both are used by Particules' and Retours' pitch knobs (grep-verified: `src/particules/Particules.cpp`, `src/retours_delay/Retours.cpp`), which keep their notched knobs unchanged. Ondes gets its own separate linear mapping.
- `RackWavetableProvider` (`src/particules/RackWavetableProvider.hpp`) must stay default-constructible with all three bank groups enabled (`NumBanksAvailable() == 24`) — three existing Catch2 tests in `tests/particules_dsp/test_wavetable_oscillator.cpp` construct it with no arguments and rely on this.
- `particules_dsp::WavetableOscillator` caches `num_banks_`/`waveforms_per_bank_` inside `SetProvider()` (see `src/particules/dsp/src/wavetable/wavetable_oscillator.cpp:30-36`) and only recomputes them when `SetProvider()` is called again. Any code that changes which bank groups are enabled on a live module's `wavetable_provider_` MUST call `osc_.SetProvider(&wavetable_provider_)` again afterward, or the oscillator keeps using the stale bank count.
- No GUI-simulator or Rack-host tests are to be written or run by an agent. Manual verification of on-screen/knob behavior goes in the "User checklist" section at the end of this plan, for the user to run themselves.
- Follow existing repo conventions: `createBoolMenuItem`/`createIndexSubmenuItem` for context menus (see `src/loooop/Loooop.cpp:388-423`, `src/Onbetap.cpp:438-445`), plain-assert style for Lane 1 tests (see `tests/particules/test_pitch_notch_map.cpp`), Catch2 `TEST_CASE`/`REQUIRE` for Lane 2 (see `tests/particules_dsp/test_wavetable_oscillator.cpp`).

---

## Task 1: Onbetap — drop 1x, rename the oversampling menu

**Files:**
- Modify: `src/Onbetap.cpp:80-88` (default-oversample constant + comment)
- Modify: `src/Onbetap.cpp:233-292` (`processSide` — drop the 1x branch and the now-dead `needHp` parameter)
- Modify: `src/Onbetap.cpp:314-357` (`process()` — drop the now-dead `needHp` computation and its two call-site arguments)
- Modify: `src/Onbetap.cpp:367-386` (`dataToJson`/`dataFromJson` — migrate legacy `oversample:1` patches)
- Modify: `src/Onbetap.cpp:438-445` (context menu — rename, drop 1x)
- Modify: `mm-test-patches/RB-Onbetap-1.yml` (saved `oversample:1` → `2`)
- Modify: `mm-test-patches/RB-Onbetap-2.yml` (saved `oversample:1` → `2`)
- Modify: `mm-test-patches/TESTING.md` (RB-Onbetap-1/2/3 sections referencing the old 1x default)
- Modify: `CHANGELOG.md` (new `[Unreleased]` bullet)

**Interfaces:** None — this task is self-contained; nothing else in the codebase reads `Onbetap::oversample`.

**Context:** Reading the current code (`src/Onbetap.cpp:80-88`), Onbetap defaults to 1x oversampling on MetaModule (the Cortex-A7 has the least CPU headroom) and 2x on desktop, with a right-click "Oversampling" submenu offering `{"1x", "2x", "4x"}` (lines 438-445). `processSide` (lines 233-292) branches on `oversample == 4`, `== 2`, else (1x, no resampling). `dataFromJson` (line 382) accepts saved values of 1, 2, or 4, falling back to the host's default otherwise.

The MetaModule test patches `RB-Onbetap-1.yml` and `RB-Onbetap-2.yml` were deliberately baked at `"oversample":1` to test that exact MetaModule-default state (see `mm-test-patches/TESTING.md`'s "Tamed mode at 1× oversampling (baked state)" and "Vintage mode is baked in (drift ON, 1×)"), and `RB-Onbetap-3.yml`'s `TESTING.md` section frames itself as an A/B "against RB-Onbetap-1" specifically to decide whether to lock Onbetap to 4x like Vespid. That decision is effectively now made (drop 1x, keep 2x/4x as options) — this task updates those patches and the test doc so a hardware tester isn't sent to compare against a state (1x) that can no longer be reached from the menu.

- [ ] **Step 1: Update the default-oversample constant and comment**

Replace (`src/Onbetap.cpp:80-88`):

```cpp
	// Oversampling default: 1x on MetaModule, where the Cortex-A7 core needs the
	// headroom; 2x on desktop. The 1x/2x/4x menu is available on both hosts, and
	// a patch that saved a factor keeps it.
#if defined(METAMODULE)
	static constexpr int kDefaultOversample = 1;
#else
	static constexpr int kDefaultOversample = 2;
#endif
	int oversample = kDefaultOversample;            // 1 / 2 / 4
```

with:

```cpp
	// Oversampling default: 2x ("CPU efficient") on both hosts. 1x used to be
	// the MetaModule default (the Cortex-A7 core has the least headroom) but
	// has been removed as a menu option entirely -- 2x has enough headroom on
	// MetaModule now (see docs/superpowers/plans/2026-07-24-cpu-optimization.md),
	// so the menu is just CPU efficient (2x) / high quality (4x) on both hosts.
	// A patch saved at the old oversample:1 loads at 2x (dataFromJson below).
	static constexpr int kDefaultOversample = 2;
	int oversample = kDefaultOversample;            // 2 / 4
```

- [ ] **Step 2: Drop the 1x branch in `processSide`, and the `needHp` parameter it was the only consumer of**

`needHp` (a `processSide` parameter) exists solely to tell the removed 1x branch whether it could skip computing the `hp` tap (`auto o = flt.processG(x1, g, kEff, needHp);`, the 4-arg overload of `OnbetapFilter::processG`, `src/onbetap/OnbetapFilter.hpp:150`). The 2x and 4x branches always call the 3-arg form (`flt.processG(x, g, kEff)`, which defaults `needHp` to `true` inside `OnbetapFilter` itself — no change needed there), so once the 1x branch is gone, `needHp` has no reader anywhere in `Onbetap.cpp`. Remove it end to end: the parameter, its computation in `process()`, and both call sites.

Replace (`src/Onbetap.cpp:233-292`, the whole method) with the same method minus the 1x branch and the `needHp` parameter. Before:

```cpp
	// One stereo side through the oversampled core. Returns output volts.
	// fir{Lp,Bp,Hp} run on the 2x and 4x paths (on 4x as stage B behind the
	// fir4* stage-A decimators); the 1x path bypasses them entirely.
	float processSide(OnbetapFilter& flt, float& xPrev, DCBlock& dc, float inVolts,
	                  float g, float kEff, float driveScale, float makeup, float push,
	                  bool needHp,
	                  DecimFir13& firLp, DecimFir13& firBp, DecimFir13& firHp,
	                  DecimFir9& fir4Lp, DecimFir9& fir4Bp, DecimFir9& fir4Hp) {
		float lp = 0, bp = 0, hp = 0;
		float x1 = inVolts * driveScale;
		if (oversample == 4) {
			// 4x: two-stage decimation — fir4* (DecimFir9, 192k→96k) feeds
			// the same DecimFir13 stage the 2x path uses (96k→48k), so the
			// 4x passband matches 2x by construction. See engine.hpp for
			// the folding-band math and design provenance. The stage-A FIRs
			// must see every substep, but their output is only consumed on
			// even substeps, so odd substeps advance history without the
			// 9-tap MAC (pushHistory — cpu-optimization doc §4.4).
			for (int i = 1; i <= 4; i++) {
				float t = (float)i * 0.25f;
				float x = xPrev + (x1 - xPrev) * t;  // linear interp upsample
				auto o = flt.processG(x, g, kEff);
				if ((i & 1) == 0) {                  // 96k instants: substeps 2, 4
					float fl = firLp.push(fir4Lp.push(o.lp));
					float fb = firBp.push(fir4Bp.push(o.bp));
					float fh = firHp.push(fir4Hp.push(o.hp));
					if (i == 4) { lp = fl; bp = fb; hp = fh; }
				} else {
					fir4Lp.pushHistory(o.lp);
					fir4Bp.pushHistory(o.bp);
					fir4Hp.pushHistory(o.hp);
				}
			}
		} else if (oversample == 2) {
			// 2x: 13-tap decimation FIR (see engine.hpp DecimFir13) replaces
			// the crude 2-tap boxcar average, which under-attenuates the
			// alias band and both droops the top octave and lets content
			// above the new Nyquist fold back down (measured, Task 5).
			for (int i = 1; i <= 2; i++) {
				float t = (float)i * 0.5f;
				float x = xPrev + (x1 - xPrev) * t;  // linear interp upsample
				auto o = flt.processG(x, g, kEff);
				float fl = firLp.push(o.lp);
				float fb = firBp.push(o.bp);
				float fh = firHp.push(o.hp);
				if (i == 2) { lp = fl; bp = fb; hp = fh; }  // decimate: keep 1 of 2
			}
		} else {
			// 1x (the MetaModule default): no resampling — the old generic
			// fall-through interpolated toward t = 1 and averaged over one
			// sample, i.e. two runtime divides and arithmetic to reproduce
			// the input (doc §4.2). needHp only gates the tap here: at 1x
			// skipping it is exactly equivalent (hp is not filter state);
			// at 2x/4x the decimation FIRs must keep seeing hp, so those
			// paths always compute it.
			auto o = flt.processG(x1, g, kEff, needHp);
			lp = o.lp; bp = o.bp; hp = o.hp;
		}
		xPrev = x1;   // still tracked at 1x so an oversampling switch
		              // interpolates from the right previous sample
```

After:

```cpp
	// One stereo side through the oversampled core. Returns output volts.
	// fir{Lp,Bp,Hp} run on both paths (on 4x as stage B behind the fir4*
	// stage-A decimators).
	float processSide(OnbetapFilter& flt, float& xPrev, DCBlock& dc, float inVolts,
	                  float g, float kEff, float driveScale, float makeup, float push,
	                  DecimFir13& firLp, DecimFir13& firBp, DecimFir13& firHp,
	                  DecimFir9& fir4Lp, DecimFir9& fir4Bp, DecimFir9& fir4Hp) {
		float lp = 0, bp = 0, hp = 0;
		float x1 = inVolts * driveScale;
		if (oversample == 4) {
			// 4x: two-stage decimation — fir4* (DecimFir9, 192k→96k) feeds
			// the same DecimFir13 stage the 2x path uses (96k→48k), so the
			// 4x passband matches 2x by construction. See engine.hpp for
			// the folding-band math and design provenance. The stage-A FIRs
			// must see every substep, but their output is only consumed on
			// even substeps, so odd substeps advance history without the
			// 9-tap MAC (pushHistory — cpu-optimization doc §4.4).
			for (int i = 1; i <= 4; i++) {
				float t = (float)i * 0.25f;
				float x = xPrev + (x1 - xPrev) * t;  // linear interp upsample
				auto o = flt.processG(x, g, kEff);
				if ((i & 1) == 0) {                  // 96k instants: substeps 2, 4
					float fl = firLp.push(fir4Lp.push(o.lp));
					float fb = firBp.push(fir4Bp.push(o.bp));
					float fh = firHp.push(fir4Hp.push(o.hp));
					if (i == 4) { lp = fl; bp = fb; hp = fh; }
				} else {
					fir4Lp.pushHistory(o.lp);
					fir4Bp.pushHistory(o.bp);
					fir4Hp.pushHistory(o.hp);
				}
			}
		} else {
			// 2x (the only other option, and the default on both hosts):
			// 13-tap decimation FIR (see engine.hpp DecimFir13) replaces
			// the crude 2-tap boxcar average, which under-attenuates the
			// alias band and both droops the top octave and lets content
			// above the new Nyquist fold back down (measured, Task 5).
			for (int i = 1; i <= 2; i++) {
				float t = (float)i * 0.5f;
				float x = xPrev + (x1 - xPrev) * t;  // linear interp upsample
				auto o = flt.processG(x, g, kEff);
				float fl = firLp.push(o.lp);
				float fb = firBp.push(o.bp);
				float fh = firHp.push(o.hp);
				if (i == 2) { lp = fl; bp = fb; hp = fh; }  // decimate: keep 1 of 2
			}
		}
		xPrev = x1;
```

Now remove `needHp`'s computation and both call sites in `process()`. Before (`src/Onbetap.cpp:325-357`):

```cpp
		bool rConnected = inputs[AUDIO_INPUT_R].isConnected();

		// HP tap gate (§4.6/§7.2, conservative): only the 1x path may skip
		// the tap, and only when neither the crossfade target nor — while a
		// crossfade is still running — its source reads hp. Vintage switches
		// modes hard, so only the target matters there. Modes 2/3/4
		// (HP/notch/peak) read hp.
		auto usesHp = [](int m) { return m >= 2; };
		bool needHp = oversample != 1 || usesHp(modeTarget)
		              || (modeXf < 1.f && !vintageDrift && usesHp(modeCurrent));

		for (int c = 0; c < voices; c++) {
			OnbetapVoice& v = pool.voices[c];
			float g      = v.gSlew.process(v.gTarget);
			float kBase  = v.kSlew.process(v.kTarget);
			float drive  = v.driveSlew.process(v.driveTarget);
			float makeup = v.makeupSlew.process(v.makeupTarget);
			float push   = v.pushSlew.process(v.pushTarget);
			float kEff   = std::max(kBase, -0.31f);  // denominator guard floor

			float inL = inputs[AUDIO_INPUT].getPolyVoltage(c) + dither;
			float outL = processSide(v.fL, v.xPrevL, v.dcL, inL, g, kEff, drive, makeup,
			                         push, needHp, v.firLpL, v.firBpL, v.firHpL,
			                         v.fir4LpL, v.fir4BpL, v.fir4HpL);
			outputs[AUDIO_OUTPUT].setVoltage(outL, c);

			if (rConnected) {
				float inR = inputs[AUDIO_INPUT_R].getPolyVoltage(c) + dither;
				float outR = processSide(v.fR, v.xPrevR, v.dcR, inR,
				                         g * v.fRgRatio, kEff, drive, makeup, push,
				                         needHp, v.firLpR, v.firBpR, v.firHpR,
				                         v.fir4LpR, v.fir4BpR, v.fir4HpR);
				outputs[AUDIO_OUTPUT_R].setVoltage(outR, c);
```

After:

```cpp
		bool rConnected = inputs[AUDIO_INPUT_R].isConnected();

		for (int c = 0; c < voices; c++) {
			OnbetapVoice& v = pool.voices[c];
			float g      = v.gSlew.process(v.gTarget);
			float kBase  = v.kSlew.process(v.kTarget);
			float drive  = v.driveSlew.process(v.driveTarget);
			float makeup = v.makeupSlew.process(v.makeupTarget);
			float push   = v.pushSlew.process(v.pushTarget);
			float kEff   = std::max(kBase, -0.31f);  // denominator guard floor

			float inL = inputs[AUDIO_INPUT].getPolyVoltage(c) + dither;
			float outL = processSide(v.fL, v.xPrevL, v.dcL, inL, g, kEff, drive, makeup,
			                         push, v.firLpL, v.firBpL, v.firHpL,
			                         v.fir4LpL, v.fir4BpL, v.fir4HpL);
			outputs[AUDIO_OUTPUT].setVoltage(outL, c);

			if (rConnected) {
				float inR = inputs[AUDIO_INPUT_R].getPolyVoltage(c) + dither;
				float outR = processSide(v.fR, v.xPrevR, v.dcR, inR,
				                         g * v.fRgRatio, kEff, drive, makeup, push,
				                         v.firLpR, v.firBpR, v.firHpR,
				                         v.fir4LpR, v.fir4BpR, v.fir4HpR);
				outputs[AUDIO_OUTPUT_R].setVoltage(outR, c);
```

(the `modeTarget`/`modeCurrent`/`vintageDrift`/`usesHp` logic that fed `needHp` had no other reader — `usesHp` was a local lambda defined only for this computation — so nothing else needs to change; the rest of the `for` loop body, the `else` branch that mirrors L to R, and the closing braces are unchanged.)

After this step, grep `needHp` in `src/Onbetap.cpp` to confirm zero remaining matches (it should now only exist as `OnbetapFilter::processG`'s unrelated default parameter in `src/onbetap/OnbetapFilter.hpp`, which this task does not touch).

- [ ] **Step 3: Migrate legacy `oversample:1` patches on load**

Replace (`src/Onbetap.cpp:378-383`):

```cpp
		// "limitMode" from old patches is deliberately ignored (hardwired Soft).
		json_t* os = json_object_get(root, "oversample");
		if (os) {
			int v = (int)json_integer_value(os);
			oversample = (v == 1 || v == 2 || v == 4) ? v : kDefaultOversample;
		}
```

with:

```cpp
		// "limitMode" from old patches is deliberately ignored (hardwired Soft).
		json_t* os = json_object_get(root, "oversample");
		if (os) {
			// oversample:1 was a valid value before 1x was removed from the
			// menu; a patch saved at 1x now loads at the 2x default instead
			// of silently keeping an unreachable-from-the-menu value.
			int v = (int)json_integer_value(os);
			oversample = (v == 2 || v == 4) ? v : kDefaultOversample;
		}
```

- [ ] **Step 4: Rename the menu and drop the 1x entry**

Replace (`src/Onbetap.cpp:438-445`):

```cpp
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Oversampling",
			{"1x", "2x", "4x"},
			[m]() { return m->oversample == 1 ? 0 : m->oversample == 2 ? 1 : 2; },
			[m](int i) {
				m->oversample = (i == 0) ? 1 : (i == 1) ? 2 : 4;
				m->pool.resetAll();
			}));
```

with:

```cpp
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Oversampling",
			{"CPU efficient", "high quality"},
			[m]() { return m->oversample == 4 ? 1 : 0; },
			[m](int i) {
				m->oversample = (i == 1) ? 4 : 2;
				m->pool.resetAll();
			}));
```

- [ ] **Step 5: Build and re-run the existing Onbetap DSP test suite**

Run: `make -C vcv -j`
Expected: clean build, no warnings about unused `needHp` or unreachable code.

Run: `cd tests && ./run.sh`
Expected: `tests/onbetap/test_onbetap.cpp` still passes (it exercises `OnbetapFilter` directly, not `oversample`/`processSide`, so it's unaffected by this task — this run is a regression guard, not new coverage).

- [ ] **Step 6: Update the two MetaModule test patches baked at `oversample:1`**

In `mm-test-patches/RB-Onbetap-1.yml`, change:

```
        {"vintageDrift":false,"oversample":1}
```

to:

```
        {"vintageDrift":false,"oversample":2}
```

In `mm-test-patches/RB-Onbetap-2.yml`, change:

```
        {"vintageDrift":true,"oversample":1}
```

to:

```
        {"vintageDrift":true,"oversample":2}
```

(`mm-test-patches/RB-Onbetap-3.yml`'s `"oversample":4` is untouched.)

- [ ] **Step 7: Update `mm-test-patches/TESTING.md`'s Onbetap sections**

In the `## RB-Onbetap-1` section, change:

```
**Setup:** Saw VCO at C2 into the Onbetap through a unity-gain VCA (knob v = Input level, loaded at full), Tamed mode at 1× oversampling (baked state). Cutoff starts \~400 Hz, Q 60%, Drive 20%, LP mode, all CV amounts full. Filter output on Out 1/Out 2.
```

to:

```
**Setup:** Saw VCO at C2 into the Onbetap through a unity-gain VCA (knob v = Input level, loaded at full), Tamed mode at 2× oversampling ("CPU efficient", the baked default on both hosts). Cutoff starts \~400 Hz, Q 60%, Drive 20%, LP mode, all CV amounts full. Filter output on Out 1/Out 2.
```

In the `## RB-Onbetap-2` section, change:

```
**Setup:** Two saw VCOs — C3 left, C3 +7 cents right — feed the Onbetap's L/R inputs (true stereo through one filter). Vintage mode is baked in (drift ON, 1×). A slow sine LFO (0.1 Hz, ±1.5 V) is wired to Cutoff CV internally with Cutoff CV Amt at +30%. Cutoff \~750 Hz, Q 50%, Drive 30%, LP mode. Output on Out 1/Out 2 — listen in stereo.
```

to:

```
**Setup:** Two saw VCOs — C3 left, C3 +7 cents right — feed the Onbetap's L/R inputs (true stereo through one filter). Vintage mode is baked in (drift ON, 2× "CPU efficient"). A slow sine LFO (0.1 Hz, ±1.5 V) is wired to Cutoff CV internally with Cutoff CV Amt at +30%. Cutoff \~750 Hz, Q 50%, Drive 30%, LP mode. Output on Out 1/Out 2 — listen in stereo.
```

Replace the entire `## RB-Onbetap-3` section:

```
## RB-Onbetap-3 — 4× oversampling (Onbetap, aliasing A/B against RB-Onbetap-1)

**Setup:** Same layout as RB-Onbetap-1 (saw VCO at C2, Tamed mode) but baked at 4× oversampling with Drive at 80% and Cutoff high (\~70%, several kHz) — a deliberately hot, bright setting. RB-Onbetap-1 runs the MetaModule default 1×; to A/B, set patch 1's knobs C to 80% and A to 70% to match. Output on Out 1/Out 2.
**Panel:** identical to RB-Onbetap-1 (A Cutoff · B Q · C Drive · D Mode · E Cutoff CV Amt · F Q CV Amt · u Drive CV Amt · v Input level · In 1 Cutoff CV · In 2 Q CV · In 3 Drive CV · In 4 VCO Pitch).

**This patch is an A/B against RB-Onbetap-1**, which needs its knobs moved to match (C 80%, A 70%) since it loads at gentler settings. Do that from a fresh reload of patch 1 each time, so the only difference between the two is oversampling.

**Try:**

*Block 1 — the aliasing A/B · this patch freshly loaded and untouched; RB-Onbetap-1 freshly loaded with C to 80% and A to 70%*

1. In RB-Onbetap-1 (1×, knobs matched as above), play a rising line into In 4 over 2-3 octaves — **Expect:** inharmonic aliasing "birdies" under the distortion, faint whistles that sweep DOWN as you play UP.
2. Reload THIS patch and play the same rising line, knobs untouched — **Expect:** clean at 4×, no counter-sweeping birdies; the distortion harmonics all move up with the notes.
3. Push it harder here: C to max, sweep A through the top of its range while holding a high note — **Expect:** still no birdies; the harshest setting available stays harmonically well-behaved.

**Reset:** reload the patch — the CPU figures below must come from the loaded settings, not from step 3's maxed Drive.

*Block 2 — the CPU numbers · from a freshly loaded patch, and a freshly loaded RB-Onbetap-1 with C 80% / A 70%*

4. Watch the device CPU meter with this patch at its loaded positions, then do the same in RB-Onbetap-1 — **Expect:** headroom acceptable at 4×. **Write down both figures.** We're considering locking Onbetap to 4× the way Vespid is, and this pair of numbers is the deciding data point.
5. For each patch, catch the worst case rather than the idle figure: wiggle A and B and modulate In 1 while watching the meter — **Expect:** the peak reading is higher than the resting one; record the peak.
```

with:

```
## RB-Onbetap-3 — 4× oversampling / high quality (Onbetap, aliasing A/B against RB-Onbetap-1)

**Setup:** Same layout as RB-Onbetap-1 (saw VCO at C2, Tamed mode) but baked at 4× oversampling ("high quality") with Drive at 80% and Cutoff high (\~70%, several kHz) — a deliberately hot, bright setting. RB-Onbetap-1 runs the default 2× ("CPU efficient"); to A/B, set patch 1's knobs C to 80% and A to 70% to match. Output on Out 1/Out 2.
**Panel:** identical to RB-Onbetap-1 (A Cutoff · B Q · C Drive · D Mode · E Cutoff CV Amt · F Q CV Amt · u Drive CV Amt · v Input level · In 1 Cutoff CV · In 2 Q CV · In 3 Drive CV · In 4 VCO Pitch).

**This patch is an A/B against RB-Onbetap-1**, which needs its knobs moved to match (C 80%, A 70%) since it loads at gentler settings. Do that from a fresh reload of patch 1 each time, so the only difference between the two is oversampling.

**Try:**

*Block 1 — the aliasing A/B · this patch freshly loaded and untouched; RB-Onbetap-1 freshly loaded with C to 80% and A to 70%*

1. In RB-Onbetap-1 (2× "CPU efficient", knobs matched as above), play a rising line into In 4 over 2-3 octaves — **Expect:** inharmonic aliasing "birdies" under the distortion, faint whistles that sweep DOWN as you play UP.
2. Reload THIS patch and play the same rising line, knobs untouched — **Expect:** clean at 4× "high quality", no counter-sweeping birdies; the distortion harmonics all move up with the notes.
3. Push it harder here: C to max, sweep A through the top of its range while holding a high note — **Expect:** still no birdies; the harshest setting available stays harmonically well-behaved.

**Reset:** reload the patch — the CPU figures below must come from the loaded settings, not from step 3's maxed Drive.

*Block 2 — the CPU numbers · from a freshly loaded patch, and a freshly loaded RB-Onbetap-1 with C 80% / A 70%*

4. Watch the device CPU meter with this patch at its loaded positions, then do the same in RB-Onbetap-1 — **Expect:** headroom acceptable at 4×, and better still at 2× — the 1x option that used to trade audio quality for headroom is gone, since 2× alone now has enough headroom on MetaModule. **Write down both figures** as the current CPU-efficient/high-quality baseline.
5. For each patch, catch the worst case rather than the idle figure: wiggle A and B and modulate In 1 while watching the meter — **Expect:** the peak reading is higher than the resting one; record the peak.
```

- [ ] **Step 8: Add a CHANGELOG entry**

In `CHANGELOG.md`, add this bullet to the top of the `## [Unreleased]` list (do not edit the pre-existing "Vespid & Onbetap on MetaModule" bullet further down — it documents an earlier, now-superseded state; changelog entries are historical, not living docs):

```markdown
- **Onbetap** — the Oversampling menu drops the **1x** option; it's now **CPU efficient** (2x) / **high quality** (4x), and 2x is the default on both VCV Rack and MetaModule (previously MetaModule defaulted to 1x). A patch saved with the old 1x setting now opens at 2x.
```

- [ ] **Step 9: Commit**

```bash
git add src/Onbetap.cpp mm-test-patches/RB-Onbetap-1.yml mm-test-patches/RB-Onbetap-2.yml mm-test-patches/TESTING.md CHANGELOG.md
git commit -m "Onbetap: drop 1x oversampling, rename menu to CPU efficient / high quality"
```

---

## Task 2: Ondes — replace the notched pitch knob with a plain linear one

**Files:**
- Create: `src/particules/ondes_pitch_map.hpp`
- Create: `tests/particules/test_ondes_pitch_map.cpp`
- Modify: `src/particules/Ondes.cpp:1-58` (pitch knob config + `process()`)
- Modify: `CHANGELOG.md`

**Interfaces:**
- Produces: `ondesKnobToSemitones(float t) -> float` and `ondesSemitonesToKnob(float st) -> float` in `src/particules/ondes_pitch_map.hpp` — linear, knob `t` in `[0,1]` maps to `[-24, 24]` semitones, `t=0.5` is 0 semitones. Used by `Ondes.cpp`'s `OndesPitchParamQuantity` and by `Ondes::process()`.

**Context:** `src/particules/Ondes.cpp` currently configures `PITCH_PARAM` with the shared `PitchParamQuantity` (`src/plugin.hpp:28-37`) and calls the shared `pitchKnobToSemitones()` (`src/particules/pitch_notch_map.hpp`) in `process()`, both of which apply a piecewise-linear taper that gives 3x the knob travel to zones around ±19/±12/±7/0 semitones (making those landmarks easy to dial in by feel). That taper is also used by Particules and Retours (`Global Constraints` above) and must not change for them. Ondes gets a private, linear replacement with the same `[-24, 24]` semitone range and the same center-detent-at-zero behavior, just without the stretched zones.

- [ ] **Step 1: Write the failing test**

Create `tests/particules/test_ondes_pitch_map.cpp`:

```cpp
#include <cassert>
#include <cmath>
#include <cstdio>
#include "../../src/particules/ondes_pitch_map.hpp"

static bool approx(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) < eps;
}

int main() {
    // Endpoint mapping, same range as the notched map it replaces.
    assert(approx(ondesKnobToSemitones(0.0f), -24.0f));
    assert(approx(ondesKnobToSemitones(1.0f),  24.0f));
    assert(approx(ondesKnobToSemitones(0.5f),   0.0f));

    // Round-trip through the inverse at several points, including the old
    // notch landmarks (they're unremarkable now, but should still round-trip).
    for (float st : {-24.f, -19.f, -12.f, -7.f, 0.f, 7.f, 12.f, 19.f, 24.f}) {
        float roundtrip = ondesKnobToSemitones(ondesSemitonesToKnob(st));
        assert(approx(roundtrip, st));
    }

    // Linear: equal knob steps produce equal semitone steps everywhere,
    // including across where the old notch zones used to be widened.
    float stepAtOldNotch = ondesKnobToSemitones(0.55f) - ondesKnobToSemitones(0.45f);
    float stepElsewhere   = ondesKnobToSemitones(0.90f) - ondesKnobToSemitones(0.80f);
    assert(approx(stepAtOldNotch, stepElsewhere));

    // Strictly monotonic.
    float prev = ondesKnobToSemitones(0.0f);
    for (int i = 1; i <= 200; ++i) {
        float cur = ondesKnobToSemitones(i / 200.0f);
        assert(cur > prev);
        prev = cur;
    }

    // Out-of-range knob positions clamp instead of extrapolating.
    assert(approx(ondesKnobToSemitones(-1.f), -24.0f));
    assert(approx(ondesKnobToSemitones(2.f),   24.0f));

    printf("All tests passed.\n");
    return 0;
}
```

- [ ] **Step 2: Run it to confirm it fails to build (the header doesn't exist yet)**

Run: `cd tests && ./run.sh`
Expected: FAIL — `g++` error, `ondes_pitch_map.hpp: No such file or directory`.

- [ ] **Step 3: Implement `ondes_pitch_map.hpp`**

Create `src/particules/ondes_pitch_map.hpp`:

```cpp
#pragma once
#include <algorithm>

// Plain linear knob-to-semitone mapping for Ondes' pitch knob. Unlike
// Particules'/Retours' pitchKnobToSemitones (pitch_notch_map.hpp), Ondes has
// no notch stretching around octave/fifth/unison landmarks -- turning the
// knob moves pitch at a constant rate across the full +-24 semitone span.
inline float ondesKnobToSemitones(float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    return (t - 0.5f) * 48.0f;
}

inline float ondesSemitonesToKnob(float st) {
    st = std::max(-24.0f, std::min(24.0f, st));
    return st / 48.0f + 0.5f;
}
```

- [ ] **Step 4: Run the test to confirm it passes**

Run: `cd tests && ./run.sh`
Expected: `== building particules/test_ondes_pitch_map.cpp ==` followed by `All tests passed.` and the script exits 0.

- [ ] **Step 5: Wire the linear map into `Ondes.cpp`**

Replace (`src/particules/Ondes.cpp:1-46`, the includes through the constructor):

```cpp
#include "plugin.hpp"
#include "RackWavetableProvider.hpp"
#include "WavetableFrame.hpp"
#include "dsp/src/wavetable/wavetable_oscillator.h"
#include <cmath>

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

    particules_dsp::WavetableOscillator osc_;
    RackWavetableProvider wavetable_provider_;

    // Pitch knob cache: pitchKnobToSemitones() is a linear search; skip when unchanged.
    float cached_pitch_knob_      = -999.f;
    float cached_pitch_semitones_ = 0.f;

    // Last post-CV bank/wave (0-1), read by the panel display. UI-thread read of
    // an audio-thread write is a benign race (display only), as in Fundamental.
    float lastBank = 0.f;
    float lastWave = 0.f;

    Ondes() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam<PitchParamQuantity>(PITCH_PARAM, 0.f, 1.f, 0.5f, "Pitch");
        configParam(POSITION_PARAM, 0.f, 1.f, 0.5f, "Position");
        configParam(POSITION_AMT_PARAM, -1.f, 1.f, 0.f, "Position CV amount");
        configParam(BANK_PARAM, 0.f, 1.f, 0.f, "Bank");
        configParam(BANK_AMT_PARAM, -1.f, 1.f, 0.f, "Bank CV amount");
        configInput(VOCT_INPUT, "Pitch (V/oct)");
        configInput(POSITION_INPUT, "Position CV");
        configInput(BANK_INPUT, "Bank CV");
        configOutput(OUT_OUTPUT, "Audio out");

        osc_.Init(APP->engine->getSampleRate());
        osc_.SetProvider(&wavetable_provider_);
    }
```

with:

```cpp
#include "plugin.hpp"
#include "RackWavetableProvider.hpp"
#include "WavetableFrame.hpp"
#include "ondes_pitch_map.hpp"
#include "dsp/src/wavetable/wavetable_oscillator.h"
#include <cmath>

// Ondes' own pitch-knob quantity: plain linear +-24 st (see
// ondes_pitch_map.hpp), independent of the shared notched PitchParamQuantity
// in plugin.hpp that Particules/Retours use.
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

    particules_dsp::WavetableOscillator osc_;
    RackWavetableProvider wavetable_provider_;

    // Last post-CV bank/wave (0-1), read by the panel display. UI-thread read of
    // an audio-thread write is a benign race (display only), as in Fundamental.
    float lastBank = 0.f;
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

        osc_.Init(APP->engine->getSampleRate());
        osc_.SetProvider(&wavetable_provider_);
    }
```

(`cached_pitch_knob_`/`cached_pitch_semitones_` are dropped: they existed only to skip `pitchKnobToSemitones()`'s linear search over notch segments; the linear formula is O(1), so the cache no longer earns its keep.)

Replace (`src/particules/Ondes.cpp:53-59`, the top of `process()`):

```cpp
    void process(const ProcessArgs& args) override {
        float raw_pitch = params[PITCH_PARAM].getValue();
        if (raw_pitch != cached_pitch_knob_) {
            cached_pitch_knob_      = raw_pitch;
            cached_pitch_semitones_ = pitchKnobToSemitones(raw_pitch);
        }
        float pitch = cached_pitch_semitones_ + inputs[VOCT_INPUT].getVoltage() * 12.f;
```

with:

```cpp
    void process(const ProcessArgs& args) override {
        float pitch = ondesKnobToSemitones(params[PITCH_PARAM].getValue())
                    + inputs[VOCT_INPUT].getVoltage() * 12.f;
```

- [ ] **Step 6: Build**

Run: `make -C vcv -j`
Expected: clean build. (`PitchParamQuantity` from `plugin.hpp` is no longer referenced by `Ondes.cpp`, but stays defined there for Particules/Retours — confirm no "unused" warnings, which shouldn't appear since it's still used by those two files.)

- [ ] **Step 7: Re-run both test lanes**

Run: `cd tests && ./run.sh`
Expected: all pass, including the new `test_ondes_pitch_map.cpp` and the untouched `test_pitch_notch_map.cpp`.

Run: `./tests/particules_dsp/run.sh`
Expected: all pass (unaffected by this task; regression guard).

- [ ] **Step 8: Add a CHANGELOG entry**

In `CHANGELOG.md`, add to the top of `## [Unreleased]`:

```markdown
- **Ondes** — the Pitch knob is now a plain linear +-24 semitone control. It no longer stretches extra knob travel around octave/fifth/unison landmarks the way it used to (and the way Particules' and Retours' pitch knobs still do).
```

- [ ] **Step 9: Commit**

```bash
git add src/particules/ondes_pitch_map.hpp tests/particules/test_ondes_pitch_map.cpp src/particules/Ondes.cpp CHANGELOG.md
git commit -m "Ondes: replace notched pitch knob with a plain linear one"
```

---

## Task 3: Ondes — disable waveform bank groups from the context menu

**Files:**
- Modify: `src/particules/RackWavetableProvider.hpp` (bank-group filtering)
- Create: `tests/particules_dsp/test_wavetable_provider_bank_groups.cpp`
- Modify: `src/particules/Ondes.cpp` (menu, JSON persistence, display)
- Modify: `CHANGELOG.md`

**Interfaces:**
- Consumes: nothing from Task 1 or 2.
- Produces (on `RackWavetableProvider`, all public, no allocation, safe to call from the audio thread): `static constexpr int kNumBankGroups = 3;` · `bool isGroupEnabled(int group) const` · `bool canDisableGroup(int group) const` (false only when `group` is currently enabled and is the last one) · `void setGroupEnabled(int group, bool enabled)` (no-op if it would disable the last enabled group). `group` is `0` = sines (banks 0-7), `1` = formants (banks 8-15), `2` = Braids (banks 16-23) — see `src/particules/WavetableData.hpp:8-11`'s own grouping comment. `GetWaveform()`/`NumBanksAvailable()` (already-existing `particules_dsp::WavetableProvider` overrides) now report only the enabled groups' banks, contiguously, in group order.

**Context:** `WavetableData.hpp` documents its own 24-bank layout as three groups of 8 (`Banks 0-7: mild/additive`, `Banks 8-15: formantish`, `Banks 16-23: Braids imports`) — exactly the "1 - sines / 2 - formants / 3 - Braids" split requested. `RackWavetableProvider` (`src/particules/RackWavetableProvider.hpp`) is the seam between that raw data and both the audio-thread oscillator (`particules_dsp::WavetableOscillator`, which asks it for `NumBanksAvailable()`/`GetWaveform(bank, index)`) and the UI-thread waveform-trace display (`robotboy::wavetableFrameSample()` in `WavetableFrame.hpp`, which takes the same provider interface). Filtering bank groups at this one seam means both audio and display pick up the change automatically, with no duplicated logic.

`particules_dsp::WavetableOscillator::SetProvider()` snapshots `NumBanksAvailable()`/`WaveformsPerBank()` into member fields and only re-reads them when called again (`Global Constraints` above) — so every place that changes group-enabled state on the module's live `wavetable_provider_` must re-call `osc_.SetProvider(&wavetable_provider_)` afterward.

- [ ] **Step 1: Write the failing test**

Create `tests/particules_dsp/test_wavetable_provider_bank_groups.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "RackWavetableProvider.hpp"
#include "WavetableData.hpp"

TEST_CASE("RackWavetableProvider: all bank groups enabled by default", "[wavetable][bank-groups]") {
    RackWavetableProvider provider;
    REQUIRE(provider.NumBanksAvailable() == 24);
    for (int g = 0; g < RackWavetableProvider::kNumBankGroups; ++g)
        REQUIRE(provider.isGroupEnabled(g));
}

TEST_CASE("RackWavetableProvider: disabling a group removes its banks and shrinks the count", "[wavetable][bank-groups]") {
    RackWavetableProvider provider;
    provider.setGroupEnabled(1, false);  // formants
    REQUIRE_FALSE(provider.isGroupEnabled(1));
    REQUIRE(provider.NumBanksAvailable() == 16);
}

TEST_CASE("RackWavetableProvider: remaining banks are spread contiguously across the logical range", "[wavetable][bank-groups]") {
    RackWavetableProvider provider;
    provider.setGroupEnabled(1, false);  // sines(0) + Braids(2) remain, 8 banks each
    REQUIRE(provider.NumBanksAvailable() == 16);
    REQUIRE(provider.GetWaveform(0, 0) == WavetableData::kData[0][0]);   // first sines bank
    REQUIRE(provider.GetWaveform(7, 0) == WavetableData::kData[7][0]);   // last sines bank
    REQUIRE(provider.GetWaveform(8, 0) == WavetableData::kData[16][0]);  // first Braids bank
    REQUIRE(provider.GetWaveform(15, 0) == WavetableData::kData[23][0]); // last Braids bank
}

TEST_CASE("RackWavetableProvider: disabling two groups leaves the third spanning the full range", "[wavetable][bank-groups]") {
    RackWavetableProvider provider;
    provider.setGroupEnabled(0, false);
    provider.setGroupEnabled(1, false);
    REQUIRE(provider.NumBanksAvailable() == 8);
    REQUIRE(provider.GetWaveform(0, 0) == WavetableData::kData[16][0]);
    REQUIRE(provider.GetWaveform(7, 0) == WavetableData::kData[23][0]);
}

TEST_CASE("RackWavetableProvider: cannot disable the only enabled group", "[wavetable][bank-groups]") {
    RackWavetableProvider provider;
    provider.setGroupEnabled(0, false);
    provider.setGroupEnabled(1, false);
    REQUIRE(provider.NumBanksAvailable() == 8);
    REQUIRE_FALSE(provider.canDisableGroup(2));
    provider.setGroupEnabled(2, false);  // must be refused (no-op)
    REQUIRE(provider.isGroupEnabled(2));
    REQUIRE(provider.NumBanksAvailable() == 8);
}

TEST_CASE("RackWavetableProvider: re-enabling a group restores its banks", "[wavetable][bank-groups]") {
    RackWavetableProvider provider;
    provider.setGroupEnabled(0, false);
    REQUIRE(provider.NumBanksAvailable() == 16);
    provider.setGroupEnabled(0, true);
    REQUIRE(provider.NumBanksAvailable() == 24);
    REQUIRE(provider.GetWaveform(0, 0) == WavetableData::kData[0][0]);
}
```

- [ ] **Step 2: Run it to confirm it fails to build**

Run: `./tests/particules_dsp/run.sh`
Expected: FAIL — compile error, `RackWavetableProvider` has no members `isGroupEnabled`/`setGroupEnabled`/`canDisableGroup`/`kNumBankGroups`.

- [ ] **Step 3: Implement bank-group filtering in `RackWavetableProvider.hpp`**

Replace the whole file `src/particules/RackWavetableProvider.hpp`:

```cpp
#pragma once

#include "WavetableData.hpp"
#include "particules_dsp/types.h"

// Implements particules_dsp::WavetableProvider using the Plaits-derived data in
// WavetableData.hpp: 24 banks x 8 waveforms x 256 samples, grouped into three
// user-facing sets of 8 banks each (WavetableData.hpp's own grouping comment):
// group 0 = sines (banks 0-7), group 1 = formants (banks 8-15), group 2 =
// Braids imports (banks 16-23). Any group can be disabled from Ondes' context
// menu; the logical bank range GetWaveform()/NumBanksAvailable() expose then
// covers only the enabled groups, so the knob's full [0,1] range always spans
// whatever remains -- no dead zone at a disabled group's old position.
struct RackWavetableProvider : particules_dsp::WavetableProvider {
    static constexpr int kNumBankGroups = 3;
    static constexpr int kBanksPerGroup =
        WavetableData::kNumWavetableBanks / kNumBankGroups;
    static_assert(WavetableData::kNumWavetableBanks % kNumBankGroups == 0,
                  "bank groups must divide the bank count evenly");

    const float* GetWaveform(int bank, int index) const override {
        int physical = physicalBankIndex(bank);
        return physical >= 0 ? WavetableData::kData[physical][index] : nullptr;
    }
    int NumBanksAvailable() const override {
        int n = 0;
        for (int g = 0; g < kNumBankGroups; ++g)
            if (groupEnabled_[g]) n += kBanksPerGroup;
        return n;
    }
    int WaveformsPerBank() const override {
        return WavetableData::kWaveformsPerBank;
    }

    bool isGroupEnabled(int group) const { return groupEnabled_[group]; }

    // False only when `group` is currently enabled and is the last one --
    // disabling it would leave zero banks reachable.
    bool canDisableGroup(int group) const {
        if (!groupEnabled_[group]) return true;
        int enabledCount = 0;
        for (int g = 0; g < kNumBankGroups; ++g)
            if (groupEnabled_[g]) enabledCount++;
        return enabledCount > 1;
    }

    // No-op if disabling `group` would leave none enabled.
    void setGroupEnabled(int group, bool enabled) {
        if (!enabled && !canDisableGroup(group)) return;
        groupEnabled_[group] = enabled;
    }

private:
    // Maps a logical bank index (as seen by GetWaveform/NumBanksAvailable) to
    // its physical WavetableData bank, skipping disabled groups in
    // sines -> formants -> Braids order. Returns -1 if out of range.
    int physicalBankIndex(int logicalBank) const {
        int remaining = logicalBank;
        for (int g = 0; g < kNumBankGroups; ++g) {
            if (!groupEnabled_[g]) continue;
            if (remaining < kBanksPerGroup) return g * kBanksPerGroup + remaining;
            remaining -= kBanksPerGroup;
        }
        return -1;
    }

    bool groupEnabled_[kNumBankGroups] = {true, true, true};
};
```

- [ ] **Step 4: Run the test to confirm it passes**

Run: `./tests/particules_dsp/run.sh`
Expected: all `particules_dsp_tests` cases pass, including the six new `[bank-groups]` ones and the three pre-existing `RackWavetableProvider` cases in `test_wavetable_oscillator.cpp` (still true — default-constructed provider is unfiltered).

- [ ] **Step 5: Add the context menu, JSON persistence, and fix the display's provider**

Add file-scope bank-group names above `struct Ondes` in `src/particules/Ondes.cpp` (after the `OndesPitchParamQuantity` struct added in Task 2, before `struct Ondes : Module`):

```cpp
static constexpr const char* kBankGroupNames[RackWavetableProvider::kNumBankGroups] = {
    "1 - Sines", "2 - Formants", "3 - Braids"
};
```

Add `dataToJson`/`dataFromJson` to `struct Ondes` (after `process()`, before the closing brace of the struct):

```cpp
    json_t* dataToJson() override {
        json_t* root = json_object();
        json_t* groups = json_array();
        for (int g = 0; g < RackWavetableProvider::kNumBankGroups; ++g)
            json_array_append_new(groups, json_boolean(wavetable_provider_.isGroupEnabled(g)));
        json_object_set_new(root, "bankGroupsEnabled", groups);
        return root;
    }

    void dataFromJson(json_t* root) override {
        json_t* groups = json_object_get(root, "bankGroupsEnabled");
        if (groups && json_is_array(groups)) {
            int n = (int)json_array_size(groups);
            for (int g = 0; g < RackWavetableProvider::kNumBankGroups && g < n; ++g) {
                json_t* v = json_array_get(groups, g);
                wavetable_provider_.setGroupEnabled(g, json_boolean_value(v));
            }
            osc_.SetProvider(&wavetable_provider_);
        }
    }
```

In `WavetableDisplay::drawLayer` (`src/particules/Ondes.cpp`, the `struct WavetableDisplay` in Task 2's file), replace the fresh throwaway provider with the module's real one, so the trace reflects the current bank-group selection instead of always drawing all 24 banks:

Before:

```cpp
            float bank = module ? module->lastBank : 0.f;
            float wave = module ? module->lastWave : 0.f;
            RackWavetableProvider provider;
```

After:

```cpp
            float bank = module ? module->lastBank : 0.f;
            float wave = module ? module->lastWave : 0.f;
            static const RackWavetableProvider kFallbackProvider;  // module==nullptr: browser thumbnail
            const RackWavetableProvider& provider =
                module ? module->wavetable_provider_ : kFallbackProvider;
```

(the rest of `drawLayer`'s loop body, which reads `robotboy::wavetableFrameSample(provider, bank, wave, i % n)`, is unchanged — `provider` is now a reference instead of a local value, and the call site doesn't care).

Add `appendContextMenu` to `struct OndesWidget` (`src/particules/Ondes.cpp`, after the constructor, before the closing brace):

```cpp
    void appendContextMenu(Menu* menu) override {
        auto* m = dynamic_cast<Ondes*>(module);
        if (!m) return;

        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Waveform banks"));
        for (int g = 0; g < RackWavetableProvider::kNumBankGroups; ++g) {
            menu->addChild(createBoolMenuItem(kBankGroupNames[g], "",
                [m, g] { return m->wavetable_provider_.isGroupEnabled(g); },
                [m, g](bool v) {
                    m->wavetable_provider_.setGroupEnabled(g, v);
                    m->osc_.SetProvider(&m->wavetable_provider_);
                }));
        }
    }
```

- [ ] **Step 6: Build**

Run: `make -C vcv -j`
Expected: clean build.

- [ ] **Step 7: Re-run both test lanes**

Run: `cd tests && ./run.sh`
Expected: all pass (unaffected; regression guard).

Run: `./tests/particules_dsp/run.sh`
Expected: all pass, including Step 4's new cases.

- [ ] **Step 8: Add a CHANGELOG entry**

In `CHANGELOG.md`, add to the top of `## [Unreleased]`:

```markdown
- **Ondes** — the right-click menu can now disable whole waveform-bank groups (**1 - Sines**, **2 - Formants**, **3 - Braids**). Disabling a group removes its banks from the Bank knob's range and spreads the remaining groups across the full knob travel; at least one group must stay enabled.
```

- [ ] **Step 9: Commit**

```bash
git add src/particules/RackWavetableProvider.hpp tests/particules_dsp/test_wavetable_provider_bank_groups.cpp src/particules/Ondes.cpp CHANGELOG.md
git commit -m "Ondes: add right-click menu to disable waveform bank groups"
```

---

## User checklist (manual, GUI-simulator / by-ear — not for an agent to run)

- [ ] Onbetap: right-click menu shows "Oversampling" with exactly two entries, "CPU efficient" and "high quality" (no "1x"); confirm the correct one is checked after selecting each.
- [ ] Onbetap: load `mm-test-patches/RB-Onbetap-1.yml` and `RB-Onbetap-2.yml` on MetaModule (or the sim) and confirm they sound as described in the updated `TESTING.md` sections (2x default, no audible regression from the old 1x-baked versions).
- [ ] Ondes: turn the Pitch knob slowly end-to-end and confirm it feels like a normal linear knob — no zones where it "sticks"/moves slower, unlike Particules' or Retours' Pitch knob (compare side by side).
- [ ] Ondes: right-click menu shows "Waveform banks" with "1 - Sines", "2 - Formants", "3 - Braids", all checked by default.
- [ ] Ondes: uncheck "2 - Formants" and sweep the Bank knob — confirm the waveform trace and the sound only ever show sines/Braids timbres, spread across the full knob range (no dead zone in the middle).
- [ ] Ondes: with only one group enabled, confirm clicking it again in the menu does nothing (stays checked, can't get down to zero groups).
- [ ] Ondes: save a patch with a non-default bank-group selection, reload VCV Rack (or the patch), confirm the selection persisted.
