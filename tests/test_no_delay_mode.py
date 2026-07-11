from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class NoDelayModeTest(unittest.TestCase):
    def test_delay_engine_files_are_removed(self):
        self.assertFalse((ROOT / "src/particules/dsp/src/delay/delay_engine.h").exists())
        self.assertFalse((ROOT / "src/particules/dsp/src/delay/delay_engine.cpp").exists())
        self.assertFalse((ROOT / "tests/beads/test_delay.cpp").exists())

    def test_delay_mode_symbols_are_removed(self):
        paths = [
            "src/particules/dsp/include/beads/parameters.h",
            "src/particules/dsp/include/beads/beads.h",
            "src/particules/dsp/src/beads_processor.h",
            "src/particules/dsp/src/beads_processor.cpp",
            "src/particules/dsp/src/grain/grain_engine.cpp",
            "metamodule/CMakeLists.txt",
        ]
        forbidden = (
            "delay_mode",
            "DelayEngine",
            "delay_engine",
            "IsDelayMode",
            "DelayTriggeredThisBlock",
            "mode_xfade",
            "wet_alt",
            "ApplyLfoAr",
            "time_lfo",
            "size_lfo",
            "shape_lfo",
            "pitch_lfo",
            "delay mode",
        )
        for relative in paths:
            text = (ROOT / relative).read_text()
            for symbol in forbidden:
                self.assertNotIn(symbol, text, f"{symbol} remains in {relative}")


if __name__ == "__main__":
    unittest.main()
