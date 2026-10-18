import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]

# Copied from tagAliases in Rack's src/tag.cpp (v2). The VCV Library rejects
# any module tag that isn't one of these names or aliases (case-insensitive).
RACK_TAGS = [
    ["Arpeggiator"],
    ["Attenuator"],
    ["Blank"],
    ["Chorus"],
    ["Clock generator", "Clock"],
    ["Clock modulator"],
    ["Compressor"],
    ["Controller"],
    ["Delay"],
    ["Digital"],
    ["Distortion"],
    ["Drum", "Drums", "Percussion"],
    ["Dual"],
    ["Dynamics"],
    ["Effect"],
    ["Envelope follower"],
    ["Envelope generator"],
    ["Equalizer", "EQ"],
    ["Expander"],
    ["External"],
    ["Filter", "VCF", "Voltage controlled filter"],
    ["Flanger"],
    ["Function generator"],
    ["Granular"],
    ["Hardware clone", "Hardware"],
    ["Limiter"],
    ["Logic"],
    ["Low-frequency oscillator", "LFO", "Low frequency oscillator"],
    ["Low-pass gate", "Low pass gate", "Lowpass gate"],
    ["MIDI"],
    ["Mixer"],
    ["Multiple"],
    ["Noise"],
    ["Oscillator", "VCO", "Voltage controlled oscillator"],
    ["Panning", "Pan"],
    ["Phaser"],
    ["Physical modeling"],
    ["Polyphonic", "Poly"],
    ["Quad"],
    ["Quantizer"],
    ["Random"],
    ["Recording"],
    ["Reverb"],
    ["Ring modulator"],
    ["Sample and hold", "S&H", "Sample & hold"],
    ["Sampler"],
    ["Sequencer"],
    ["Slew limiter"],
    ["Speech"],
    ["Switch"],
    ["Synth voice"],
    ["Tuner"],
    ["Utility"],
    ["Visual"],
    ["Vocoder"],
    ["Voltage-controlled amplifier", "Amplifier", "VCA", "Voltage controlled amplifier"],
    ["Waveshaper"],
]


class PluginTagsTest(unittest.TestCase):
    def test_module_tags_are_valid_rack_tags(self):
        valid = {alias.lower() for aliases in RACK_TAGS for alias in aliases}
        plugin = json.loads((ROOT / "plugin.json").read_text())
        for module in plugin["modules"]:
            for tag in module.get("tags", []):
                with self.subTest(module=module["slug"], tag=tag):
                    self.assertIn(tag.lower(), valid)


if __name__ == "__main__":
    unittest.main()
