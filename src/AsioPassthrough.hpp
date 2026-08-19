#pragma once

namespace AsioPassthrough
{
    bool Install();
    bool IsInstalled();

    // Safe to call from the worker thread while audio is running.
    // semitones: 0 = E, -1 = Eb, -2 = D, etc.
    // referenceHz: 440 = normal concert pitch, 445 = A445, etc.
    void SetTuning(int semitones, int referenceHz);
}
