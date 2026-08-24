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
        float RenderHead(double headPhase, double drift) const;

        std::atomic<float> targetRatio{ 1.0f };
        std::atomic<bool> neutral{ true };

        std::vector<float> history;

        std::uint32_t historyMask = 0;
        std::uint32_t writeIndex = 0;

        std::uint32_t minimumDelayFrames = 0;
        std::uint32_t sweepFrames = 0;
        std::uint32_t maximumDelayFrames = 0;

        double phase = 0.0;

        float currentRatio = 1.0f;
        float ratioSlew = 1.0f;

        float wetMix = 0.0f;
        float wetStep = 1.0f;

        bool wasNeutral = true;
    };
}
