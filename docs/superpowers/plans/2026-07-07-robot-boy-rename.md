# Robot Boy Rename Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rename the Foobar plugin, packages, repository, and local checkout to Robot Boy / RobotBoy, and create a migrated copy of the supplied VCV patch.

**Architecture:** Treat `Robot Boy` as the user-facing brand and `RobotBoy` as the machine identifier. Rename the active metadata and build/registration graph coherently while preserving all module slugs, then verify both host builds before renaming the GitHub repository and local checkout. Patch migration changes only exact module-level `plugin: "Foobar"` values in decompressed VCV JSON.

**Tech Stack:** JSON, C++20, CMake, GNU Make, shell, Python 3 standard library, Zstandard CLI, Git, GitHub CLI.

---

### Task 1: Add an identity contract test

**Files:**
- Create: `tests/test_robotboy_identity.py`
- Test: `tests/test_robotboy_identity.py`

- [ ] **Step 1: Write the failing identity test**

Create a standard-library test that loads `plugin.json` and `metamodule/plugin-mm.json`, reads `metamodule/loooop/brand.hh`, `metamodule/CMakeLists.txt`, `metamodule/register.cc`, and `src/plugin.cpp`, then asserts:

```python
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RobotBoyIdentityTest(unittest.TestCase):
    def test_public_metadata(self):
        plugin = json.loads((ROOT / "plugin.json").read_text())
        self.assertEqual(plugin["slug"], "RobotBoy")
        self.assertEqual(plugin["name"], "Robot Boy")
        self.assertEqual(plugin["brand"], "Robot Boy")
        self.assertEqual(
            [module["slug"] for module in plugin["modules"]],
            ["Loooop", "Lop", "MF20Filter", "Particules"],
        )

        mm = json.loads((ROOT / "metamodule/plugin-mm.json").read_text())
        self.assertEqual(mm["MetaModuleBrandName"], "Robot Boy")
        self.assertEqual(mm["MetaModuleBrandSlug"], "RobotBoy")

    def test_build_and_registration_identity(self):
        brand = (ROOT / "metamodule/loooop/brand.hh").read_text()
        cmake = (ROOT / "metamodule/CMakeLists.txt").read_text()
        register = (ROOT / "metamodule/register.cc").read_text()
        plugin_cpp = (ROOT / "src/plugin.cpp").read_text()

        self.assertIn('#define ROBOTBOY_BRAND "RobotBoy"', brand)
        self.assertNotIn("FOOBAR_BRAND", brand)
        self.assertIn("project(RobotBoy ", cmake)
        self.assertIn("ROBOTBOY_COMBINED", cmake)
        self.assertNotIn("FOOBAR_COMBINED", cmake)
        self.assertIn("init_RobotBoy", register)
        self.assertIn("init_RobotBoy", plugin_cpp)
        self.assertNotIn("init_Foobar", register)
        self.assertNotIn("init_Foobar", plugin_cpp)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test and verify RED**

Run: `python3 -m unittest tests/test_robotboy_identity.py -v`

Expected: failures showing the current `Foobar` metadata and identifiers.

### Task 2: Rename active plugin metadata and build identifiers

**Files:**
- Modify: `plugin.json`
- Modify: `metamodule/plugin-mm.json`
- Modify: `metamodule/CMakeLists.txt`
- Modify: `metamodule/register.cc`
- Modify: `src/plugin.cpp`
- Modify: `metamodule/loooop/brand.hh`
- Modify: `metamodule/loooop/LoooopCore.cc`
- Modify: `metamodule/loooop/LopCore.cc`
- Modify: `metamodule/loooop/Loooop_info.hh`
- Modify: `metamodule/loooop/Lop_info.hh`
- Modify: `README.md`
- Modify: `tests/README.md`
- Modify: `tests/beads/CMakeLists.txt`
- Modify: `tests/run.sh`
- Test: `tests/test_robotboy_identity.py`

- [ ] **Step 1: Update public metadata**

Set the root manifest fields to:

```json
"slug": "RobotBoy",
"name": "Robot Boy",
"brand": "Robot Boy"
```

Set MetaModule metadata to:

```json
"MetaModuleBrandName": "Robot Boy",
"MetaModuleBrandSlug": "RobotBoy"
```

Leave the four module objects and their slugs unchanged.

- [ ] **Step 2: Rename the MetaModule target and package**

In `metamodule/CMakeLists.txt`, rename `Foobar` to `RobotBoy` in `project()`, `add_library()`, every target command, `SOURCE_LIB`, and `PLUGIN_NAME`. Rename `FOOBAR_COMBINED` to `ROBOTBOY_COMBINED`.

- [ ] **Step 3: Rename registration and brand identifiers**

Rename:

```text
init_Foobar       -> init_RobotBoy
FOOBAR_COMBINED   -> ROBOTBOY_COMBINED
FOOBAR_BRAND      -> ROBOTBOY_BRAND
"Foobar" brand   -> "RobotBoy" brand slug
```

Update active explanatory comments in the same files. Preserve module registration order and module slugs.

- [ ] **Step 4: Rename current docs and test-local identifiers**

Change the current README heading/text to `Robot Boy`, the test README heading to `Robot Boy regression tests`, the beads CMake project/target identifiers to lowercase `robotboy_*`, and `/tmp/foobar_*` test outputs to `/tmp/robotboy_*`.

- [ ] **Step 5: Run identity test and verify GREEN**

Run: `python3 -m unittest tests/test_robotboy_identity.py -v`

Expected: 2 tests pass.

- [ ] **Step 6: Validate manifests and active references**

Run:

```bash
python3 -m json.tool plugin.json >/dev/null
python3 -m json.tool metamodule/plugin-mm.json >/dev/null
git grep -n -i foobar -- ':!docs/superpowers/**' ':!.superpowers/**'
```

Expected: both JSON commands succeed; active grep returns no plugin-identity hits.

- [ ] **Step 7: Commit the identity rename**

```bash
git add plugin.json metamodule/plugin-mm.json metamodule/CMakeLists.txt \
  metamodule/register.cc src/plugin.cpp metamodule/loooop/brand.hh \
  metamodule/loooop/LoooopCore.cc metamodule/loooop/LopCore.cc \
  metamodule/loooop/Loooop_info.hh metamodule/loooop/Lop_info.hh \
  README.md tests/README.md tests/beads/CMakeLists.txt tests/run.sh \
  tests/test_robotboy_identity.py
git commit -m "refactor: rename plugin to Robot Boy"
```

### Task 3: Migrate the supplied VCV patch without altering the original

**Files:**
- Preserve: `Foobar-test.vcv`
- Create: `RobotBoy-test.vcv`

- [ ] **Step 1: Decompress the source patch to temporary JSON**

Run:

```bash
zstd -d -f Foobar-test.vcv -o /tmp/Foobar-test.json
python3 -m json.tool /tmp/Foobar-test.json >/dev/null
```

Expected: decompression succeeds and the payload is valid JSON.

- [ ] **Step 2: Verify the source patch contains old plugin references**

Run:

```bash
python3 - <<'PY'
import json
from pathlib import Path
p = json.loads(Path('/tmp/Foobar-test.json').read_text())
refs = [m for m in p['modules'] if m.get('plugin') == 'Foobar']
assert refs, 'expected at least one Foobar module reference'
print(f'{len(refs)} Foobar references found')
PY
```

Expected: prints a positive reference count.

- [ ] **Step 3: Create migrated JSON by changing only exact plugin fields**

Run:

```bash
python3 - <<'PY'
import json
from pathlib import Path
source = Path('/tmp/Foobar-test.json')
target = Path('/tmp/RobotBoy-test.json')
p = json.loads(source.read_text())
for module in p['modules']:
    if module.get('plugin') == 'Foobar':
        module['plugin'] = 'RobotBoy'
target.write_text(json.dumps(p, separators=(',', ':')))
PY
zstd -f /tmp/RobotBoy-test.json -o RobotBoy-test.vcv
```

- [ ] **Step 4: Verify semantic equivalence except plugin identity**

Run a Python comparison that loads both JSON payloads, deep-copies the source, changes only exact `plugin == "Foobar"` fields to `RobotBoy`, and asserts equality with the migrated payload. Also assert the original `Foobar-test.vcv` still exists and its checksum did not change from before migration.

Expected: comparison succeeds, with at least one migrated reference and zero remaining module-level `Foobar` references.

- [ ] **Step 5: Commit both patch fixtures**

```bash
git add Foobar-test.vcv RobotBoy-test.vcv
git commit -m "test: add Robot Boy patch migration fixture"
```

### Task 4: Verify both host builds

**Files:**
- Verify: `tests/**`
- Verify: `vcv/**`
- Verify: `metamodule/**`

- [ ] **Step 1: Run repository regression tests**

Run:

```bash
./tests/run.sh
./tests/beads/run.sh
python3 -m unittest tests/test_robotboy_identity.py -v
```

Expected: all commands exit 0 with no test failures.

- [ ] **Step 2: Build the VCV Rack plugin**

Run: `make -C vcv clean && make -C vcv -j`

Expected: build exits 0 and produces the RobotBoy plugin binary/package metadata.

- [ ] **Step 3: Build the MetaModule package**

Use the `particules-build-metamodule` skill and run the repository-supported MetaModule build path after the CMake target rename.

Expected: `metamodule/metamodule-plugins/RobotBoy.mmplugin` exists.

- [ ] **Step 4: Inspect MetaModule package identity and asset paths**

Run:

```bash
tar tf metamodule/metamodule-plugins/RobotBoy.mmplugin
```

Expected: package metadata uses `RobotBoy`, and looper faceplates are nested under `RobotBoy/Loooop/`.

- [ ] **Step 5: Run final diff checks**

Run:

```bash
git diff --check
git status --short
```

Expected: no whitespace errors; only known unrelated untracked files remain after committed rename work.

### Task 5: Rename the GitHub repository and local checkout

**Files/External state:**
- Rename GitHub repository: `gabriel-roth/Foobar` -> `gabriel-roth/RobotBoy`
- Update Git remote: `origin`
- Rename local checkout: `/Users/gabrielroth/Dev/Foobar` -> `/Users/gabrielroth/Dev/RobotBoy`

- [ ] **Step 1: Push all committed work to the current remote**

Run: `git push origin main`

Expected: push succeeds with all rename commits present on `main`.

- [ ] **Step 2: Rename the GitHub repository**

Run: `gh repo rename RobotBoy --repo gabriel-roth/Foobar --yes`

Expected: GitHub reports the repository as `gabriel-roth/RobotBoy`.

- [ ] **Step 3: Update and verify origin**

Run:

```bash
git remote set-url origin https://github.com/gabriel-roth/RobotBoy.git
git remote -v
git ls-remote origin HEAD
```

Expected: both origin URLs use `RobotBoy`; remote lookup succeeds.

- [ ] **Step 4: Rename the local checkout directory**

From `/Users/gabrielroth/Dev`, run:

```bash
mv Foobar RobotBoy
```

Expected: `/Users/gabrielroth/Dev/RobotBoy` exists and `/Users/gabrielroth/Dev/Foobar` does not.

- [ ] **Step 5: Verify final repository state from the new path**

From `/Users/gabrielroth/Dev/RobotBoy`, run:

```bash
pwd
git branch --show-current
git remote -v
git status --short
git rev-parse HEAD
git rev-parse origin/main
```

Expected: path is `~/Dev/RobotBoy`, branch is `main`, origin uses `RobotBoy`, unrelated untracked files remain untouched, and local/remote commits match.
