# Löp display red lane — user GUI checklist

Automated tests and both builds pass; these are the visual checks that need
a human in front of VCV Rack (restart Rack first) and/or the MM simulator.

- [ ] Löp's playhead lane draws **red** (same red as Loooop head 1), not purple.
- [ ] The lane band is the **same height as one Loooop lane** (height/8 —
      half its old height).
- [ ] The waveform region is visibly **taller**, filling the freed space.
- [ ] Loooop's own display is unchanged (four lanes: red / yellow / blue /
      purple at the usual height).
- [ ] Module-browser previews for both Löp and Loooop still render.
- [ ] (MM, if convenient) Löp display on the MetaModule build matches VCV.
