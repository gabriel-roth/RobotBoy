# Three Filters: MF-20, Onbetap & Vespid

Robot Boy includes three filters, and none of them is polite. Each one recreates a famous analog filter with a reputation for misbehaving — a Japanese screamer, a Soviet brute, and a British punk built out of the wrong parts. What follows is a guide to what makes each one distinctive and how to get the most out of it, written for players rather than circuit designers. (MF-20 and Onbetap also have their own control-by-control technical references: [MF-20](MF20.md), [Onbetap](Onbetap.md).)

All three are stereo and polyphonic, and run on both [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). They're modeled from the behavior of the original circuits — the way they distort, ring, and lose their composure — not from a generic filter with a distortion effect bolted on. That's the whole point: the character *is* the filter.

A quick word on **resonance**. Every one of these filters can be pushed until it starts ringing, and then until it "self-oscillates" — sings a pure tone all by itself, with no input at all. At that point the filter becomes a sine-ish oscillator you can play. What separates these three from a clean modern filter is how they *get* there and how they behave once they arrive: politely, ferociously, or unpredictably.

---

## MF-20 — the Korg MS-20 filter

<img src="screenshots/MF-20.png" alt="MF-20 module" height="360">

### The original

The **Korg MS-20** (1978) is one of the most-sampled, most-cloned synthesizers ever made — a chunky, patchable semi-modular that turned up on countless records and, decades later, in half the softsynth libraries in existence. A lot of its fame comes down to its filter, which is unusually aggressive: crank the resonance and it doesn't just emphasize a frequency, it *screams*, distorting and howling in a way that cuts through any mix.

There's a wrinkle in the MS-20 story that matters here. Korg built the filter two different ways over the instrument's life. Early units (1978–79) used a Korg-made chip called the **Korg-35**; later units switched to a more common chip, the **LM13600**. Players have argued ever since about which sounds better. The early Korg-35 version is generally described as angrier and rawer; the later chip is smoother, especially when the resonance is maxed out. MF-20 gives you **both**, switchable in the right-click menu:

- **OTA** — the later revision. Smooth and open-sounding.
- **Korg35** — the original. Edgier, with a grittier distortion character.

### What makes it distinctive

Two things. First, the MS-20's filter is actually **two filters in series** — a high-pass *then* a low-pass — which is how you get its trademark band-pass and notch sounds by sweeping them against each other. On MF-20 the two cutoffs have their own knobs, but you can also patch the **Total** input to sweep them together the way the hardware's shared modulation does, opening and closing a band.

Second, that resonance. Both modes self-oscillate cleanly at maximum Peak, so you can play the MS-20 filter as a raw, slightly dirty sine oscillator. Add **Drive** and the whole thing saturates, adding the harmonic grind the MS-20 is loved for. Where this filter shines: acid basslines, screaming leads, and aggressive filter sweeps that stay musical even when they're distorting.

---

## Onbetap — the Formanta Polivoks filter

### The original

The **Polivoks** (1982) is the most famous synthesizer ever to come out of the Soviet Union. Designed by engineer Vladimir Kuzmin at the Formanta radio factory — with an industrial, tank-like look styled by his wife Olimpiada after Soviet military radios — it was built in the tens of thousands for the domestic market and almost unknown in the West until long after the USSR was gone. (Kuzmin died in June 2026.)

It has a well-earned reputation as one of the most savage-sounding filters ever built. Where the MS-20 screams, the Polivoks *snarls*. Its resonance is extreme and slightly unstable, its distortion is thick and buzzy, and at the top of the resonance range it can lurch into wild, unpredictable self-oscillation. Part of the reason is a genuinely strange circuit: the Polivoks filter is built from Soviet op-amp chips run in a way they were never meant to be, with **no capacitors** in the usual place at all. That unorthodox design is a big part of why it sounds like nothing else.

### What makes it distinctive

Onbetap models that circuit's behavior directly, and a few of its quirks are worth knowing because they're the opposite of what most filters do:

- **Drive fights resonance.** On almost any other filter, pushing the input harder makes the resonance ring louder. On the Polivoks it's the reverse: **a loud signal rings *less* than a quiet one at the same resonance setting.** So Drive isn't just a distortion knob — it's a second, inverse control over how much the filter sings. Play softly and it howls; play hard and it thickens and chokes the ring. This interplay is the heart of the Polivoks sound.
- **The resonance point moves.** Self-oscillation kicks in earlier (lower on the knob) when the cutoff is high than when it's low, so the filter feels different at different pitches — livelier and more on-edge up top.
- **It can turn suddenly harsh.** At maximum resonance the self-oscillation can drop into a lower, harsher, buzzier tone than you'd expect — the same "suddenly nasty" surprise real units are known for.

Onbetap offers five filter modes (**Lowpass, Bandpass, Highpass, Notch, Peak** — the first two are what the hardware actually had), and a **Character** switch in the menu:

- **Tamed** — a clean, well-behaved, calibrated version of the circuit. Predictable and stable.
- **Vintage** — adds the imperfections of a real, aging hardware unit: the tuning drifts slowly, the two stereo channels don't quite match, and fast cutoff moves produce an audible thump — exactly like the factory panel switch on the original. It's seeded, so a given patch always drifts the same way each time you load it.

Where this filter shines: brutal basses, distorted leads, and anything that wants to sound a little dangerous. Automate Drive against a steady note and let the resonance duck and swell.

---

## Vespid — the EDP Wasp filter

### The original

The **Wasp** (1978) was made by **Electronic Dream Plant**, a tiny British company run by musician Adrian Wagner and engineer Chris Huggett. It's a legend of make-do design: cheap, lightweight, housed in a black-and-yellow plastic case (hence the name) with a flat touch-plate keyboard and a built-in speaker, and priced to undercut everything else on the market. It became a cult favorite and turned up with the likes of Devo and the Eurythmics.

What makes the Wasp's filter famous is *how* it was built. To save money, EDP used cheap **digital logic chips** — the kind meant for on/off switching — and "abused" them as analog amplifiers, running them in a way they were never designed for. The result is a filter full of grit, buzz, and dirty edges: it distorts in an ugly-in-a-good-way manner that's completely its own. It's been cloned many times over; the best-known clone is **Doepfer's A-124** Eurorack module, which is where a lot of modular players first met the sound.

### What makes it distinctive

The Wasp filter is a **multimode** filter, and Vespid gives you all its outputs at once — **low-pass, band-pass, and high-pass** — plus a **Mix** output with its own knob and CV that crossfades smoothly from low-pass, through a notch, to high-pass. So you can pull three different flavors out of one filter simultaneously, or morph between low and high on a single control.

The other thing to know is its edge. The original Wasp sits right at the *verge* of self-oscillation — enough to whistle and chirp, but it never quite runs away. That restrained-but-nervous quality is part of its charm. Doepfer's clone added a mod that pushes it over the edge into full self-oscillation. Vespid gives you both, as a **Character** switch in the menu:

- **Tame** — the original 1978 Wasp. Rides the edge of oscillation for whistles and chirps, but stays under control.
- **Screaming** — the Doepfer A-124's self-oscillation mod. Crosses into a full, singing self-oscillation, bounded by the circuit's limits.

There's also a menu option that tunes how eagerly the filter tips into oscillation, and one that chooses between the hardware's slightly-off oscillation pitch and a corrected, playable-in-tune version — a nice touch if you want to use the self-oscillation as a voice. **Freq** tracks 1 V/octave, and Freq, resonance, and **Drive** all take CV.

Where this filter shines: dirty, buzzy timbres, gnarly percussion, and morphing multimode sweeps. Reach for the Mix output when you want to sweep from thump to sizzle in one move.

---

## Which one?

| | Character | Signature move |
|---|---|---|
| **MF-20** | Screaming, aggressive, two-filters-in-series | Sweep HP and LP against each other; switch Korg35 vs OTA for raw vs smooth |
| **Onbetap** | Snarling, unstable, brutal | Ride Drive against a held note — the resonance ducks as you push harder |
| **Vespid** | Dirty, buzzy, on-edge | Pull LP/BP/HP at once, or morph the Mix output notch |

None of these is the filter to reach for when you want something clean and invisible — that's not what they're for. Reach for them when you want the filter itself to be part of the sound.
