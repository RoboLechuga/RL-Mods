#include "DelayLinePitchShifter.hpp"
#include <cmath>

namespace Audio
{
    namespace
    {
        constexpr std::uint32_t RING_SAMPLES = 4096;
        constexpr int TARGET_JUMP_SAMPLES = 256;
        constexpr int MIN_FADE_SAMPLES = 16;
        constexpr int MAX_FADE_SAMPLES = 256;

        constexpr std::uint32_t DECIMATION = 4;
        constexpr std::uint32_t DECIMATED_RING = 1024;
        constexpr std::uint32_t DETECT_INTERVAL_SAMPLES = 256;
        constexpr std::uint32_t DETECT_WINDOW = 256;
        constexpr int MIN_LAG = 12;
        constexpr int MAX_LAG = 240;
        constexpr float OCTAVE_BIAS = 1.15f;

        float SemitonesToRatio(int semitones)
        {
            return std::pow(2.0f, static_cast<float>(semitones) / 12.0f);
        }
    }

    DelayLinePitchShifter::DelayLinePitchShifter(int semitones)
        : ratio(SemitonesToRatio(semitones))
    {
    }

    void DelayLinePitchShifter::SetSemitones(int semitones)
    {
        ratio.store(SemitonesToRatio(semitones), std::memory_order_relaxed);
    }

    void DelayLinePitchShifter::Prepare(const CaptureFormat&)
    {
        ring.assign(RING_SAMPLES, 0.0f);
        writePosition = 0;
        readDelay = 130.0;
        fadeFromDelay = 0.0;
        fadeLength = 64;
        fadeRemaining = 0;

        decimated.assign(DECIMATED_RING, 0.0f);
        decimatedPosition = 0;
        decimationAccumulator = 0.0f;
        decimationPhase = 0;
        samplesSinceDetect = 0;
        periodSamples = 480;
        candidatePeriod = 0;
        candidateVotes = 0;
    }

    float DelayLinePitchShifter::ReadTap(double delay) const
    {
        if (delay < 0.0) delay = 0.0;
        if (delay > RING_SAMPLES - 2.0) delay = RING_SAMPLES - 2.0;

        double readPosition = static_cast<double>(writePosition) - delay;
        if (readPosition < 0.0) readPosition += RING_SAMPLES;

        const std::uint32_t index0 = static_cast<std::uint32_t>(readPosition);
        const std::uint32_t index1 =
            index0 + 1 == RING_SAMPLES ? 0 : index0 + 1;
        const float fraction =
            static_cast<float>(readPosition - index0);

        return ring[index0] + (ring[index1] - ring[index0]) * fraction;
    }

    int DelayLinePitchShifter::RefineAtFullRate(int coarsePeriod) const
    {
        constexpr int SPAN = 6;
        constexpr std::uint32_t WINDOW = 128;
        constexpr std::uint32_t mask = RING_SAMPLES - 1;

        const std::uint32_t base = writePosition;
        float bestSum = 1e9f;
        int bestPeriod = coarsePeriod;

        for (int period = coarsePeriod - SPAN;
             period <= coarsePeriod + SPAN;
             ++period)
        {
            if (period < 8) continue;

            float sum = 0.0f;

            for (std::uint32_t n = 0; n < WINDOW; ++n)
            {
                const std::uint32_t a = (base - n) & mask;
                const std::uint32_t b =
                    (base - n - static_cast<std::uint32_t>(period)) & mask;
                sum += std::fabs(ring[a] - ring[b]);
            }

            if (sum < bestSum)
            {
                bestSum = sum;
                bestPeriod = period;
            }
        }

        return bestPeriod;
    }

    void DelayLinePitchShifter::DetectPeriod()
    {
        float best[MAX_LAG + 1];

        const std::uint32_t newest = decimatedPosition;
        float globalMin = 1e9f;
        int globalLag = 0;

        for (int lag = MIN_LAG; lag <= MAX_LAG; ++lag)
        {
            float sum = 0.0f;

            for (std::uint32_t n = 0; n < DETECT_WINDOW; ++n)
            {
                const std::uint32_t a =
                    (newest - 1 - n) & (DECIMATED_RING - 1);
                const std::uint32_t b =
                    (newest - 1 - n - lag) & (DECIMATED_RING - 1);
                sum += std::fabs(decimated[a] - decimated[b]);
            }

            best[lag] = sum;

            if (sum < globalMin)
            {
                globalMin = sum;
                globalLag = lag;
            }
        }

        if (globalLag == 0) return;

        int chosenLag = globalLag;

        for (int lag = MIN_LAG; lag < globalLag; ++lag)
        {
            if (best[lag] <= globalMin * OCTAVE_BIAS)
            {
                chosenLag = lag;
                break;
            }
        }

        float average = 0.0f;
        for (int lag = MIN_LAG; lag <= MAX_LAG; ++lag)
            average += best[lag];

        average /= static_cast<float>(MAX_LAG - MIN_LAG + 1);

        if (average > 0.0f && best[chosenLag] < average * 0.4f)
        {
            const int refined =
                RefineAtFullRate(chosenLag * static_cast<int>(DECIMATION));

            const int tolerance = periodSamples / 8 + 2;

            if (refined - periodSamples <= tolerance &&
                periodSamples - refined <= tolerance)
            {
                periodSamples = (periodSamples * 3 + refined) / 4;
                candidateVotes = 0;
            }
            else
            {
                const int candidateTolerance = refined / 8 + 2;
                const int distance =
                    refined > candidatePeriod
                        ? refined - candidatePeriod
                        : candidatePeriod - refined;

                if (distance <= candidateTolerance)
                {
                    if (++candidateVotes >= 3)
                    {
                        periodSamples = refined;
                        candidateVotes = 0;
                    }
                }
                else
                {
                    candidatePeriod = refined;
                    candidateVotes = 1;
                }
            }
        }
        else if (candidateVotes > 0)
        {
            --candidateVotes;
        }
    }

    void DelayLinePitchShifter::Process(
        float* samples,
        std::uint32_t frameCount)
    {
        if (ring.empty()) return;

        const float pitchRatio =
            ratio.load(std::memory_order_relaxed);
        const double drift = 1.0 - static_cast<double>(pitchRatio);

        for (std::uint32_t i = 0; i < frameCount; ++i)
        {
            const float incoming = samples[i];

            ring[writePosition] = incoming;

            decimationAccumulator += incoming;
            if (++decimationPhase == DECIMATION)
            {
                decimated[decimatedPosition] =
                    decimationAccumulator * (1.0f / DECIMATION);

                decimatedPosition =
                    (decimatedPosition + 1) & (DECIMATED_RING - 1);

                decimationAccumulator = 0.0f;
                decimationPhase = 0;
            }

            if (++samplesSinceDetect >= DETECT_INTERVAL_SAMPLES)
            {
                samplesSinceDetect = 0;
                DetectPeriod();
            }

            readDelay += drift;

            if (fadeRemaining > 0)
            {
                fadeFromDelay += drift;

                const float progress =
                    1.0f -
                    static_cast<float>(fadeRemaining) /
                    static_cast<float>(fadeLength);

                const float gain =
                    0.5f -
                    0.5f * std::cos(3.14159265f * progress);

                samples[i] =
                    ReadTap(fadeFromDelay) * (1.0f - gain) +
                    ReadTap(readDelay) * gain;

                --fadeRemaining;
            }
            else
            {
                int wholePeriods =
                    (TARGET_JUMP_SAMPLES + periodSamples / 2) /
                    periodSamples;

                if (wholePeriods < 1) wholePeriods = 1;
                if (wholePeriods > 8) wholePeriods = 8;

                const double jump =
                    static_cast<double>(wholePeriods * periodSamples);

                int nextFade = static_cast<int>(jump) / 4;

                if (nextFade < MIN_FADE_SAMPLES)
                    nextFade = MIN_FADE_SAMPLES;

                if (nextFade > MAX_FADE_SAMPLES)
                    nextFade = MAX_FADE_SAMPLES;

                const double upperBound =
                    jump + nextFade + 4.0;

                if (readDelay > upperBound)
                {
                    fadeFromDelay = readDelay;
                    readDelay -= jump;
                    fadeLength = nextFade;
                    fadeRemaining = nextFade;
                }
                else if (readDelay < nextFade + 2.0)
                {
                    fadeFromDelay = readDelay;
                    readDelay += jump;

                    if (drift < 0.0)
                    {
                        const int headroom =
                            static_cast<int>(fadeFromDelay / -drift) - 1;

                        if (headroom < nextFade)
                            nextFade = headroom < 8 ? 8 : headroom;
                    }

                    fadeLength = nextFade;
                    fadeRemaining = nextFade;
                }

                samples[i] = ReadTap(readDelay);
            }

            if (++writePosition == RING_SAMPLES)
                writePosition = 0;
        }
    }

    std::uint32_t DelayLinePitchShifter::GetLatencyFrames() const
    {
        return 480;
    }
}
