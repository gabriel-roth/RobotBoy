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
            ["Loooop", "Lop", "MF20Filter", "Onbetap", "Particules", "Ondes", "Yellowjacket", "Retours"],
        )

        mm = json.loads((ROOT / "metamodule/plugin-mm.json").read_text())
        self.assertEqual(mm["MetaModuleBrandName"], "Robot Boy")
        self.assertEqual(mm["MetaModuleBrandSlug"], "RobotBoy")

        # Slug parity: the ordered module-slug list must match between the
        # public VCV manifest and the MetaModule manifest, or a module added
        # to one and forgotten in the other silently falls out of sync.
        self.assertEqual(
            [module["slug"] for module in plugin["modules"]],
            [module["slug"] for module in mm["MetaModuleIncludedModules"]],
        )

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
