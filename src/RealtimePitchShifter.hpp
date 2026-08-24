#pragma once

#include "IInputProcessor.hpp"

#include <atomic>
#include <cstdint>
#include <vector>

namespace Audio
{
    class RealtimePitchShifter final : public IInputProcessor
    {
    public:
        explicit RealtimePitchShifter(int semitones);

        void SetSemitones(int semitones);
        void SetTuning(int semitones, int referenceHz);

        bool IsNeutral() const;

        void Prepare(const CaptureFormat& format) override;
        void Process(float* samples, std::uint32_t frameCount) override;
        std::uint32_t GetLatencyFrames() const override;

    private:
        static float RatioForTuning(int semitones, int referenceHz);
        static std::uint32_t NextPowerOfTwo(std::uint32_t value);

        float ReadHistory(double delayFrames) const;
        void TrackPeriod(float sample);
        void BeginDelayJump(double newDelay);

        std::atomic<float> targetRatio{ 1.0f };
        std::atomic<bool> neutral{ true };

        std::vector<float> history;
        std::uint32_t historyMask = 0;
        std::uint32_t writeIndex = 0;

        std::uint32_t sampleRate = 48000;

        double readDelay = 0.0;
        double oldReadDelay = 0.0;

        double lowDelay = 0.0;
        double centerDelay = 0.0;
        double highDelay = 0.0;

        float currentRatio = 1.0f;
        float ratioStep = 1.0f;

        std::uint32_t crossfadeLength = 0;
        std::uint32_t crossfadeRemaining = 0;

        float wetMix = 0.0f;
        float wetStep = 1.0f;

        float previousInput = 0.0f;
        std::uint64_t sampleCounter = 0;
        std::uint64_t lastPositiveCrossing = 0;

        double estimatedPeriod = 240.0;
        int periodCandidateCount = 0;
        double periodCandidate = 0.0;
    };
}
