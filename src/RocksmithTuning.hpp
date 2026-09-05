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

    // Installs the lightweight tuning-reference builder capture used to obtain
    // independent multiplayer chart targets. Single-player Auto still prefers
    // the existing displayed tuner-text reader.
    bool InitializeTunerTargetCapture();
    void ShutdownTunerTargetCapture();

    // Player-aware pre-song tuner target reader. Player 0 keeps the proven
    // single-player tuner-text path and falls back to the builder capture when
    // multiplayer blanks that text. Player 1 is sourced from the independent
    // builder capture.
    bool TryReadTunerTarget(int player, Tuning& tuning);

    // Compatibility overload for existing single-player callers.
    bool TryReadTunerTarget(Tuning& tuning);

    // Temporary F10 test snapshot. Appends the currently captured P1/P2 targets
    // to RLMods_tuning_debug.txt without performing a heap scan.
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
