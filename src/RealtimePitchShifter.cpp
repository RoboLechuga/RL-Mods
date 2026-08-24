#include "RealtimePitchShifter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Audio
{
    namespace
    {
        constexpr int DEFAULT_REFERENCE_HZ = 440;
        constexpr float PI = 3.14159265358979323846f;

        // Short grains keep the added delay close to the old shifter while
        // four-way overlap removes the obvious two-head amplitude pulse.
        constexpr float GRAIN_MS = 10.0f;

        // Search only a small neighborhood when a grain is restarted.
        // This is a WSOLA-style alignment step: choose a nearby source
        // position that best matches the tail we just produced.
        constexpr float SEARCH_MS = 0.70f;
        constexpr float CORRELATION_MS = 1.35f;

        constexpr float RATIO_SLEW_MS = 12.0f;
        constexpr float WET_RAMP_MS = 8.0f;

        constexpr double MIN_READ_DELAY = 2.0;
        constexpr float EPSILON = 1.0e-12f;
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

        grainFrames =
            std::clamp<std::uint32_t>(
                static_cast<std::uint32_t>(
                    static_cast<double>(sampleRate) *
                    GRAIN_MS *
                    0.001),
                256,
                768);

        // Four grains at 75% overlap.
        hopFrames =
            std::max<std::uint32_t>(
                1,
                grainFrames /
                static_cast<std::uint32_t>(GRAIN_COUNT));

        // Keep grainFrames an exact multiple of four so the phases remain
        // evenly staggered forever.
        grainFrames =
            hopFrames *
            static_cast<std::uint32_t>(GRAIN_COUNT);

        searchRadius =
            std::clamp<std::uint32_t>(
                static_cast<std::uint32_t>(
                    static_cast<double>(sampleRate) *
                    SEARCH_MS *
                    0.001),
                16,
                48);

        correlationFrames =
            std::clamp<std::uint32_t>(
                static_cast<std::uint32_t>(
                    static_cast<double>(sampleRate) *
                    CORRELATION_MS *
                    0.001),
                32,
                static_cast<std::uint32_t>(
                    OUTPUT_TAIL_SIZE));

        // The source is always safely in the past. This is the shifted-path
        // latency; neutral E/A440 never uses it for audible output.
        baseDelay =
            static_cast<double>(
                grainFrames +
                searchRadius +
                24);

        const std::uint32_t requiredHistory =
            static_cast<std::uint32_t>(
                baseDelay +
                static_cast<double>(grainFrames) * 0.35 +
                searchRadius +
                correlationFrames +
                64);

        const std::uint32_t historySize =
            NextPowerOfTwo(
                std::max<std::uint32_t>(
                    requiredHistory,
                    2048));

        history.assign(historySize, 0.0f);
        historyMask = historySize - 1;
        writeIndex = 0;

        outputTail.fill(0.0f);
        outputTailWrite = 0;
        outputTailCount = 0;

        for (std::size_t i = 0; i < GRAIN_COUNT; ++i)
        {
            grains[i].age =
                static_cast<std::uint32_t>(i) *
                hopFrames;

            grains[i].anchorDelay =
                baseDelay;
        }

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

        wetMix =
            IsNeutral()
                ? 0.0f
                : 1.0f;
    }

    float RealtimePitchShifter::ReadHistory(
        double delayFrames) const
    {
        if (history.empty())
            return 0.0f;

        delayFrames =
            std::clamp(
                delayFrames,
                MIN_READ_DELAY,
                static_cast<double>(
                    history.size() - 2));

        double position =
            static_cast<double>(writeIndex) -
            delayFrames;

        while (position < 0.0)
            position +=
                static_cast<double>(
                    history.size());

        const double integral =
            std::floor(position);

        const std::uint32_t index0 =
            static_cast<std::uint32_t>(
                integral) &
            historyMask;

        const std::uint32_t index1 =
            (index0 + 1) &
            historyMask;

        const float fraction =
            static_cast<float>(
                position - integral);

        return
            history[index0] +
            (history[index1] -
             history[index0]) *
                fraction;
    }

    float RealtimePitchShifter::GrainWindow(
        std::uint32_t age) const
    {
        const float phase =
            (static_cast<float>(age) + 0.5f) /
            static_cast<float>(grainFrames);

        const float sine =
            std::sin(PI * phase);

        return sine * sine;
    }

    void RealtimePitchShifter::PushOutputTail(float sample)
    {
        outputTail[outputTailWrite] = sample;

        outputTailWrite =
            (outputTailWrite + 1) %
            OUTPUT_TAIL_SIZE;

        if (outputTailCount < OUTPUT_TAIL_SIZE)
            ++outputTailCount;
    }

    float RealtimePitchShifter::ReadOutputTail(
        std::size_t indexFromOldest) const
    {
        if (outputTailCount == 0)
            return 0.0f;

        const std::size_t count =
            outputTailCount;

        if (indexFromOldest >= count)
            indexFromOldest = count - 1;

        const std::size_t oldest =
            (outputTailWrite +
             OUTPUT_TAIL_SIZE -
             count) %
            OUTPUT_TAIL_SIZE;

        return outputTail[
            (oldest + indexFromOldest) %
            OUTPUT_TAIL_SIZE];
    }

    double RealtimePitchShifter::FindAlignedDelay() const
    {
        if (outputTailCount <
            correlationFrames)
        {
            return baseDelay;
        }

        double bestDelay = baseDelay;
        float bestScore =
            -std::numeric_limits<float>::infinity();

        for (int offset =
                 -static_cast<int>(searchRadius);
             offset <=
                 static_cast<int>(searchRadius);
             ++offset)
        {
            const double candidateDelay =
                baseDelay +
                static_cast<double>(offset);

            float dot = 0.0f;
            float sourceEnergy = 0.0f;
            float outputEnergy = 0.0f;

            for (std::uint32_t n = 0;
                 n < correlationFrames;
                 ++n)
            {
                // Oldest -> newest tail sample.
                const float produced =
                    ReadOutputTail(
                        outputTailCount -
                        correlationFrames +
                        n);

                // Match a source segment ending at candidateDelay.
                const double delay =
                    candidateDelay +
                    static_cast<double>(
                        correlationFrames - 1 - n);

                const float source =
                    ReadHistory(delay);

                dot += source * produced;
                sourceEnergy += source * source;
                outputEnergy += produced * produced;
            }

            const float denominator =
                std::sqrt(
                    sourceEnergy *
                    outputEnergy +
                    EPSILON);

            const float score =
                denominator > EPSILON
                    ? dot / denominator
                    : 0.0f;

            if (score > bestScore)
            {
                bestScore = score;
                bestDelay = candidateDelay;
            }
        }

        return bestDelay;
    }

    void RealtimePitchShifter::ResetGrain(Grain& grain)
    {
        grain.age = 0;
        grain.anchorDelay =
            FindAlignedDelay();
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

        for (std::uint32_t frame = 0;
             frame < frameCount;
             ++frame)
        {
            const float dry =
                samples[frame];

            history[writeIndex] =
                dry;

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

            // A read delay that increases by (1-ratio) per sample causes
            // the read position itself to advance at ratio samples/sample.
            const double drift =
                1.0 -
                static_cast<double>(
                    currentRatio);

            float sum = 0.0f;
            float weightSum = 0.0f;

            for (auto& grain : grains)
            {
                const float weight =
                    GrainWindow(grain.age);

                const double delay =
                    grain.anchorDelay +
                    drift *
                    static_cast<double>(
                        grain.age);

                sum +=
                    ReadHistory(delay) *
                    weight;

                weightSum += weight;
            }

            const float wet =
                weightSum > EPSILON
                    ? sum / weightSum
                    : dry;

            // At E Standard / A440 the audible path becomes the untouched
            // input. The granular engine still runs so its history remains
            // warm for the next tuning change.
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

            const float output =
                dry +
                (wet - dry) *
                    wetMix;

            samples[frame] = output;
            PushOutputTail(output);

            for (auto& grain : grains)
            {
                ++grain.age;

                if (grain.age >= grainFrames)
                    ResetGrain(grain);
            }

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
            baseDelay);
    }
}
