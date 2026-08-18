#pragma once
#include "IInputProcessor.hpp"

#include <atomic>
#include <cstdint>
#include <vector>

namespace Audio
{
    class DelayLinePitchShifter final : public IInputProcessor
    {
    public:
        explicit DelayLinePitchShifter(int semitones);

        void SetSemitones(int semitones);

        void Prepare(const CaptureFormat& format) override;
        void Process(float* samples, std::uint32_t frameCount) override;
        std::uint32_t GetLatencyFrames() const override;

    private:
        std::atomic<float> ratio;

        std::vector<float> ring;
        std::uint32_t writePosition = 0;
        double readDelay = 0.0;
        double fadeFromDelay = 0.0;
        int fadeLength = 64;
        int fadeRemaining = 0;

        std::vector<float> decimated;
        std::uint32_t decimatedPosition = 0;
        float decimationAccumulator = 0.0f;
        std::uint32_t decimationPhase = 0;
        std::uint32_t samplesSinceDetect = 0;
        int periodSamples = 0;
        int candidatePeriod = 0;
        int candidateVotes = 0;

        float ReadTap(double delay) const;
        void DetectPeriod();
        int RefineAtFullRate(int coarsePeriod) const;
    };
}
