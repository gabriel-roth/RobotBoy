// src/classic_mode.h
#pragma once

// Classic mode: Particules, Retours and Ondes can run their pre-Beads (v2.1)
// engines. Patches saved before 2.2 have no "classic" key, so each module
// recognises them by what v2.1 did or didn't save.
namespace robotboy {

enum class ClassicFingerprint {
    kAbsentTapeHiss,     // Particules, Retours: every 2.2 save writes "tapeHiss"
    kPresentBankGroups,  // Ondes: every v2.1 save wrote "bankGroupsEnabled"
};

inline bool DetectClassic(bool has_classic_key, bool classic_value,
                          bool fingerprint_key_present, ClassicFingerprint kind) {
    if (has_classic_key) return classic_value;
    return kind == ClassicFingerprint::kAbsentTapeHiss ? !fingerprint_key_present
                                                       : fingerprint_key_present;
}

// Lets shared param quantities (the Pitch tooltip) ask which engine runs.
struct ClassicModeSource {
    virtual bool classicMode() const = 0;
    virtual ~ClassicModeSource() = default;
};

}  // namespace robotboy
