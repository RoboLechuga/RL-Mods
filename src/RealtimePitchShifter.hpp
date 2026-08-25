#pragma once

#include "IInputProcessor.hpp"

#include <array>
#include <atomic>
#include <cstdint>

namespace Audio
{
    // Stateful, per-player wrapper around the classic Stephan M. Bernsee
    // STFT pitch-shifting algorithm. The DSP implementation is adapted under
    // Bernsee's Wide Open License; see RealtimePitchShifter.cpp and
    // THIRD_PARTY-NOTICES.txt.
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
        static constexpr int FFT_SIZE = 1024;
        static constexpr int OVERSAMPLE = 4;
        static constexpr int HALF_FFT = FFT_SIZE / 2;
        static constexpr int STEP_SIZE = FFT_SIZE / OVERSAMPLE;
        static constexpr int FIFO_LATENCY = FFT_SIZE - STEP_SIZE;

        static float RatioForTuning(int semitones, int referenceHz);
        static void Fft(float* buffer, long frameSize, long sign);

        float ProcessOne(float input, float pitchRatio);
        void ResetState();

        std::atomic<float> targetRatio{ 1.0f };
        std::atomic<bool> neutral{ true };

        float sampleRate = 48000.0f;
        float currentRatio = 1.0f;
        float ratioStep = 1.0f;

        float wetMix = 0.0f;
        float wetStep = 1.0f;

        long rover = FIFO_LATENCY;

        std::array<float, FFT_SIZE> inputFifo{};
        std::array<float, FFT_SIZE> outputFifo{};
        std::array<float, 2 * FFT_SIZE> fftWorkspace{};
        std::array<float, HALF_FFT + 1> lastPhase{};
        std::array<float, HALF_FFT + 1> sumPhase{};
        std::array<float, 2 * FFT_SIZE> outputAccum{};
        std::array<float, FFT_SIZE> analysisFrequency{};
        std::array<float, FFT_SIZE> analysisMagnitude{};
        std::array<float, FFT_SIZE> synthesisFrequency{};
        std::array<float, FFT_SIZE> synthesisMagnitude{};
    };
}
