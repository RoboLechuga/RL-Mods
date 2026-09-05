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

    // Current Rocksmith menu name from the configured executable layout.
    std::string CurrentMenuName();

    // True for the pre-song tuner menus used by Learn a Song, Nonstop Play,
    // Score Attack, Session Mode, Duet/H2H, and the pre-game tuner.
    bool IsPreSongTuner(const std::string& menu);

    // True for a gameplay menu. Used only to confirm that a pre-song tuner
    // was successfully exited forward into the song.
    bool IsSongGameplayMenu(const std::string& menu);

    // Reads only the displayed pre-song tuner target text. Auto mode uses this
    // source and latches the result; it does not continuously recalculate from
    // the in-song arrangement pointer.
    bool TryReadTunerTarget(Tuning& tuning);

    // Temporary F10 diagnostic used to discover the independent Player 2
    // pre-song tuner target path. Appends to RLMods_tuning_debug.txt.
    bool CaptureDebugSnapshot();

    // Legacy/general target reader retained for diagnostics and other callers.
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
