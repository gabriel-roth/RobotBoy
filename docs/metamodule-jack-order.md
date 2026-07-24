# MetaModule jack order (per module)

Jacks as they currently appear on the MetaModule roller, inputs then outputs.

- **Native modules (Loooop, Löp):** order = the `QlpJackIn` / `QlpJackOut`
  element order in `metamodule/loooop/*_info.hh`.
- **Adapter modules (MF-20, Onbetap, Particules, Ondes, Vespid, Retours):**
  order = the VCV `InputId` / `OutputId` enums; labels = `configInput` /
  `configOutput` text.

Reflects branch `metamodule-param-order` as of 2026-07-24.

---

## Loooop  *(native)*

Inputs:

1. In L
2. In R
3. Record Trigger
4. Clear Trigger
5. Dry/Wet CV
6. Red Size CV
7. Red Pos CV
8. Red Speed CV
9. Red Jitter CV
10. Red Pan CV
11. Red Level CV
12. Red Trig
13. Red Jump
14. Yellow Size CV
15. Yellow Pos CV
16. Yellow Speed CV
17. Yellow Jitter CV
18. Yellow Pan CV
19. Yellow Level CV
20. Yellow Trig
21. Yellow Jump
22. Blue Size CV
23. Blue Pos CV
24. Blue Speed CV
25. Blue Jitter CV
26. Blue Pan CV
27. Blue Level CV
28. Blue Trig
29. Blue Jump
30. Purple Size CV
31. Purple Pos CV
32. Purple Speed CV
33. Purple Jitter CV
34. Purple Pan CV
35. Purple Level CV
36. Purple Trig
37. Purple Jump

Outputs:

1. Mix L
2. Mix R
3. Red Out L
4. Red Out R
5. Yellow Out L
6. Yellow Out R
7. Blue Out L
8. Blue Out R
9. Purple Out L
10. Purple Out R

## Löp  *(native)*

Inputs:

1. In L
2. In R
3. Size CV
4. Position CV
5. Speed CV
6. Jitter CV
7. Trig
8. Jump
9. Dry/Wet CV
10. Record Trigger
11. Clear Trigger

Outputs:

1. Out L
2. Out R

## MF-20  *(adapter)*

Inputs:

1. Audio L
2. Audio R
3. LP Cutoff CV
4. HP Cutoff CV
5. Total Cutoff CV (sweeps both filters)

Outputs:

1. Audio L
2. Audio R

## Onbetap  *(adapter)*

Inputs:

1. Audio L
2. Audio R
3. Cutoff CV
4. Resonance CV
5. Drive CV

Outputs:

1. Audio L
2. Audio R

## Particules  *(adapter)*

Inputs:

1. Audio in L
2. Audio in R
3. Freeze gate
4. Seed/clock
5. Time CV
6. Density CV
7. Pitch CV (V/oct)
8. Size CV
9. Shape CV
10. Feedback CV
11. Reverb CV
12. Dry/wet CV

Outputs:

1. Audio out L
2. Audio out R

## Ondes  *(adapter)*

Inputs:

1. Pitch (V/oct)
2. Bank CV
3. Position CV

Outputs:

1. Audio out

## Vespid  *(adapter)*

Inputs:

1. Audio L
2. Audio R
3. Frequency CV
4. Resonance CV
5. Drive CV
6. Blend CV

Outputs:

1. Mix L
2. Mix R
3. Highpass L
4. Highpass R
5. Bandpass L
6. Bandpass R
7. Lowpass L
8. Lowpass R

## Retours  *(adapter)*

Inputs:

1. Audio in L
2. Audio in R
3. Slice gate
4. Clock
5. Time CV
6. Interval CV
7. Pitch CV
8. Shape CV
9. Feedback CV
10. Dry/wet CV

Outputs:

1. Audio out L
2. Audio out R
