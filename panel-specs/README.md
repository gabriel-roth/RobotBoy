# panel-specs/

Declarative panel layouts (v2 spec format) for every RobotBoy module. Each
`<slug>.yaml` here generates the corresponding `res/<Slug>.svg`.

## Commands

Generate, using redux venv path per the migration's ground rules:

```bash
~/Dev/vcv-panel-gen-redux/.venv/bin/python ~/Dev/vcv-panel-gen-redux/panelgen.py \
    panel-specs/<spec>.yaml --out res/<Slug>.svg
```

Validate a spec without writing anything:

```bash
~/Dev/vcv-panel-gen-redux/.venv/bin/python ~/Dev/vcv-panel-gen-redux/panelgen.py \
    panel-specs/<spec>.yaml --check
```

Preview (composites real ComponentLibrary art onto the panel, opens a
browser):

```bash
~/Dev/vcv-panel-gen-redux/.venv/bin/python ~/Dev/vcv-panel-gen-redux/panelgen.py \
    panel-specs/<spec>.yaml --out res/<Slug>.svg --preview --open
```

## Theme

House defaults are read automatically from `~/.config/vcv-panel-gen/theme.yaml`
(same schema across v1 and v2); no flag needed for a normal RobotBoy
generate. Per-panel overrides live in the spec's own `theme:` block (see
`yellowjacket-gold.yaml` for an example).

## More

- Layout judgment (grid math, spacing recipes, control clustering) lives in
  the `vcv-panel` skill, not here — read that before writing or editing a
  spec.
- Tool internals and the full spec-format reference:
  `~/Dev/vcv-panel-gen-redux/README.md` and
  `~/Dev/vcv-panel-gen-redux/docs/superpowers/specs/2026-07-17-grid-panel-generator-design.md`.

## Rules

- **`res/*.svg` are generated artifacts — never hand-edit them.** Change the
  spec and regenerate. (`vcv/res/` is a further build-time copy made by
  `vcv/Makefile`; also never hand-edited.)
- `Particules` and `Retours` were formerly hand-built Inkscape originals;
  both are now spec-driven like every other panel here.
