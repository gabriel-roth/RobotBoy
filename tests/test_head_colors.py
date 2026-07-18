import os, re, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

def parse_table_hues():
    with open(os.path.join(ROOT, "src/loooop/HeadColors.hpp")) as f:
        src = f.read()
    body = src[src.index("kHeadColors"):]
    rows = re.findall(r"\{\s*0x([0-9A-Fa-f]{2})\s*,\s*0x([0-9A-Fa-f]{2})\s*,\s*0x([0-9A-Fa-f]{2})\s*,", body)
    return ["%02x%02x%02x" % (int(r,16), int(g,16), int(b,16)) for r,g,b in rows]

def parse_spec_hues():
    # v2 spec format: the four head tints are zone entries (one rect per
    # head, in H1..H4 order) whose fill is the head color.
    with open(os.path.join(ROOT, "panel-specs/loooop.yaml")) as f:
        spec = f.read()
    hexes = [re.search(r'fill:\s*"#([0-9A-Fa-f]{6,8})"', l).group(1)
             for l in spec.splitlines() if re.match(r"\s*-\s*\{x:", l) and "fill" in l]
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
