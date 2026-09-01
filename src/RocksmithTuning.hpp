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

    // Reads Rocksmith's currently loaded arrangement tuning.
    // The values are authored semitone offsets for E A D G B E.
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
