import os, re, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

def parse_table_hues():
    src = open(os.path.join(ROOT, "src/loooop/HeadColors.hpp")).read()
    body = src[src.index("kHeadColors"):]
    rows = re.findall(r"\{\s*0x([0-9A-Fa-f]{2})\s*,\s*0x([0-9A-Fa-f]{2})\s*,\s*0x([0-9A-Fa-f]{2})\s*,", body)
    return ["%02x%02x%02x" % (int(r,16), int(g,16), int(b,16)) for r,g,b in rows]

def parse_spec_hues():
    spec = open(os.path.join(ROOT, "panel-specs/loooop.yaml")).read()
    line = next(l for l in spec.splitlines() if "tints:" in l and "#" in l)
    hexes = re.findall(r"#([0-9A-Fa-f]{6,8})", line)
    return [h[:6].lower() for h in hexes]   # strip any alpha suffix

class HeadColorSync(unittest.TestCase):
    def test_panel_tints_match_canonical_table(self):
        table = parse_table_hues()
        spec = parse_spec_hues()
        self.assertEqual(len(table), 4, f"expected 4 table colors, got {table}")
        self.assertEqual(table, spec,
            f"panel-specs/loooop.yaml tints {spec} drifted from kHeadColors {table}")

if __name__ == "__main__":
    unittest.main()
