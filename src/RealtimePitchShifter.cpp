#include "RealtimePitchShifter.hpp"

#include <algorithm>
#include <cmath>

namespace Audio
{
    namespace
    {
        constexpr float PI = 3.14159265358979323846f;

        constexpr float MIN_REFERENCE_HZ = 1.0f;
        constexpr int DEFAULT_REFERENCE_HZ = 440;

        constexpr float RATIO_SLEW_TIME_SECONDS = 0.020f;
        constexpr float WET_RAMP_TIME_SECONDS = 0.010f;

        constexpr float MIN_DELAY_MS = 2.0f;
        constexpr float SWEEP_MS = 12.0f;
        constexpr float MAX_DELAY_MARGIN_MS = 2.0f;
    }

    RealtimePitchShifter::RealtimePitchShifter(int semitones)
    {
        SetSemitones(semitones);
    }

    float RealtimePitchShifter::RatioForTuning(
        int semitones,
        int referenceHz)
    {
        if (referenceHz < static_cast<int>(MIN_REFERENCE_HZ))
            referenceHz = DEFAULT_REFERENCE_HZ;

        const float semitoneRatio =
            std::pow(
                2.0f,
                static_cast<float>(semitones) / 12.0f);

        const float referenceRatio =
            static_cast<float>(referenceHz) /
            static_cast<float>(DEFAULT_REFERENCE_HZ);

        return semitoneRatio * referenceRatio;
    }

    void RealtimePitchShifter::SetSemitones(int semitones)
    {
        SetTuning(semitones, DEFAULT_REFERENCE_HZ);
    }

    void RealtimePitchShifter::SetTuning(
        int semitones,
        int referenceHz)
    {
        const float ratio =
            RatioForTuning(semitones, referenceHz);

        targetRatio.store(
            ratio,
            std::memory_order_relaxed);

        neutral.store(
            semitones == 0 &&
            referenceHz == DEFAULT_REFERENCE_HZ,
            std::memory_order_relaxed);
    }

    bool RealtimePitchShifter::IsNeutral() const
    {
        return neutral.load(std::memory_order_relaxed);
    }

    std::uint32_t RealtimePitchShifter::NextPowerOfTwo(
        std::uint32_t value)
    {
        if (value <= 1)
            return 1;

        --value;

        value |= value >> 1;
        value |= value >> 2;
        value |= value >> 4;
        value |= value >> 8;
        value |= value >> 16;

        return value + 1;
    }

    void RealtimePitchShifter::Prepare(
        const CaptureFormat& format)
    {
        const float sampleRate =
            static_cast<float>(format.sampleRate);

        minimumDelayFrames =
            static_cast<std::uint32_t>(
                sampleRate * MIN_DELAY_MS * 0.001f);

        sweepFrames =
            static_cast<std::uint32_t>(
                sampleRate * SWEEP_MS * 0.001f);

        const std::uint32_t marginFrames =
            static_cast<std::uint32_t>(
                sampleRate *
                MAX_DELAY_MARGIN_MS *
                0.001f);

        maximumDelayFrames =
            minimumDelayFrames +
            sweepFrames +
            marginFrames;

        const std::uint32_t historySize =
            NextPowerOfTwo(
                maximumDelayFrames + 8);

        history.assign(
            historySize,
            0.0f);

        historyMask =
            historySize - 1;

        writeIndex = 0;
        phase = 0.0;

        currentRatio =
            targetRatio.load(
                std::memory_order_relaxed);

        const float slewSamples =
            std::max(
                1.0f,
                sampleRate *
                RATIO_SLEW_TIME_SECONDS);

        ratioSlew =
            1.0f / slewSamples;

        const float wetRampSamples =
            std::max(
                1.0f,
                sampleRate *
                WET_RAMP_TIME_SECONDS);

        wetStep =
            1.0f / wetRampSamples;

        wasNeutral = IsNeutral();
        wetMix = wasNeutral ? 0.0f : 1.0f;
    }

    float RealtimePitchShifter::ReadHistory(
        double delayFrames) const
    {
        if (history.empty())
            return 0.0f;

        delayFrames =
            std::clamp(
                delayFrames,
                static_cast<double>(
                    minimumDelayFrames),
                static_cast<double>(
                    maximumDelayFrames));

        double readPosition =
            static_cast<double>(writeIndex) -
            delayFrames;

        while (readPosition < 0.0)
            readPosition +=
                static_cast<double>(
                    history.size());

        const std::uint32_t index0 =
            static_cast<std::uint32_t>(
                readPosition) &
            historyMask;

        const std::uint32_t index1 =
            (index0 + 1) &
            historyMask;

        const float fraction =
            static_cast<float>(
                readPosition -
                std::floor(readPosition));

        return
            history[index0] +
            (history[index1] -
             history[index0]) *
                fraction;
    }

    float RealtimePitchShifter::RenderHead(
        double headPhase,
        double drift) const
    {
        const double wrapped =
            headPhase -
            std::floor(headPhase);

        const double delay =
            static_cast<double>(
                minimumDelayFrames) +
            wrapped *
            static_cast<double>(
                sweepFrames);

        return ReadHistory(delay);
    }

    void RealtimePitchShifter::Process(
        float* samples,
        std::uint32_t frameCount)
    {
        if (!samples || history.empty())
            return;

        const float target =
            targetRatio.load(
                std::memory_order_relaxed);

        const bool nowNeutral =
            neutral.load(
                std::memory_order_relaxed);

        if (nowNeutral != wasNeutral)
            wasNeutral = nowNeutral;

        for (std::uint32_t i = 0;
             i < frameCount;
             ++i)
        {
            const float dry =
                samples[i];

            history[writeIndex] =
                dry;

            const float difference =
                target - currentRatio;

            if (std::fabs(difference) <=
                ratioSlew)
            {
                currentRatio =
                    target;
            }
            else
            {
                currentRatio +=
                    difference > 0.0f
                        ? ratioSlew
                        : -ratioSlew;
            }

            const double drift =
                1.0 -
                static_cast<double>(
                    currentRatio);

            phase +=
                drift /
                static_cast<double>(
                    sweepFrames);

            phase -=
                std::floor(phase);

            const double phaseA =
                phase;

            double phaseB =
                phase + 0.5;

            if (phaseB >= 1.0)
                phaseB -= 1.0;

            const float headA =
                RenderHead(
                    phaseA,
                    drift);

            const float headB =
                RenderHead(
                    phaseB,
                    drift);

            const float windowA =
                0.5f -
                0.5f *
                std::cos(
                    2.0f *
                    PI *
                    static_cast<float>(
                        phaseA));

            const float windowB =
                0.5f -
                0.5f *
                std::cos(
                    2.0f *
                    PI *
                    static_cast<float>(
                        phaseB));

            const float weightSum =
                windowA + windowB;

            float wet = 0.0f;

            if (weightSum > 0.000001f)
            {
                wet =
                    (headA * windowA +
                     headB * windowB) /
                    weightSum;
            }

            const float targetWet =
                nowNeutral ? 0.0f : 1.0f;

            if (wetMix < targetWet)
            {
                wetMix =
                    std::min(
                        targetWet,
                        wetMix + wetStep);
            }
            else if (wetMix > targetWet)
            {
                wetMix =
                    std::max(
                        targetWet,
                        wetMix - wetStep);
            }

            samples[i] =
                dry +
                (wet - dry) *
                    wetMix;

            writeIndex =
                (writeIndex + 1) &
                historyMask;
        }
    }

    std::uint32_t
    RealtimePitchShifter::GetLatencyFrames() const
    {
        if (IsNeutral())
            return 0;

        return minimumDelayFrames;
    }
}
