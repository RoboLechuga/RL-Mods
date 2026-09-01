#pragma once

namespace AsioPassthrough
{
    enum class Status
    {
        NotInstalled,
        WaitingForBuffers,
        BufferSetupFailed,
        NoChannelsBound,
        UnsupportedFormat,
        DuplicateChannel,
        Ready
    };

    bool Install();
    bool IsInstalled();

    Status GetStatus();
    const char* GetStatusText();

    // Safe to call from the worker thread while audio is running.
    // semitones: 0 = unity pitch class, -1 = one semitone down, etc.
    // referenceHz: 440 = normal concert pitch, 445 = A445, etc.
    void SetTuning(int semitones, int referenceHz);
    void SetPlayerTuning(int player, int semitones, int referenceHz);

    // Exact ratio control is used by the 1.3 tuning layer when the physical
    // guitar's reference is not A440. 1.0f is the true dry/neutral path.
    void SetPlayerRatio(int player, float ratio);

    bool IsPlayerReady(int player);
}
