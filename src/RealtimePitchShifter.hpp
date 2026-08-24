#pragma once

#include "IInputProcessor.hpp"

#include <array>
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
        static constexpr std::size_t GRAIN_COUNT = 4;
        static constexpr std::size_t OUTPUT_TAIL_SIZE = 128;

        struct Grain
        {
            std::uint32_t age = 0;
            double anchorDelay = 0.0;
        };

        static float RatioForTuning(int semitones, int referenceHz);
        static std::uint32_t NextPowerOfTwo(std::uint32_t value);

        float ReadHistory(double delayFrames) const;
        float GrainWindow(std::uint32_t age) const;

        double FindAlignedDelay() const;
        void ResetGrain(Grain& grain);

        void PushOutputTail(float sample);
        float ReadOutputTail(std::size_t indexFromOldest) const;

        std::atomic<float> targetRatio{ 1.0f };
        std::atomic<bool> neutral{ true };

        std::vector<float> history;
        std::uint32_t historyMask = 0;
        std::uint32_t writeIndex = 0;

        std::uint32_t sampleRate = 48000;

        std::uint32_t grainFrames = 480;
        std::uint32_t hopFrames = 120;
        std::uint32_t searchRadius = 32;
        std::uint32_t correlationFrames = 64;

        double baseDelay = 540.0;

        std::array<Grain, GRAIN_COUNT> grains{};

        std::array<float, OUTPUT_TAIL_SIZE> outputTail{};
        std::size_t outputTailWrite = 0;
        std::size_t outputTailCount = 0;

        float currentRatio = 1.0f;
        float ratioStep = 1.0f;

        float wetMix = 0.0f;
        float wetStep = 1.0f;
    };
}
