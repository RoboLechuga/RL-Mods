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

    // Diagnostic-build lifecycle stubs. No builder hook is installed.
    bool InitializeTunerTargetCapture();
    void ShutdownTunerTargetCapture();

    // Diagnostic build: Player 0 keeps the original tuner-text reader while
    // Player 1 intentionally remains unavailable until the MP UI path is mapped.
    bool TryReadTunerTarget(int player, Tuning& tuning);

    // Compatibility overload for existing single-player callers.
    bool TryReadTunerTarget(Tuning& tuning);

    // Temporary F10 diagnostic. Scans live private process memory for tuning
    // strings, then works backward through pointer references to find P1-style
    // UI objects and direct tuner-root sibling paths.
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
