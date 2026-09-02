#pragma once

#include <array>
#include <string>

namespace RocksmithTuning
{
    struct Tuning
    {
        std::array<int, 6> strings{};

        bool operator==(const Tuning& other) const
        {
            return strings == other.strings;
        }

        bool operator!=(const Tuning& other) const
        {
            return !(*this == other);
        }
    };

    // Temporary F10 diagnostics for the v1.3 auto-tune investigation.
    // Appends both 2022 and 2024 pointer-chain results to
    // RLMods_tuning_debug.txt in the Rocksmith directory.
    bool CaptureDebugSnapshot();

    // Reads Rocksmith's current target tuning. In a pre-song tuner this uses
    // the displayed tuner tuning; once gameplay is loaded it uses the current
    // arrangement's six authored string offsets. Both September-2022 and
    // December-2024 executable pointer roots are supported.
    bool TryReadArrangement(Tuning& tuning);

    // Reads the currently authored true/reference tuning.
    // A220-style bass references are normalized to their A440-family value.
    bool TryReadReferenceHz(int& referenceHz);

    // Returns true only when every string can move by the same semitone amount.
    bool TryGetUniformShift(
        const Tuning& physical,
        const Tuning& target,
        int& semitones);

    Tuning Shifted(
        const Tuning& tuning,
        int semitones);

    std::string Name(const Tuning& tuning);
    std::string VectorText(const Tuning& tuning);
}
