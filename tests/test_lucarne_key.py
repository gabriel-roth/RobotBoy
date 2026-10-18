import os, re, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# The key's colored shapes stay in the glyphs (panel-specs/glyphs/lucarne/)
# as invisible placeholders (fill-opacity="0", lines stroke-opacity="0"),
# keeping today's Lucarne colors; src/lucarne/KeyLayout.hpp paints them in
# the active scheme. These tests fail when the two drift apart.

# Each role's color in the glyphs (the lucarne scheme's key).
ROLE_HEX = {
    "Wave": "7d9ab5", "PanL": "3fa9f5", "PanLC": "94c6e8", "PanC": "e8e4da",
    "PanCR": "f4b78c", "PanR": "ff8a3d", "LineA": "f0aa42", "LineB": "4baece",
    "LineDry": "c9cbd3", "BarA": "ffba51", "BarB": "52daff", "BarDry": "e8ecf2",
}

def attr(el, name):
    m = re.search(r'\b%s="([^"]*)"' % name, el)
    return m.group(1) if m else None

def parse_panel(path):
    """Zero-opacity shapes in paint order: (hex, x1, y1, x2, y2, stroke)."""
    with open(os.path.join(ROOT, path)) as f:
        svg = f.read()
    shapes = []
    for el in re.findall(r"<(?:polygon|line)\b[^>]*>", svg):
        if el.startswith("<polygon") and attr(el, "fill-opacity") == "0":
            pts = [tuple(map(float, p.split(","))) for p in attr(el, "points").split()]
            xs, ys = [p[0] for p in pts], [p[1] for p in pts]
            shapes.append((attr(el, "fill").lstrip("#").lower(),
                           min(xs), min(ys), max(xs), max(ys), 0.0))
        elif el.startswith("<line") and attr(el, "stroke-opacity") == "0":
            shapes.append((attr(el, "stroke").lstrip("#").lower(),
                           *(float(attr(el, k)) for k in ("x1", "y1", "x2", "y2")),
                           float(attr(el, "stroke-width"))))
    return shapes

def parse_layout(name):
    """Entries of a KeyShape array: (role, x1, y1, x2, y2, stroke)."""
    with open(os.path.join(ROOT, "src/lucarne/KeyLayout.hpp")) as f:
        src = f.read()
    body = re.search(r"\b%s\[\]\s*=\s*\{(.*?)\n\};" % name, src, re.S).group(1)
    num = r"\s*(-?[\d.]+)f?\s*"
    rows = re.findall(r"\{KeyRole::(\w+),%s,%s,%s,%s,%s\}" % ((num,) * 5), body)
    return [(r[0], *map(float, r[1:])) for r in rows]

class LucarneKeySync(unittest.TestCase):
    def check(self, svg, array):
        panel = parse_panel(svg)
        coded = parse_layout(array)
        self.assertTrue(panel, f"{svg} has no zero-opacity key placeholders")
        self.assertEqual(len(coded), len(panel),
            f"{array} has {len(coded)} shapes, {svg} has {len(panel)}")
        for i, (p, c) in enumerate(zip(panel, coded)):
            self.assertEqual(ROLE_HEX[c[0]], p[0],
                f"{array}[{i}] role {c[0]} vs glyph color #{p[0]}")
            for a, b in zip(p[1:], c[1:]):
                self.assertAlmostEqual(a, b, delta=0.01,
                    msg=f"{array}[{i}] {c} drifted from {svg} {p}")

    def test_particules_key(self):
        self.check("res/Lucarne.svg", "kParticulesKey")

    def test_retours_key(self):
        self.check("res/Lucarne-Retours.svg", "kRetoursKey")

if __name__ == "__main__":
    unittest.main()
