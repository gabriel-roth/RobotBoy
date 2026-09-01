# panel-specs/

RobotBoy's panels are generated with
[`vcv-panel-gen`](https://github.com/gabriel-roth/vcv-panel-gen), a separate
tool that turns a declarative YAML spec into a finished VCV Rack / MetaModule
front-panel SVG — grid layout, label text baked to vector paths, and
validation (anything off the panel edge is an error; overlaps are warnings).
It has no opinion on what makes a good layout; that judgment lives in the
spec.

Declarative panel layouts (v2 spec format) for every RobotBoy module live
here. Each `<slug>.yaml` generates the corresponding `res/<Slug>.svg` (see
Rules below).

## Setup

Generating or previewing a panel requires `vcv-panel-gen` cloned as a sibling
of this repo:

```bash
git clone https://github.com/gabriel-roth/vcv-panel-gen ~/Dev/vcv-panel-gen
cd ~/Dev/vcv-panel-gen && python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
```

This isn't needed to build the plugin — `res/*.svg` are already generated and
committed. It's only needed when editing a panel's layout.

## Commands

Generate, using redux venv path per the migration's ground rules:

```bash
~/Dev/vcv-panel-gen/.venv/bin/python ~/Dev/vcv-panel-gen/panelgen.py \
    panel-specs/<spec>.yaml --out res/<Slug>.svg
```

Validate a spec without writing anything:

```bash
~/Dev/vcv-panel-gen/.venv/bin/python ~/Dev/vcv-panel-gen/panelgen.py \
    panel-specs/<spec>.yaml --check
```

Preview (composites real ComponentLibrary art onto the panel, opens a
browser):

```bash
~/Dev/vcv-panel-gen/.venv/bin/python ~/Dev/vcv-panel-gen/panelgen.py \
    panel-specs/<spec>.yaml --out res/<Slug>.svg --preview --open
```

## Theme

House defaults are read automatically from `~/.config/vcv-panel-gen/theme.yaml`
(same schema across v1 and v2); no flag needed for a normal RobotBoy
generate. Per-panel overrides live in the spec's own `theme:` block (see
`vespid-gold.yaml` for an example).

## More

- Layout judgment (grid math, spacing recipes, control clustering) lives in
  the `vcv-panel` skill, not here — read that before writing or editing a
  spec.
- Tool internals and the full spec-format reference:
  `~/Dev/vcv-panel-gen/README.md` and `~/Dev/vcv-panel-gen/AGENTS.md`.

## Rules

- **`res/*.svg` are generated artifacts — never hand-edit them.** Change the
  spec and regenerate. (`vcv/res/` is a further build-time copy made by
  `vcv/Makefile`; also never hand-edited.)
- `Particules` and `Retours` were formerly hand-built Inkscape originals;
  both are now spec-driven like every other panel here.
