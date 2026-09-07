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

    std::string CurrentMenuName();
    bool IsPreSongTuner(const std::string& menu);
    bool IsSongGameplayMenu(const std::string& menu);

    // Reads Rocksmith's six-string pre-song tuner target. Player 0 is SP/P1;
    // player 1 is P2 in supported multiplayer tuner menus. The structural
    // object path is primary; single-player tuner text is a compatibility
    // fallback only.
    bool TryReadTunerTarget(int player, Tuning& tuning);
    bool TryReadTunerTarget(Tuning& tuning);

    // Legacy/general arrangement reader retained for non-Auto callers.
    bool TryReadArrangement(Tuning& tuning);

    // Reads the currently authored true/reference tuning.
    // A220-style bass references are normalized to their A440-family value.
    bool TryReadReferenceHz(int& referenceHz);

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
