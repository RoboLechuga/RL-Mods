#include "RealtimePitchShifter.hpp"

#include <algorithm>
#include <cmath>

namespace Audio
{
    namespace
    {
        constexpr int DEFAULT_REFERENCE_HZ = 440;
        constexpr float PI = 3.14159265358979323846f;

        constexpr float LOW_DELAY_MS = 3.0f;
        constexpr float CENTER_DELAY_MS = 9.0f;
        constexpr float HIGH_DELAY_MS = 15.0f;

        constexpr float CROSSFADE_MS = 1.5f;
        constexpr float RATIO_SLEW_MS = 12.0f;
        constexpr float WET_RAMP_MS = 6.0f;

        constexpr float MIN_TRACK_HZ = 55.0f;
        constexpr float MAX_TRACK_HZ = 1200.0f;

        constexpr float CROSSING_SLOPE_FLOOR = 0.00025f;
        constexpr float PERIOD_CHANGE_LIMIT = 0.35f;
    }

    RealtimePitchShifter::RealtimePitchShifter(int semitones)
    {
        SetSemitones(semitones);
    }

    float RealtimePitchShifter::RatioForTuning(
        int semitones,
        int referenceHz)
    {
        if (referenceHz < 1)
            referenceHz = DEFAULT_REFERENCE_HZ;

        const float coarse =
            std::pow(
                2.0f,
                static_cast<float>(semitones) / 12.0f);

        const float reference =
            static_cast<float>(referenceHz) /
            static_cast<float>(DEFAULT_REFERENCE_HZ);

        return coarse * reference;
    }

    void RealtimePitchShifter::SetSemitones(int semitones)
    {
        SetTuning(semitones, DEFAULT_REFERENCE_HZ);
    }

    void RealtimePitchShifter::SetTuning(
        int semitones,
        int referenceHz)
    {
        if (referenceHz < 1)
            referenceHz = DEFAULT_REFERENCE_HZ;

        targetRatio.store(
            RatioForTuning(semitones, referenceHz),
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
        sampleRate =
            format.sampleRate > 0
                ? format.sampleRate
                : 48000;

        lowDelay =
            static_cast<double>(sampleRate) *
            LOW_DELAY_MS *
            0.001;

        centerDelay =
            static_cast<double>(sampleRate) *
            CENTER_DELAY_MS *
            0.001;

        highDelay =
            static_cast<double>(sampleRate) *
            HIGH_DELAY_MS *
            0.001;

        const std::uint32_t requiredHistory =
            static_cast<std::uint32_t>(
                highDelay +
                static_cast<double>(sampleRate) * 0.030 +
                32.0);

        const std::uint32_t historySize =
            NextPowerOfTwo(
                std::max<std::uint32_t>(
                    requiredHistory,
                    2048));

        history.assign(historySize, 0.0f);
        historyMask = historySize - 1;
        writeIndex = 0;

        readDelay = centerDelay;
        oldReadDelay = centerDelay;

        crossfadeLength =
            std::max<std::uint32_t>(
                16,
                static_cast<std::uint32_t>(
                    static_cast<double>(sampleRate) *
                    CROSSFADE_MS *
                    0.001));

        crossfadeRemaining = 0;

        currentRatio =
            targetRatio.load(
                std::memory_order_relaxed);

        ratioStep =
            1.0f /
            std::max(
                1.0f,
                static_cast<float>(sampleRate) *
                RATIO_SLEW_MS *
                0.001f);

        wetStep =
            1.0f /
            std::max(
                1.0f,
                static_cast<float>(sampleRate) *
                WET_RAMP_MS *
                0.001f);

        wetMix = IsNeutral() ? 0.0f : 1.0f;

        previousInput = 0.0f;
        sampleCounter = 0;
        lastPositiveCrossing = 0;

        estimatedPeriod =
            static_cast<double>(sampleRate) / 200.0;

        periodCandidate = estimatedPeriod;
        periodCandidateCount = 0;
    }

    float RealtimePitchShifter::ReadHistory(
        double delayFrames) const
    {
        if (history.empty())
            return 0.0f;

        delayFrames =
            std::clamp(
                delayFrames,
                1.0,
                static_cast<double>(
                    history.size() - 2));

        double position =
            static_cast<double>(writeIndex) -
            delayFrames;

        while (position < 0.0)
            position +=
                static_cast<double>(
                    history.size());

        const double base =
            std::floor(position);

        const std::uint32_t i0 =
            static_cast<std::uint32_t>(base) &
            historyMask;

        const std::uint32_t i1 =
            (i0 + 1) &
            historyMask;

        const float fraction =
            static_cast<float>(
                position - base);

        return
            history[i0] +
            (history[i1] - history[i0]) *
                fraction;
    }

    void RealtimePitchShifter::TrackPeriod(float sample)
    {
        ++sampleCounter;

        const float slope =
            sample - previousInput;

        const bool positiveCrossing =
            previousInput <= 0.0f &&
            sample > 0.0f &&
            slope > CROSSING_SLOPE_FLOOR;

        previousInput = sample;

        if (!positiveCrossing)
            return;

        if (lastPositiveCrossing == 0)
        {
            lastPositiveCrossing = sampleCounter;
            return;
        }

        const std::uint64_t interval =
            sampleCounter -
            lastPositiveCrossing;

        lastPositiveCrossing = sampleCounter;

        const double minPeriod =
            static_cast<double>(sampleRate) /
            MAX_TRACK_HZ;

        const double maxPeriod =
            static_cast<double>(sampleRate) /
            MIN_TRACK_HZ;

        if (interval <
                static_cast<std::uint64_t>(minPeriod) ||
            interval >
                static_cast<std::uint64_t>(maxPeriod))
        {
            return;
        }

        const double measured =
            static_cast<double>(interval);

        const double lower =
            estimatedPeriod *
            (1.0 - PERIOD_CHANGE_LIMIT);

        const double upper =
            estimatedPeriod *
            (1.0 + PERIOD_CHANGE_LIMIT);

        if (measured >= lower &&
            measured <= upper)
        {
            estimatedPeriod =
                estimatedPeriod * 0.82 +
                measured * 0.18;

            periodCandidateCount = 0;
            return;
        }

        const double candidateTolerance =
            std::max(
                4.0,
                periodCandidate * 0.12);

        if (std::fabs(
                measured -
                periodCandidate) <=
            candidateTolerance)
        {
            periodCandidate =
                periodCandidate * 0.6 +
                measured * 0.4;

            ++periodCandidateCount;

            if (periodCandidateCount >= 3)
            {
                estimatedPeriod =
                    periodCandidate;

                periodCandidateCount = 0;
            }
        }
        else
        {
            periodCandidate = measured;
            periodCandidateCount = 1;
        }
    }

    void RealtimePitchShifter::BeginDelayJump(
        double newDelay)
    {
        newDelay =
            std::clamp(
                newDelay,
                lowDelay,
                highDelay);

        oldReadDelay = readDelay;
        readDelay = newDelay;

        crossfadeRemaining =
            crossfadeLength;
    }

    void RealtimePitchShifter::Process(
        float* samples,
        std::uint32_t frameCount)
    {
        if (!samples ||
            history.empty())
        {
            return;
        }

        const float target =
            targetRatio.load(
                std::memory_order_relaxed);

        const bool nowNeutral =
            neutral.load(
                std::memory_order_relaxed);

        for (std::uint32_t i = 0;
             i < frameCount;
             ++i)
        {
            const float dry = samples[i];

            history[writeIndex] = dry;
            TrackPeriod(dry);

            const float ratioDifference =
                target -
                currentRatio;

            if (std::fabs(ratioDifference) <=
                ratioStep)
            {
                currentRatio = target;
            }
            else
            {
                currentRatio +=
                    ratioDifference > 0.0f
                        ? ratioStep
                        : -ratioStep;
            }

            const double drift =
                1.0 -
                static_cast<double>(
                    currentRatio);

            readDelay += drift;

            if (crossfadeRemaining > 0)
                oldReadDelay += drift;

            if (crossfadeRemaining == 0 &&
                std::fabs(drift) >
                    0.000001)
            {
                const double safePeriod =
                    std::clamp(
                        estimatedPeriod,
                        static_cast<double>(
                            sampleRate) /
                            MAX_TRACK_HZ,
                        static_cast<double>(
                            sampleRate) /
                            MIN_TRACK_HZ);

                const double desiredJump =
                    std::max(
                        safePeriod,
                        std::fabs(
                            readDelay -
                            centerDelay));

                int periods =
                    static_cast<int>(
                        std::round(
                            desiredJump /
                            safePeriod));

                periods =
                    std::clamp(
                        periods,
                        1,
                        12);

                const double jump =
                    safePeriod *
                    static_cast<double>(periods);

                if (drift > 0.0 &&
                    readDelay >= highDelay)
                {
                    BeginDelayJump(
                        readDelay -
                        jump);
                }
                else if (drift < 0.0 &&
                         readDelay <= lowDelay)
                {
                    BeginDelayJump(
                        readDelay +
                        jump);
                }
            }

            float wet = 0.0f;

            if (crossfadeRemaining > 0)
            {
                const float progress =
                    1.0f -
                    static_cast<float>(
                        crossfadeRemaining) /
                    static_cast<float>(
                        crossfadeLength);

                const float newGain =
                    std::sin(
                        progress *
                        PI *
                        0.5f);

                const float oldGain =
                    std::cos(
                        progress *
                        PI *
                        0.5f);

                wet =
                    ReadHistory(oldReadDelay) *
                        oldGain +
                    ReadHistory(readDelay) *
                        newGain;

                --crossfadeRemaining;
            }
            else
            {
                wet =
                    ReadHistory(readDelay);
            }

            const float targetWet =
                nowNeutral
                    ? 0.0f
                    : 1.0f;

            if (wetMix < targetWet)
            {
                wetMix =
                    std::min(
                        targetWet,
                        wetMix +
                        wetStep);
            }
            else if (wetMix > targetWet)
            {
                wetMix =
                    std::max(
                        targetWet,
                        wetMix -
                        wetStep);
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

        return static_cast<std::uint32_t>(
            centerDelay);
    }
}
